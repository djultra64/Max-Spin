"""Bounded native character formats; own fixtures and tests: djultra64."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))


def words(*values):
    return struct.pack('<' + 'I' * len(values), *values)


def fixture_pam(files):
    """Small independently built PAM with hierarchical directory records."""
    files = {(name if name.lower().startswith('media/') else 'Media/' + name): value for name, value in files.items()}
    paths = set()
    for path in files:
        parts = path.split('/')
        paths.update('/'.join(parts[:i]) for i in range(1, len(parts) + 1))
    paths = sorted(paths)
    names = [p.rsplit('/', 1)[-1] for p in paths]
    count = len(paths)
    pool = b''.join(n.encode() + b'\0' for n in names)
    base = (36 + count * 32 + len(pool) + 3) & ~3
    rows = []
    table = bytearray(count * 8)
    body = bytearray()
    name_at = 0
    crc = lambda path: zlib.crc32(path.lower().replace('/', '\\').encode())
    for i, (path, name) in enumerate(zip(paths, names)):
        key = crc(path)
        parent = crc(path.rsplit('/', 1)[0]) if '/' in path else 0
        rows.append(words(key, i, name_at, 0, parent, 0))
        name_at += len(name) + 1
        if path not in files:
            struct.pack_into('<II', table, i * 8, 0, 0xffffffff)
        else:
            body.extend(bytes((-len(body)) % 4))
            data = files[path]
            struct.pack_into('<II', table, i * 8, len(data), base + len(body))
            body.extend(data)
    rows.sort(key=lambda row: struct.unpack_from('<I', row)[0])
    lookup = b''.join(rows)
    header = b'PAM_PAK\0' + struct.pack('<HH5I', count, count, len(pool), base,
        zlib.crc32(table), zlib.crc32(lookup), zlib.crc32(pool))
    result = header + words(zlib.crc32(header)) + table + lookup + pool
    return bytes(result + bytes(base - len(result)) + body)


def fixture_tex():
    # One 4x4 BC1 texture; the single 8-byte block follows its 36-byte row.
    return words(0x1234569f, 1, 0x10000001, 0x42, 44, 4, 4, 4, 1, 0, 0) + bytes(8)


def fixture_model(skin=False):
    # One rigid triangle strip with a real MA3 material and 32-byte vertices.
    material = bytearray(112)
    material[84:92] = b'PAM-MA3\0'
    name = b'B_t\xeate'
    program = words(3, 0, 4, 0, 26, 131072, 0xffff0000, 0xffffffff,
                    11, 0, 17, 3, 3 << 16)
    stream_format = (14 if skin is True else skin) if skin else 0
    weight_count = stream_format - 13 if skin else 0
    tail = (struct.pack('<' + 'f' * weight_count, 1, *([0] * (weight_count - 1))) + bytes(4 if stream_format == 14 else 8)) if skin else b''
    records = b''.join(struct.pack('<8f', *v) + tail for v in [
        (0, 0, 0, 0, 0, 1, 0, 0), (1, 0, 0, 0, 0, 1, 1, 0),
        (0, 1, 0, 0, 0, 1, 0, 1)])
    kind = 0x1234569b if skin else 0x12345679
    if skin:
        program = program[:36] + words(stream_format) + program[40:]
    node = words(kind, zlib.crc32(name), 0, 0, 0, 0, 1)
    node += b'OBJECT_NAME' + words(len(name)) + name
    node += words(0x1234568b, 1, 0x42, 0x10000001, 0x1234568c, 1)
    node += material + words(0x1234568e, 5, len(program)) + program
    node += words(0x12345684, 3) + struct.pack('<3H', 0, 1, 2) + words(0)
    if not skin:
        node += words(0x12345695, 1) + struct.pack('<6f', 0, 0, 0, 1, 1, 0)
    node += words(0x12345697, 1, 0x123456a1, 3, stream_format) + records
    if skin:
        node += words(0x1234567e, 1, 0x7fb21946, 0x7fb21946, 0x7fb21946, 0x7fb21946, 0)
    node += words(0x12345694, 0)
    return words(0x1234567c, 1, kind) + node + words(0x12345694, 0)


class CharacterFormats(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = TOOLS / 'character_formats.py'
        if path.exists():
            import character_formats
            cls.f = character_formats
        else:
            cls.f = None

    def require_formats(self):
        self.assertIsNotNone(self.f, 'bounded native format layer is missing')
        return self.f

    def test_original_texture_namespaces_are_explicit_and_candidates_stay_strict(self):
        f = self.require_formats()
        for namespace in (0x00000006, 0x10000007):
            raw = bytearray(fixture_tex())
            struct.pack_into('<I', raw, 8, namespace)
            with self.subTest(namespace=namespace):
                with self.assertRaisesRegex(ValueError, 'Invalid native texture row'):
                    f.parse_texpak(bytes(raw))
                entries = f.parse_texpak(bytes(raw), original_source=True)
                self.assertEqual(entries[0]['id'], '%08x00000042' % namespace)
                self.assertEqual(entries[0]['bytes'], 8)
                malformed = bytearray(raw)
                struct.pack_into('<I', malformed, 16, 43)
                with self.assertRaisesRegex(ValueError, 'Invalid native texture row'):
                    f.parse_texpak(bytes(malformed), original_source=True)
                malformed = bytearray(raw)
                struct.pack_into('<I', malformed, 8, 0x10000008)
                with self.assertRaisesRegex(ValueError, 'Invalid native texture row'):
                    f.parse_texpak(bytes(malformed), original_source=True)

    def test_zero_change_archive_is_identical_and_one_leaf_preserves_other_bytes(self):
        f = self.require_formats()
        original = fixture_pam({'one.ini': b'one', 'two.ini': b'two\0'})
        self.assertEqual(f.rebuild_pam(original, {}), original)
        self.assertEqual(f.rebuild_pam(original, {'media/one.ini': b'one'}), original)
        changed = f.rebuild_pam(original, {'media/one.ini': b'longer changed value'})
        self.assertEqual(f.payloads(changed), {
            'media/one.ini': b'longer changed value', 'media/two.ini': b'two\0'})
        with self.assertRaises(ValueError):
            f.rebuild_pam(original, {'media/missing.ini': b'bad'})

    def test_archive_checksum_and_path_crc_corruption_are_rejected(self):
        f = self.require_formats()
        original = bytearray(fixture_pam({'one.ini': b'data'}))
        original[20] ^= 1
        with self.assertRaises(ValueError):
            f.payloads(bytes(original))

    def test_zlb_roundtrip_and_corrupt_block_rejection(self):
        f = self.require_formats()
        raw = b'original character data' * 20000
        compressed = f.pack_zlb(raw)
        self.assertEqual(f.unpack_zlb(compressed), raw)
        bad = bytearray(compressed)
        bad[-1] ^= 1
        with self.assertRaises(ValueError):
            f.unpack_zlb(bytes(bad))
        self.assertEqual(f.unpack_zlb(b'uncompressed'), b'uncompressed')

    def test_texture_rows_consume_exact_mips_and_reject_bad_offset(self):
        f = self.require_formats()
        self.assertEqual(f.parse_texpak(fixture_tex())[0]['id'], '1000000100000042')
        bad = bytearray(fixture_tex())
        struct.pack_into('<I', bad, 16, 45)
        with self.assertRaises(ValueError):
            f.parse_texpak(bytes(bad))

    def test_full_rigid_model_and_texture_dependency_are_checked(self):
        f = self.require_formats()
        report = f.validate_models({'lod00': fixture_model()}, fixture_tex())
        self.assertEqual(report['models']['lod00']['vertices'], 3)
        self.assertEqual(report['models']['lod00']['objects'], 1)
        bad = bytearray(fixture_tex())
        struct.pack_into('<I', bad, 12, 0x43)
        with self.assertRaisesRegex(ValueError, 'texture'):
            f.validate_models({'lod00': fixture_model()}, bytes(bad))

    def test_model_exposes_actual_typed_draw_strip_indices_for_lod_uv_validation(self):
        f = self.require_formats()
        rigid = f.parse_model(fixture_model())
        self.assertEqual(rigid['nodes'][0]['draw_ranges'], [{'format': 0, 'indices': [0, 1, 2]}])
        skin = f.parse_model(fixture_model(skin=15))
        self.assertEqual(skin['nodes'][0]['draw_ranges'], [{'format': 15, 'indices': [0, 1, 2]}])

    def test_model_rejects_index_range_nonfinite_and_unconsumed_tail(self):
        f = self.require_formats()
        raw = fixture_model()
        at = raw.index(words(0x12345684)) + 8
        bad = bytearray(raw)
        struct.pack_into('<H', bad, at, 9)
        with self.assertRaises(ValueError):
            f.parse_model(bytes(bad))
        at = raw.index(words(0x123456a1)) + 12
        bad = bytearray(raw)
        struct.pack_into('<f', bad, at, float('nan'))
        with self.assertRaises(ValueError):
            f.parse_model(bytes(bad))
        with self.assertRaises(ValueError):
            f.parse_model(raw + b'unknown')

    def test_original_xbox_creator_eye_global_reference_is_not_a_missing_texture(self):
        f = self.require_formats()
        raw = bytearray(fixture_model())
        at = raw.index(words(0x1234568b)) + 8
        struct.pack_into('<II', raw, at, 0xfcd68a40, 1)
        report = f.validate_models({'lod00': bytes(raw)}, fixture_tex())
        self.assertEqual(report['models']['lod00']['global_ids'], ['00000001fcd68a40'])
        struct.pack_into('<I', raw, at, 0xfcd68a41)
        with self.assertRaises(ValueError):
            f.parse_model(bytes(raw))

    def test_native_saved_racket_index_metadata_is_preserved(self):
        f = self.require_formats()
        raw = bytearray(fixture_model())
        at = raw.index(words(0x12345684)) + 8 + 6
        for metadata in (8, 24):
            struct.pack_into('<I', raw, at, metadata)
            self.assertEqual(f.parse_model(bytes(raw))['vertices'], 3)

    def test_pml_selector_changes_only_selected_row_and_zero_replay_is_identical(self):
        f = self.require_formats()
        # Independent PML fixture with table/row/value offsets written explicitly.
        names = b'Table\0Row\0Id\0Name\0'
        key = lambda name: zlib.crc32(name.lower().encode())
        raw = bytearray(108)
        struct.pack_into('<3I', raw, 0, 0, 108, 4)
        struct.pack_into('<4I', raw, 12, 1, 0, key('Root'), 28)
        struct.pack_into('<4I', raw, 28, 1, 0, key('Table'), 44)
        struct.pack_into('<5I', raw, 44, 0, 2, key('Row'), 68, 80)
        struct.pack_into('<I', raw, 68, key('Id'))
        raw[72:75] = b'15\0'
        struct.pack_into('<I', raw, 80, key('Name'))
        raw[84:88] = b'old\0'
        original = bytes(raw) + names
        pml = f.Pml(original)
        self.assertEqual(pml.encode(), original)
        pml.set('Table/Id=15', 'Name', 'Federer')
        decoded = f.Pml(pml.encode())
        self.assertEqual(decoded.get('Table/Id=15', 'Name'), 'Federer')
        self.assertEqual(decoded.get('Table/Id=15', 'Id'), '15')
        with self.assertRaises(ValueError):
            decoded.set('Table/Id=15', 'missing', 'value')

    def test_figure_resolves_case_sensitive_accented_bone_names(self):
        f = self.require_formats()
        names = b'B_l\xe8vre_inf_G_01_01\0B_front_G_01\0'
        transform = struct.pack('<10f', 0, 0, 0, 1, 1, 1, 0, 0, 0, 1)
        raw = words(0xcccccccc, 2) + transform + words(0) + transform + words(20) + names
        self.assertEqual(f.parse_figure(raw)['3851b2aa'], 'B_l\xe8vre_inf_G_01_01')
        self.assertEqual(f.parse_figure(raw)['596ef424'], 'B_front_G_01')

    def test_userdata_conversion_preserves_every_geometry_skin_uv_byte(self):
        f = self.require_formats()
        model = fixture_model()
        at = 12 + 8
        saved = model[:at] + b'PAM-OBJ\0' + bytes(128) + model[at:]
        converted, proof = f.convert_model(saved)
        self.assertEqual(converted, model)
        self.assertEqual(proof['removed_userdata_records'], 1)
        self.assertEqual(f.convert_model(model)[0], model)

    def test_texture_namespace_changes_only_identifiers_and_typed_references(self):
        f = self.require_formats()
        models = {'lod00': fixture_model()}
        revised, pack, mapping = f.namespace_textures(models, fixture_tex(), 'federer')
        before = f.parse_model(models['lod00'])
        after = f.parse_model(revised['lod00'])
        self.assertNotEqual(mapping['1000000100000042'], '1000000100000042')
        self.assertEqual(before['nodes'][0]['streams'], after['nodes'][0]['streams'])
        self.assertEqual(fixture_tex()[44:], pack[44:])
        f.validate_models(revised, pack)
        _, other, other_mapping = f.namespace_textures(models, fixture_tex(), 'nadal')
        self.assertNotEqual(mapping, other_mapping)

    def test_changed_pixels_receive_distinct_ids_between_outfits_of_same_character(self):
        f = self.require_formats()
        _, _, original = f.namespace_textures({'lod00': fixture_model()}, fixture_tex(), 'federer')
        changed = fixture_tex()[:-1] + b'\1'
        _, _, revised = f.namespace_textures({'lod00': fixture_model()}, changed, 'federer')
        self.assertNotEqual(original['1000000100000042'], revised['1000000100000042'])

    def test_preparation_disables_only_legacy_hair_and_keeps_other_xbox_dependencies(self):
        path = TOOLS / 'prepare_characters.py'
        self.assertTrue(path.exists(), 'native candidate preparation is missing')
        import prepare_characters
        original = b'[General]\r\nNewLod = 1\r\nShadow = ShadowTShirtBaggy.ooo\r\nName = Houston\r\nShortName = Hou\r\nLongName = P. Houston\r\n[Accessories]\r\nHair = Dread\r\n'
        revised = prepare_characters.character_ini(original, 'Nadal', 'Nad', 'R. Nadal')
        self.assertIn(b'LongName = R. Nadal', revised)
        self.assertIn(b'Shadow = ShadowTShirtBaggy.ooo', revised)
        self.assertIn(b'Hair =\r\n', revised)
        self.assertNotIn(b'Dread', revised)
        self.assertNotIn(b'FromPlayerCreator', revised)
        self.assertNotIn(b'Player_ADN', revised)

    def test_each_actual_outfit_disables_legacy_hair_without_touching_shadow_or_other_keys(self):
        import configparser
        import prepare_characters as prep
        f = self.require_formats()
        assets = TOOLS.parents[1] / 'assets'
        if not assets.is_dir():
            self.skipTest('private original Xbox assets are not installed')
        game_ini = f.payloads((assets / 'Media/Wads/GameIni.wad').read_bytes())
        for target in ('TukurXX', 'Houston'):
            for outfit in (0, 1):
                prefix = 'media/gamepart/players/%s%02d/' % (target.lower(), outfit)
                archive = assets / ('Media/Wads/Players/%s%02d.wad' % (target, outfit))
                for original in (f.payloads(archive.read_bytes())[prefix + 'player.ini'], game_ini[prefix + 'player.ini']):
                    raw = f.unpack_zlb(original)
                    before = configparser.ConfigParser(interpolation=None)
                    after = configparser.ConfigParser(interpolation=None)
                    before.read_string(raw.decode('cp1252'))
                    revised = prep.character_ini(raw, before.get('General', 'Name'), before.get('General', 'ShortName'), before.get('General', 'LongName'))
                    after.read_string(revised.decode('cp1252'))
                    self.assertTrue(before.get('Accessories', 'Hair'))
                    self.assertEqual(after.get('Accessories', 'Hair'), '')
                    before.set('Accessories', 'Hair', '')
                    self.assertEqual({s: dict(before[s]) for s in before.sections()}, {s: dict(after[s]) for s in after.sections()})

    def test_preparation_folders_follow_actual_title_roster_positions(self):
        import prepare_characters
        self.assertEqual(prepare_characters.CHARACTERS['federer']['slot'], 9)
        self.assertEqual(prepare_characters.CHARACTERS['nadal']['slot'], 10)

    def test_score_dds_conversion_preserves_xbox_header_and_color_blocks(self):
        import prepare_characters
        original = bytearray(128)
        original[:4] = b'DDS '
        struct.pack_into('<I', original, 4, 124)
        struct.pack_into('<2I', original, 12, 4, 4)
        original[84:88] = b'DXT3'
        source = bytearray(original)
        source[84:88] = b'DXT5'
        rgb = bytes.fromhex('123456789abcdef0')
        converted = prepare_characters._matching_dds(bytes(original) + bytes(16),
            bytes(source) + bytes((255, 0)) + bytes(6) + rgb)
        self.assertEqual(converted[:128], original)
        self.assertEqual(converted[128:136], bytes([255]) * 8)
        self.assertEqual(converted[136:], rgb)

    def test_skin_weight_nan_sum_and_palette_index_are_rejected(self):
        f = self.require_formats()
        raw = fixture_model(skin=True)
        self.assertEqual(f.parse_model(raw)['vertices'], 3)
        at = raw.index(words(0x123456a1)) + 12 + 32
        for fmt, value, offset in (('<f', float('nan'), 0), ('<f', 1.1, 0), ('<f', -.1, 0), ('<H', 1, 4)):
            with self.subTest(value=value):
                bad = bytearray(raw)
                struct.pack_into(fmt, bad, at + offset, value)
                with self.assertRaises(ValueError):
                    f.parse_model(bytes(bad))
        for fmt in (15, 16):
            raw = fixture_model(skin=fmt)
            self.assertEqual(f.parse_model(raw)['vertices'], 3)
            at = raw.index(words(0x123456a1)) + 12 + 32
            bad = bytearray(raw)
            struct.pack_into('<2f', bad, at, .75, .75)
            with self.assertRaises(ValueError):
                f.parse_model(bytes(bad))

    def test_full_archive_cannot_bypass_ini_dds_or_audio_dependency_rules(self):
        f = self.require_formats()
        original_files = {}
        base, blobs, manifest = {}, {}, {'target': 'TukurXX', 'files': {}}
        for outfit in range(2):
            prefix = 'Media/GamePart/Players/TukurXX%02d/' % outfit
            members = {prefix + 'playerlod00.ooo': fixture_model(),
                       prefix + 'playerlod01.ooo': fixture_model(),
                       prefix + 'textures/tex.pak': fixture_tex(),
                       prefix + 'player.ini': b'[General]\nName=Tukur\nShadow=Shadow.ooo\n',
                       prefix + 'scorename.dds': b'unchanged native DDS',
                       prefix + 'soundscore.pml': b'unchanged native sound',
                       prefix + 'soundevent.pml': b'unchanged native event'}
            archive = 'Media/Wads/Players/TukurXX%02d.wad' % outfit
            base[archive] = fixture_pam(members)
            path = 'outfit%d.wad' % outfit
            blobs[path] = base[archive]
            manifest['files'][archive] = {'path': path}
            original_files[outfit] = members
        f.validate_native_package(manifest, base, blobs)
        prefix = 'Media/GamePart/Players/TukurXX00/'
        for leaf, value in [('player.ini', b'[General]\nName=Federer\nShadow=../../other.ooo\n'),
                            ('scorename.dds', b'changed unknown DDS'),
                            ('soundscore.pml', b'new foreign audio dependencies'),
                            ('soundevent.pml', b'new foreign event dependencies')]:
            with self.subTest(leaf=leaf):
                members = dict(original_files[0])
                members[prefix + leaf] = value
                blobs['outfit0.wad'] = fixture_pam(members)
                with self.assertRaises(ValueError):
                    f.validate_native_package(manifest, base, blobs)

    def test_preparation_rejects_unbound_real_recipe_without_publishing_output(self):
        import prepare_characters
        assets = TOOLS.parents[1] / 'assets'
        research = prepare_characters.DEFAULT_RESEARCH
        if not (assets / 'Media/Wads/GamePart.wad').is_file() or not research.is_dir():
            self.skipTest('private original assets and preserved research are not installed')
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'candidate'
            with self.assertRaises(ValueError):
                prepare_characters.prepare(assets, research, output, characters=('federer',), recipes={'federer': {'schema': 'unbound'}})
            self.assertFalse(output.exists())

    def test_preserved_source_hash_mismatch_fails_before_use(self):
        import prepare_characters
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'fixture.wad'
            source.write_bytes(b'altered')
            inventory = {'legacy/fixture.wad': {'bytes': 7, 'sha256': hashlib.sha256(b'initial').hexdigest()}}
            with self.assertRaises(ValueError):
                prepare_characters.read_preserved(root, 'fixture.wad', inventory)

    def test_preparation_rejects_symlink_parent_and_dotdot_into_preserved_inputs(self):
        import prepare_characters
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            assets, research = root / 'assets', root / 'legacy'
            assets.mkdir()
            research.mkdir()
            alias = root / 'alias'
            alias.symlink_to(research, target_is_directory=True)
            for output in (alias / 'new-candidates', root / 'unused/../assets/new-candidates'):
                with self.subTest(output=output):
                    with self.assertRaises(ValueError):
                        prepare_characters.prepare(assets, research, output)
            self.assertEqual(list(assets.iterdir()), [])
            self.assertEqual(list(research.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
