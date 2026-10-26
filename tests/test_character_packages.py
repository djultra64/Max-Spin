"""Character session safety and composition tests. Own work: djultra64."""
import contextlib
import errno
import hashlib
import io
import importlib.util
import json
from pathlib import Path
import sys
import struct
import zlib
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'native-linux' / 'tools'))
import character_packages


def pam_fixture(files):
    # Independent hierarchical archive fixture, including on-disk CRC tables.
    paths = set()
    for path in files:
        parts = path.split('/')
        paths.update('/'.join(parts[:i]) for i in range(1, len(parts) + 1))
    paths = sorted(paths)
    names = [path.rsplit('/', 1)[-1] for path in paths]
    count = len(paths)
    pool = b''.join(name.encode() + b'\0' for name in names)
    base = (36 + count * 32 + len(pool) + 3) & ~3
    table, rows, body = bytearray(count * 8), [], bytearray()
    offset = 0
    crc = lambda path: zlib.crc32(path.lower().replace('/', chr(92)).encode())
    for index, (path, name) in enumerate(zip(paths, names)):
        parent = crc(path.rsplit('/', 1)[0]) if '/' in path else 0
        rows.append(struct.pack('<6I', crc(path), index, offset, 0, parent, 0))
        offset += len(name) + 1
        if path in files:
            body.extend(bytes((-len(body)) % 4))
            struct.pack_into('<II', table, index * 8, len(files[path]), base + len(body))
            body.extend(files[path])
        else:
            struct.pack_into('<II', table, index * 8, 0, 0xffffffff)
    lookup = b''.join(sorted(rows, key=lambda row: struct.unpack_from('<I', row)[0]))
    header = b'PAM_PAK\0' + struct.pack('<HH5I', count, count, len(pool), base,
        zlib.crc32(table), zlib.crc32(lookup), zlib.crc32(pool))
    result = header + struct.pack('<I', zlib.crc32(header)) + table + lookup + pool
    return bytes(result + bytes(base - len(result)) + body)


def pml_fixture(rows=None, table='TRPlayer'):
    if rows is None:
        rows = [[('Id', '15'), ('CodeName', 'TukurXX'), ('Name', 'original Federer slot'), ('Age', '20')],
                [('Id', '16'), ('CodeName', 'Houston'), ('Name', 'original Nadal slot'), ('Age', '21')]]
    names = list(dict.fromkeys(['Root', table, 'Row'] + [key for row in rows for key, _ in row]))
    output = bytearray(12)
    key = lambda name: zlib.crc32(name.lower().encode())
    def node(name, children=(), properties=()):
        start = len(output)
        output.extend(struct.pack('<3I', len(children), len(properties), key(name)))
        table = len(output)
        output.extend(bytes(4 * (len(children) + len(properties)) + (4 if not children else 0)))
        offsets = []
        for prop, value in properties:
            offsets.append(len(output))
            output.extend(struct.pack('<I', key(prop)) + value.encode() + b'\0')
            output.extend(bytes((-len(output)) % 4))
        for child in children:
            offsets.append(node(*child))
        struct.pack_into('<' + 'I' * len(offsets), output, table, *offsets)
        return start
    node('Root', [(table, [('Row', (), row) for row in rows])])
    struct.pack_into('<3I', output, 0, 0, len(output), len(names))
    return bytes(output) + b''.join(name.encode() + b'\0' for name in names)


class AssetSessionTests(unittest.TestCase):
    def test_missing_and_empty_packages_use_original_assets_without_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / 'assets'
            assets.mkdir()
            for exists in (False, True):
                packages = root / 'characters'
                if exists:
                    packages.mkdir()
                    (packages / '9').mkdir()
                with character_packages.asset_session(assets, packages) as selected:
                    self.assertEqual(selected, assets)

    def test_empty_packages_do_not_create_explicit_temporary_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root/'assets'
            assets.mkdir()
            session_root = root/'unused-cache'
            with character_packages.asset_session(assets, root/'missing', temporary_root=session_root) as selected:
                self.assertEqual(selected, assets)
            self.assertFalse(session_root.exists())

    def test_invalid_package_falls_back_with_slot_diagnostic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / 'assets'
            assets.mkdir()
            package = root / 'characters' / '9'
            package.mkdir(parents=True)
            (package / 'manifest.json').write_text('{broken')
            warnings = io.StringIO()
            with contextlib.redirect_stderr(warnings):
                with character_packages.asset_session(assets, package.parent) as selected:
                    self.assertEqual(selected, assets)
            self.assertIn('9', warnings.getvalue())
            self.assertIn('default', warnings.getvalue().lower())

    def test_deeply_nested_manifest_is_rejected_without_stopping_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / 'assets'
            assets.mkdir()
            package = root / 'characters' / '9'
            package.mkdir(parents=True)
            (package / 'manifest.json').write_text('[' * 2000 + '0' + ']' * 2000)
            with contextlib.redirect_stderr(io.StringIO()):
                with character_packages.asset_session(assets, package.parent) as selected:
                    self.assertEqual(selected, assets)

    def test_recursive_json_decoder_failure_keeps_original_athlete(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);assets=root/'assets';assets.mkdir()
            package=root/'characters/9';package.mkdir(parents=True)
            (package/'manifest.json').write_text('['*2000+'0'+']'*2000)
            with patch.object(character_packages.json,'loads',side_effect=RecursionError('decoder depth')):
                with contextlib.redirect_stderr(io.StringIO()):
                    with character_packages.asset_session(assets,package.parent) as selected:
                        self.assertEqual(selected,assets)


class FrozenPackageFixture:
    # Native archive/model validation is exercised by test_character_formats.
    # These fixtures isolate filesystem ownership and atomic package handling.
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / 'assets'
        self.packages = self.root / 'characters'
        self.assets.mkdir()
        (self.assets / 'unchanged.bin').write_bytes(b'original texture')
        self.package = self.make_package(9, 'TukurXX')
        self.validation = patch('character_packages._native_validate', return_value=None, create=True)
        self.validation.start()
        self.addCleanup(self.validation.stop)

    def make_package(self, slot, target):
        package = self.packages / str(slot)
        package.mkdir(parents=True)
        files, base = {}, {}
        for outfit in (0, 1):
            archive = f'Media/Wads/Players/{target}{outfit:02d}.wad'
            original = self.assets / archive
            original.parent.mkdir(parents=True, exist_ok=True)
            original.write_bytes(f'original {target}{outfit}'.encode())
            relative = f'outfit{outfit}.wad'
            payload = f'new {target}{outfit}'.encode()
            (package / relative).write_bytes(payload)
            files[archive] = {'path': relative, 'sha256': hashlib.sha256(payload).hexdigest()}
            base[archive] = hashlib.sha256(original.read_bytes()).hexdigest()
        manifest = {'schema':'maxspin-character-v1', 'slot':slot, 'target':target,
                    'name':'Test athlete', 'author':'djultra64', 'base_files':base,
                    'files':files, 'member_updates':[], 'pml_updates':[],
                    'compatibility':{'platform':'xbox', 'validated':True,
                                     'evidence':'synthetic filesystem contract fixture'}}
        (package / 'manifest.json').write_text(json.dumps(manifest))
        return package

    def edit(self, function):
        path = self.package / 'manifest.json'
        manifest = json.loads(path.read_text())
        function(manifest)
        path.write_text(json.dumps(manifest))



class FrozenPackageTests(FrozenPackageFixture, unittest.TestCase):
    def test_replacements_are_private_readonly_frozen_and_removed_after_session(self):
        archive = 'Media/Wads/Players/TukurXX00.wad'
        original = (self.assets / archive).read_bytes()
        with character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertNotEqual(selected, self.assets)
            self.assertEqual((selected / archive).read_bytes(), b'new TukurXX0')
            self.assertFalse((selected / archive).is_symlink())
            self.assertEqual((selected / archive).stat().st_mode & 0o222, 0)
            self.assertNotEqual((selected / archive).stat().st_ino, (self.assets / archive).stat().st_ino)
            self.assertTrue((selected / 'unchanged.bin').is_symlink())
            (self.package / 'outfit0.wad').write_bytes(b'edited during game')
            self.assertEqual((selected / archive).read_bytes(), b'new TukurXX0')
            self.assertEqual((self.assets / archive).read_bytes(), original)
        self.assertFalse(selected.exists())

    @unittest.skipIf(sys.platform == 'win32', 'Linux overlay cache uses POSIX permissions; native Windows session fixtures cover owned cleanup')
    def test_explicit_disk_root_bypasses_default_temp_quota_and_cleans_views(self):
        archive = 'Media/Wads/Players/TukurXX00.wad'
        originals = {str(path.relative_to(self.assets)): path.read_bytes()
                     for path in self.assets.rglob('*') if path.is_file()}
        saves = self.root/'saves'
        saves.mkdir()
        save = saves/'existing-save'
        save.write_bytes(b'original save')
        session_root = self.root/'disk-cache/session-tmp'
        real_mkdtemp = tempfile.mkdtemp
        def quota_on_default(suffix=None, prefix=None, dir=None):
            if dir is None:
                raise OSError(errno.EDQUOT, 'Disk quota exceeded')
            return real_mkdtemp(suffix=suffix, prefix=prefix, dir=dir)
        with patch('character_packages.tempfile.mkdtemp', side_effect=quota_on_default):
            with self.assertRaises(OSError) as raised:
                with character_packages.asset_session(self.assets, self.packages):
                    self.fail('Default temporary storage should hit simulated quota')
            self.assertEqual(raised.exception.errno, errno.EDQUOT)
            for fail in (False, True):
                with self.subTest(exception=fail):
                    try:
                        with character_packages.asset_session(self.assets, self.packages,
                                temporary_root=session_root) as selected:
                            self.assertEqual(selected.parent, session_root)
                            self.assertEqual(session_root.stat().st_mode & 0o777, 0o700)
                            self.assertEqual(selected.stat().st_mode & 0o777, 0o700)
                            self.assertEqual((selected/archive).read_bytes(), b'new TukurXX0')
                            if fail:
                                raise RuntimeError('simulated child failure')
                    except RuntimeError:
                        if not fail:
                            raise
                    self.assertFalse(selected.exists())
                    self.assertEqual(list(session_root.iterdir()), [])
        self.assertEqual({str(path.relative_to(self.assets)): path.read_bytes()
                          for path in self.assets.rglob('*') if path.is_file()}, originals)
        self.assertEqual(save.read_bytes(), b'original save')
        self.assertEqual(list(saves.iterdir()), [save])

    @unittest.skipIf(sys.platform == 'win32', 'Linux launcher requires POSIX resource module; native Windows launcher fixtures cover session ownership')
    def test_actual_launcher_uses_installation_cache_for_frozen_character_view(self):
        native = Path(__file__).resolve().parents[1]/'native-linux'
        sys.path.insert(0, str(native))
        self.addCleanup(lambda: sys.path.remove(str(native)))
        spec = importlib.util.spec_from_file_location('character_storage_launcher', native/'launch.py')
        launcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launcher)
        installation = self.root/'installation'
        installation.mkdir()
        saves = self.root/'saves'
        saves.mkdir()
        (saves/'existing-save').write_bytes(b'original save')
        selected_roots = []
        def run_title(image, build_dir, selected, selected_saves):
            selected_roots.append(selected)
            self.assertEqual(selected.parent, installation/'cache/session-tmp')
            self.assertEqual((selected/'Media/Wads/Players/TukurXX00.wad').read_bytes(), b'new TukurXX0')
            self.assertEqual(selected_saves, saves)
            return 0
        with patch.object(launcher, 'HERE', installation), patch.object(launcher, 'supervise', side_effect=run_title):
            with contextlib.redirect_stdout(io.StringIO()):
                result = launcher.main(['--assets', str(self.assets), '--characters', str(self.packages),
                                        '--saves', str(saves), '--build-dir', str(self.root/'build')])
        self.assertEqual(result, 0)
        self.assertEqual(len(selected_roots), 1)
        self.assertFalse(selected_roots[0].exists())
        self.assertEqual(list((installation/'cache/session-tmp').iterdir()), [])
        self.assertEqual((saves/'existing-save').read_bytes(), b'original save')
        self.assertEqual((self.assets/'Media/Wads/Players/TukurXX00.wad').read_bytes(), b'original TukurXX0')

    def test_target_loads_only_in_verified_visible_selector_folder(self):
        # Actual title capture default-034000ms.png: Tukur is row2,col1;
        # Houston is row2,col2. Their database IDs remain 15 and 16.
        with character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertEqual((selected / 'Media/Wads/Players/TukurXX00.wad').read_bytes(), b'new TukurXX0')
        wrong_folder = self.packages / '15'
        self.package.rename(wrong_folder)
        self.package = wrong_folder
        self.edit(lambda manifest: manifest.update(slot=15))
        with contextlib.redirect_stderr(io.StringIO()):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                self.assertEqual(selected, self.assets)

    def test_bound_shared_figure_source_is_read_only_and_never_replaced(self):
        rig = self.assets / 'Media/Wads/GamePart.wad'
        rig.parent.mkdir(parents=True, exist_ok=True)
        rig.write_bytes(b'bound original figure archive')
        digest = hashlib.sha256(rig.read_bytes()).hexdigest()
        self.edit(lambda m: m['base_files'].update({'Media/Wads/GamePart.wad':digest}))
        with character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertNotEqual(selected, self.assets)
            self.assertTrue((selected / 'Media/Wads/GamePart.wad').is_symlink())
            self.assertEqual((selected / 'Media/Wads/GamePart.wad').read_bytes(), b'bound original figure archive')

    def test_wardrobe_source_binding_stays_original_and_read_only(self):
        archive = 'Media/Wads/PlayerCreatorMaleCloth.wad'
        original = self.assets / archive
        original.parent.mkdir(parents=True, exist_ok=True)
        original.write_bytes(b'original wardrobe source')
        self.edit(lambda m: m['base_files'].update({archive:hashlib.sha256(original.read_bytes()).hexdigest()}))
        with character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertNotEqual(selected, self.assets)
            self.assertTrue((selected / archive).is_symlink())
            self.assertEqual((selected / archive).read_bytes(), b'original wardrobe source')

    def test_rejects_entire_slot_on_invalid_payload_and_keeps_other_slot(self):
        self.make_package(10, 'Houston')
        (self.package / 'outfit1.wad').write_bytes(b'hash mismatch')
        with contextlib.redirect_stderr(io.StringIO()):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                self.assertEqual((selected / 'Media/Wads/Players/TukurXX00.wad').read_bytes(), b'original TukurXX0')
                self.assertEqual((selected / 'Media/Wads/Players/Houston00.wad').read_bytes(), b'new Houston0')

    def test_unvalidated_wrong_source_and_incomplete_packages_keep_defaults(self):
        changes = [lambda m: m['compatibility'].update(validated=False),
                   lambda m: m['base_files'].update({'Media/Wads/Players/TukurXX00.wad':'0'*64}),
                   lambda m: m['files'].pop('Media/Wads/Players/TukurXX01.wad'),
                   lambda m: m.update(target='Houston'),
                   lambda m: m.update(name=float('nan'))]
        original_manifest = (self.package / 'manifest.json').read_text()
        for change in changes:
            with self.subTest(change=change):
                (self.package / 'manifest.json').write_text(original_manifest)
                self.edit(change)
                with contextlib.redirect_stderr(io.StringIO()):
                    with character_packages.asset_session(self.assets, self.packages) as selected:
                        self.assertEqual(selected, self.assets)

    def test_rejects_traversal_and_symlink_payloads_and_duplicate_json_keys(self):
        path = self.package / 'manifest.json'
        original_manifest = path.read_text()
        self.edit(lambda m: next(iter(m['files'].values())).update(path='../outside.wad'))
        with contextlib.redirect_stderr(io.StringIO()):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                self.assertEqual(selected, self.assets)
        path.write_text(original_manifest)
        (self.root / 'outside.wad').write_bytes(b'new TukurXX0')
        (self.package / 'outfit0.wad').unlink()
        (self.package / 'outfit0.wad').symlink_to(self.root / 'outside.wad')
        with contextlib.redirect_stderr(io.StringIO()):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                self.assertEqual(selected, self.assets)
        (self.package / 'outfit0.wad').unlink()
        (self.package / 'outfit0.wad').write_bytes(b'new TukurXX0')
        path.write_text(original_manifest.replace('"slot": 9', '"slot": 9, "slot": 10'))
        with contextlib.redirect_stderr(io.StringIO()):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                self.assertEqual(selected, self.assets)


class NativePackageIntegrationTests(FrozenPackageFixture, unittest.TestCase):
    def native_package(self, package, target, pixel=0):
        fixture_path = Path(__file__).resolve().parents[1] / 'native-linux/tests/test_character_formats.py'
        specification = importlib.util.spec_from_file_location('native_character_test_fixtures', fixture_path)
        fixtures = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(fixtures)
        manifest_path = package / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        for outfit in (0, 1):
            prefix = f'media/gamepart/players/{target.lower()}{outfit:02d}/'
            members = {prefix+'playerlod00.ooo': fixtures.fixture_model(),
                       prefix+'playerlod01.ooo': fixtures.fixture_model(),
                       prefix+'textures/tex.pak': fixtures.fixture_tex(),
                       prefix+'player.ini': b'[General]\nName = Tukur\n',
                       prefix+'scorename.dds': b'preserved',
                       prefix+'soundscore.pml': b'preserved score',
                       prefix+'soundevent.pml': b'preserved event'}
            original = pam_fixture(members)
            members[prefix+'textures/tex.pak'] = fixtures.fixture_tex()[:-8] + bytes([pixel]) * 8
            candidate = pam_fixture(members)
            archive = f'Media/Wads/Players/{target}{outfit:02d}.wad'
            (self.assets / archive).write_bytes(original)
            (package / f'outfit{outfit}.wad').write_bytes(candidate)
            manifest['base_files'][archive] = hashlib.sha256(original).hexdigest()
            manifest['files'][archive]['sha256'] = hashlib.sha256(candidate).hexdigest()
        manifest_path.write_text(json.dumps(manifest))

    def test_complete_native_package_passes_real_model_and_texture_validation(self):
        self.validation.stop()
        self.native_package(self.package, 'TukurXX')
        with character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertNotEqual(selected, self.assets)
            self.assertEqual((selected / 'Media/Wads/Players/TukurXX00.wad').read_bytes(), (self.package / 'outfit0.wad').read_bytes())

    def test_texture_id_collision_rejects_second_slot_and_preserves_prior_database_edits(self):
        import character_formats as formats
        self.validation.stop()
        self.native_package(self.package, 'TukurXX', 3)
        SharedDatabaseTests.bind_database(self, self.package, 'TukurXX', 15, 'Roger Federer')
        nadal = self.make_package(10, 'Houston')
        self.native_package(nadal, 'Houston', 7)
        SharedDatabaseTests.bind_database(self, nadal, 'Houston', 16, 'Rafael Nadal')
        warnings = io.StringIO()
        with contextlib.redirect_stderr(warnings):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                raw = formats.payloads((selected / 'Media/Wads/Db.wad').read_bytes())['media/db/players(m).pml']
                database = formats.Pml(formats.unpack_zlb(raw))
                self.assertEqual(database.get('TRPlayer/Id=15', 'Name'), 'Roger Federer')
                self.assertEqual(database.get('TRPlayer/Id=16', 'Name'), 'original Nadal slot')
                self.assertEqual((selected / 'Media/Wads/Players/Houston00.wad').read_bytes(), (self.assets / 'Media/Wads/Players/Houston00.wad').read_bytes())
        self.assertIn('10', warnings.getvalue())
        self.assertIn('conflict', warnings.getvalue().lower())

    def test_same_texture_id_and_pixels_can_be_shared_across_packages(self):
        self.validation.stop()
        self.native_package(self.package, 'TukurXX', 3)
        nadal = self.make_package(10, 'Houston')
        self.native_package(nadal, 'Houston', 3)
        with character_packages.asset_session(self.assets, self.packages) as selected:
            for target, package in [('TukurXX', self.package), ('Houston', nadal)]:
                self.assertEqual((selected / f'Media/Wads/Players/{target}00.wad').read_bytes(), (package / 'outfit0.wad').read_bytes())


class SharedDatabaseTests(FrozenPackageFixture, unittest.TestCase):
    def bind_database(self, package, target, identifier, value):
        archive = 'Media/Wads/Db.wad'
        original = self.assets / archive
        original.parent.mkdir(parents=True, exist_ok=True)
        if not original.exists():
            original.write_bytes(pam_fixture({'media/db/players(m).pml': pml_fixture()}))
        path = package / 'manifest.json'
        manifest = json.loads(path.read_text())
        manifest['base_files'][archive] = hashlib.sha256(original.read_bytes()).hexdigest()
        manifest['pml_updates'] = [{'archive':archive, 'member':'media/db/players(m).pml',
            'node':f'TRPlayer/Id={identifier}', 'property':'Name', 'value':value}]
        path.write_text(json.dumps(manifest))

    def test_two_packages_compose_distinct_rows_in_shared_database(self):
        import character_formats as formats
        self.bind_database(self.package, 'TukurXX', 15, 'Roger Federer')
        nadal = self.make_package(10, 'Houston')
        self.bind_database(nadal, 'Houston', 16, 'Rafael Nadal')
        original = (self.assets / 'Media/Wads/Db.wad').read_bytes()
        with character_packages.asset_session(self.assets, self.packages) as selected:
            raw = formats.payloads((selected / 'Media/Wads/Db.wad').read_bytes())['media/db/players(m).pml']
            database = formats.Pml(formats.unpack_zlb(raw))
            self.assertEqual(database.get('TRPlayer/Id=15', 'Name'), 'Roger Federer')
            self.assertEqual(database.get('TRPlayer/Id=16', 'Name'), 'Rafael Nadal')
            self.assertEqual(database.get('TRPlayer/Id=15', 'Age'), '20')
        self.assertEqual((self.assets / 'Media/Wads/Db.wad').read_bytes(), original)

    def test_out_of_range_or_foreign_row_metadata_falls_back_whole_slot(self):
        self.bind_database(self.package, 'TukurXX', 15, 'Roger Federer')
        original = (self.package / 'manifest.json').read_text()
        changes = [lambda row: row.update(property='Age', value='NaN'),
                   lambda row: row.update(property='Age', value='300'),
                   lambda row: row.update(node='TRPlayer/Id=16'),
                   lambda row: row.update(property='CodeName', value='Houston')]
        for change in changes:
            with self.subTest(change=change):
                (self.package / 'manifest.json').write_text(original)
                self.edit(lambda m: change(m['pml_updates'][0]))
                with contextlib.redirect_stderr(io.StringIO()):
                    with character_packages.asset_session(self.assets, self.packages) as selected:
                        self.assertEqual(selected, self.assets)


class SharedMemberTests(FrozenPackageFixture, unittest.TestCase):
    def bind_member(self, package, target, value):
        archive = 'Media/Wads/GameIni.wad'
        original = self.assets / archive
        original.parent.mkdir(parents=True, exist_ok=True)
        if not original.exists():
            original.write_bytes(pam_fixture({
                'media/gamepart/players/tukurxx00/player.ini': b'[General]\nName = Original\nShadow = original.ooo\n',
                'media/gamepart/players/houston00/player.ini': b'[General]\nName = Original\nShadow = original.ooo\n'}))
        payload = f'[General]\nName = {value}\nShadow = original.ooo\n'.encode()
        (package / 'player.ini').write_bytes(payload)
        path = package / 'manifest.json'
        manifest = json.loads(path.read_text())
        manifest['base_files'][archive] = hashlib.sha256(original.read_bytes()).hexdigest()
        manifest['member_updates'] = [{'archive':archive,
            'member':f'media/gamepart/players/{target.lower()}00/player.ini',
            'path':'player.ini', 'sha256':hashlib.sha256(payload).hexdigest()}]
        path.write_text(json.dumps(manifest))

    def test_target_members_compose_in_shared_archive_and_freeze(self):
        import character_formats as formats
        self.bind_member(self.package, 'TukurXX', 'Federer')
        nadal = self.make_package(10, 'Houston')
        self.bind_member(nadal, 'Houston', 'Nadal')
        with character_packages.asset_session(self.assets, self.packages) as selected:
            members = formats.payloads((selected / 'Media/Wads/GameIni.wad').read_bytes())
            self.assertIn(b'Name = Federer', members['media/gamepart/players/tukurxx00/player.ini'])
            self.assertIn(b'Name = Nadal', members['media/gamepart/players/houston00/player.ini'])
            (self.package / 'player.ini').write_bytes(b'edited while playing')
            self.assertIn(b'Name = Federer', formats.payloads((selected / 'Media/Wads/GameIni.wad').read_bytes())['media/gamepart/players/tukurxx00/player.ini'])

    def test_duplicate_native_hair_can_only_be_disabled(self):
        import character_formats as formats
        self.bind_member(self.package, 'TukurXX', 'Federer')
        archive = 'Media/Wads/GameIni.wad'
        member = 'media/gamepart/players/tukurxx00/player.ini'
        original = self.assets / archive
        entries = formats.payloads(original.read_bytes())
        entries[member] += b'[Accessories]\nHair = Manka\n'
        original.write_bytes(pam_fixture(entries))
        candidate = b'[General]\nName = Federer\nShadow = original.ooo\n[Accessories]\nHair = \n'
        for hair, accepted in ((b'', True), (b'foreign', False)):
            data = candidate.replace(b'Hair = \n', b'Hair = '+hair+b'\n')
            (self.package / 'player.ini').write_bytes(data)
            self.edit(lambda m: (m['base_files'].update({archive:hashlib.sha256(original.read_bytes()).hexdigest()}),
                                 m['member_updates'][0].update(sha256=hashlib.sha256(data).hexdigest())))
            with contextlib.redirect_stderr(io.StringIO()):
                with character_packages.asset_session(self.assets, self.packages) as selected:
                    if accepted:
                        self.assertNotEqual(selected, self.assets)
                        self.assertIn(b'Hair = \n', formats.payloads((selected / archive).read_bytes())[member])
                    else:
                        self.assertEqual(selected, self.assets)

    def test_duplicate_member_foreign_member_and_dependency_rewrite_reject_slot(self):
        self.bind_member(self.package, 'TukurXX', 'Federer')
        original_manifest = (self.package / 'manifest.json').read_text()
        original_payload = (self.package / 'player.ini').read_bytes()
        changes = [lambda m: m['member_updates'].append(dict(m['member_updates'][0])),
                   lambda m: m['member_updates'][0].update(member='media/gamepart/players/houston00/player.ini')]
        for change in changes:
            (self.package / 'manifest.json').write_text(original_manifest)
            self.edit(change)
            with contextlib.redirect_stderr(io.StringIO()):
                with character_packages.asset_session(self.assets, self.packages) as selected:
                    self.assertEqual(selected, self.assets)
        (self.package / 'manifest.json').write_text(original_manifest)
        payload = original_payload.replace(b'original.ooo', b'foreign.ooo')
        (self.package / 'player.ini').write_bytes(payload)
        self.edit(lambda m: m['member_updates'][0].update(sha256=hashlib.sha256(payload).hexdigest()))
        with contextlib.redirect_stderr(io.StringIO()):
            with character_packages.asset_session(self.assets, self.packages) as selected:
                self.assertEqual(selected, self.assets)


class FemalePackageTests(FrozenPackageFixture, unittest.TestCase):
    def test_selector_metadata_requires_source_gender_to_match_slot(self):
        import character_formats as formats
        package = self.make_package(17, 'HingisX')
        path = package / 'manifest.json'
        manifest = json.loads(path.read_text())
        archive = 'Media/Wads/Db.wad'
        member = 'media/db/playerlist.pml'
        manifest['pml_updates'] = [{'archive':archive, 'member':member,
                                   'node':'TPLayerInfo/DirName=HingisX00', 'property':'Name', 'value':'New woman'}]
        for source_gender, accepted in [('1', True), ('0', False)]:
            original = pam_fixture({member: pml_fixture([[('DirName','HingisX00'), ('Name','Original'),
                                                         ('IsWoman',source_gender)]], 'TPLayerInfo')})
            (self.assets / archive).write_bytes(original)
            manifest['base_files'][archive] = hashlib.sha256(original).hexdigest()
            path.write_text(json.dumps(manifest))
            with contextlib.redirect_stderr(io.StringIO()):
                with character_packages.asset_session(self.assets, self.packages) as selected:
                    self.assertEqual((selected / 'Media/Wads/Players/HingisX00.wad').read_bytes(),
                                     b'new HingisX0' if accepted else b'original HingisX0')

    def test_woman_slot_accepts_own_outfits_and_female_cloth_source(self):
        package = self.make_package(17, 'HingisX')
        archive = 'Media/Wads/PlayerCreatorFemaleCloth.wad'
        (self.assets / archive).write_bytes(b'original female wardrobe')
        manifest_path = package / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['gender'] = 'female'
        manifest['base_files'][archive] = hashlib.sha256(b'original female wardrobe').hexdigest()
        manifest_path.write_text(json.dumps(manifest))
        with character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertEqual((selected / 'Media/Wads/Players/HingisX00.wad').read_bytes(), b'new HingisX0')
            self.assertEqual((selected / 'Media/Wads/Players/TukurXX00.wad').read_bytes(), b'new TukurXX0')

    def test_female_metadata_updates_only_own_womens_database_rows(self):
        import character_formats as formats
        package = self.make_package(17, 'HingisX')
        female_pml = pml_fixture().replace(b'TukurXX', b'HingisX').replace(b'Houston', b'AnnaXXX')
        # ID values have equal byte widths only after parsing the independent fixture.
        tree = formats.Pml(female_pml)
        tree.set('TRPlayer/Id=15', 'Id', '516')
        tree.set('TRPlayer/Id=16', 'Id', '517')
        archive = 'Media/Wads/Db.wad'
        original = pam_fixture({'media/db/players(w).pml': tree.encode(), 'media/db/profils(w).pml': tree.encode(),
                                'media/db/players(m).pml': pml_fixture()})
        (self.assets / archive).write_bytes(original)
        manifest_path = package / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['base_files'][archive] = hashlib.sha256(original).hexdigest()
        manifest['pml_updates'] = [{'archive':archive, 'member':member, 'node':'TRPlayer/Id=516',
                                   'property':'Name', 'value':'New woman'}
                                  for member in ('media/db/players(w).pml','media/db/profils(w).pml')]
        manifest_path.write_text(json.dumps(manifest))
        with character_packages.asset_session(self.assets, self.packages) as selected:
            members = formats.payloads((selected / archive).read_bytes())
            self.assertEqual(formats.Pml(members['media/db/players(w).pml']).get('TRPlayer/Id=516', 'Name'), 'New woman')
            self.assertEqual(members['media/db/players(m).pml'], pml_fixture())
        for edit in (lambda m: m.update(gender='male'),
                     lambda m: m['pml_updates'][0].update(member='media/db/players(m).pml'),
                     lambda m: m['pml_updates'][0].update(node='TRPlayer/Id=517')):
            changed = json.loads(json.dumps(manifest))
            edit(changed)
            manifest_path.write_text(json.dumps(changed))
            with contextlib.redirect_stderr(io.StringIO()):
                with character_packages.asset_session(self.assets, package.parent) as selected:
                    self.assertEqual((selected / 'Media/Wads/Players/HingisX00.wad').read_bytes(), b'original HingisX0')

    def test_gender_and_cross_gender_wardrobe_bindings_reject_whole_slot(self):
        for slot, target, gender, archive in [(9, 'TukurXX', 'female', None),
                                            (17, 'HingisX', 'male', None),
                                            (17, 'HingisX', 'female', 'Media/Wads/PlayerCreatorMaleCloth.wad')]:
            package = self.package if slot == 9 else self.packages / str(slot)
            if not package.exists():
                self.make_package(slot, target)
            manifest_path = package / 'manifest.json'
            manifest = json.loads(manifest_path.read_text())
            manifest['gender'] = gender
            if archive:
                (self.assets / archive).write_bytes(b'foreign wardrobe')
                manifest['base_files'][archive] = hashlib.sha256(b'foreign wardrobe').hexdigest()
            manifest_path.write_text(json.dumps(manifest))
            with contextlib.redirect_stderr(io.StringIO()):
                with character_packages.asset_session(self.assets, self.packages) as selected:
                    self.assertEqual((selected / f'Media/Wads/Players/{target}00.wad').read_bytes(), f'original {target}0'.encode())


if __name__ == '__main__':
    unittest.main()
