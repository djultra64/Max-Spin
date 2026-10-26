"""Frozen Windows character exports; independent fixtures. Own work: djultra64."""
import contextlib
import hashlib
import importlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_character_packages as fixtures


def records(folder):
    raw = (folder / 'manifest.bin').read_bytes()
    if raw[:8] != b'MSCHAR1\0':
        raise AssertionError('invalid protocol magic')
    count, = struct.unpack_from('<I', raw, 8)
    at, result = 12, []
    for index in range(count):
        size, = struct.unpack_from('<H', raw, at)
        at += 2
        path = raw[at:at + size].decode('ascii')
        at += size
        length, = struct.unpack_from('<Q', raw, at)
        at += 8
        old, new = raw[at:at + 32], raw[at + 32:at + 64]
        at += 64
        result.append((path, length, old, new, folder / 'files' / f'{index:04d}.wad'))
    if at != len(raw):
        raise AssertionError('trailing manifest bytes')
    return result


class SessionTests(fixtures.FrozenPackageFixture, unittest.TestCase):
    def exporter(self):
        path = fixtures.character_packages.__file__
        self.assertTrue(Path(path).with_name('character_session.py').is_file(),
                        'shared Windows session exporter is missing')
        return importlib.import_module('character_session')

    def export(self, name='session'):
        output = self.root / name
        self.exporter().export_session(self.assets, self.packages, output)
        return output

    def native(self, package, target, pixel=3):
        fixtures.NativePackageIntegrationTests.native_package(self, package, target, pixel)

    def test_actual_native_export_has_bounded_protocol_and_frozen_hashes(self):
        self.validation.stop()
        self.native(self.package, 'TukurXX')
        originals = {p.relative_to(self.assets).as_posix(): p.read_bytes()
                     for p in self.assets.rglob('*') if p.is_file()}
        output = self.export()
        rows = records(output)
        self.assertEqual(len(rows), 2)
        for outfit, (path, length, before, after, frozen) in enumerate(rows):
            self.assertEqual(path, f'Media/Wads/Players/TukurXX{outfit:02d}.wad')
            payload = (self.package / f'outfit{outfit}.wad').read_bytes()
            self.assertEqual(frozen.read_bytes(), payload)
            self.assertEqual(length, len(payload))
            self.assertEqual(before, hashlib.sha256(originals[path]).digest())
            self.assertEqual(after, hashlib.sha256(payload).digest())
        (self.package / 'outfit0.wad').write_bytes(b'changed later')
        self.assertNotEqual(rows[0][4].read_bytes(), b'changed later')
        self.assertEqual({p.relative_to(self.assets).as_posix(): p.read_bytes()
                          for p in self.assets.rglob('*') if p.is_file()}, originals)
        self.assertEqual(sorted(p.name for p in output.iterdir()), ['files', 'manifest.bin'])

    def test_empty_and_invalid_only_selection_exports_zero_entries(self):
        (self.package / 'outfit1.wad').write_bytes(b'invalid')
        with contextlib.redirect_stderr(io.StringIO()):
            output = self.export()
        self.assertEqual((output / 'manifest.bin').read_bytes(), b'MSCHAR1\0\0\0\0\0')
        empty = self.root / 'empty'
        self.exporter().export_session(self.assets, self.root / 'missing', empty)
        self.assertEqual(records(empty), [])

    def test_real_native_manifests_are_discovered_with_upper_and_mixed_case(self):
        self.validation.stop()
        self.native(self.package, 'TukurXX')
        manifest = self.package / 'manifest.json'
        for index, name in enumerate(('MANIFEST.JSON', 'Manifest.Json')):
            with self.subTest(name=name):
                selected = self.package / name
                manifest.rename(selected)
                manifest = selected
                diagnostics = io.StringIO()
                with contextlib.redirect_stderr(diagnostics):
                    rows = records(self.export(f'case-{index}'))
                self.assertEqual(len(rows), 2)
                self.assertEqual(rows[0][4].read_bytes(), (self.package / 'outfit0.wad').read_bytes())
                self.assertEqual(diagnostics.getvalue(), '')

    def test_case_colliding_manifest_names_reject_slot_with_diagnostic(self):
        source = self.package / 'manifest.json'
        contents = source.read_bytes()
        source.rename(self.package / 'MANIFEST.JSON')
        (self.package / 'Manifest.Json').write_bytes(contents)
        matches = [path for path in self.package.iterdir() if path.name.casefold() == 'manifest.json']
        if len(matches) != 2:
            self.skipTest('filesystem does not support distinct case-colliding names')
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics):
            self.assertEqual(records(self.export()), [])
        self.assertIn('case-colliding', diagnostics.getvalue())

    def test_numbered_folders_without_any_manifest_remain_silent(self):
        (self.package / 'manifest.json').unlink()
        (self.packages / '17').mkdir()
        diagnostics = io.StringIO()
        with contextlib.redirect_stderr(diagnostics):
            self.assertEqual(records(self.export()), [])
        self.assertEqual(diagnostics.getvalue(), '')

    def test_real_conflicting_slot_rolls_back_only_that_slot_database_and_members(self):
        import character_formats as formats
        self.validation.stop()
        self.native(self.package, 'TukurXX', 3)
        fixtures.SharedDatabaseTests.bind_database(self, self.package, 'TukurXX', 15, 'Federer')
        fixtures.SharedMemberTests.bind_member(self, self.package, 'TukurXX', 'Federer')
        other = self.make_package(10, 'Houston')
        self.native(other, 'Houston', 7)
        fixtures.SharedDatabaseTests.bind_database(self, other, 'Houston', 16, 'Nadal')
        fixtures.SharedMemberTests.bind_member(self, other, 'Houston', 'Nadal')
        with contextlib.redirect_stderr(io.StringIO()):
            rows = {row[0]: row for row in records(self.export())}
        self.assertEqual(len(rows), 4)
        self.assertFalse(any('Houston' in name for name in rows))
        db = formats.Pml(formats.unpack_zlb(formats.payloads(rows['Media/Wads/Db.wad'][4].read_bytes())['media/db/players(m).pml']))
        self.assertEqual(db.get('TRPlayer/Id=15', 'Name'), 'Federer')
        self.assertEqual(db.get('TRPlayer/Id=16', 'Name'), 'original Nadal slot')
        members = formats.payloads(rows['Media/Wads/GameIni.wad'][4].read_bytes())
        self.assertIn(b'Name = Federer', members['media/gamepart/players/tukurxx00/player.ini'])
        self.assertIn(b'Name = Original', members['media/gamepart/players/houston00/player.ini'])

    def test_two_real_valid_slots_compose_one_shared_database(self):
        import character_formats as formats
        self.validation.stop()
        self.native(self.package, 'TukurXX', 3)
        other = self.make_package(10, 'Houston')
        self.native(other, 'Houston', 3)
        for package, target, identifier, value in ((self.package, 'TukurXX', 15, 'Federer'), (other, 'Houston', 16, 'Nadal')):
            fixtures.SharedDatabaseTests.bind_database(self, package, target, identifier, value)
        rows = {row[0]: row for row in records(self.export())}
        self.assertEqual(len(rows), 5)
        db = formats.Pml(formats.unpack_zlb(formats.payloads(rows['Media/Wads/Db.wad'][4].read_bytes())['media/db/players(m).pml']))
        self.assertEqual(db.get('TRPlayer/Id=15', 'Name'), 'Federer')
        self.assertEqual(db.get('TRPlayer/Id=16', 'Name'), 'Nadal')

    def test_original_digest_describes_bytes_validated_before_source_changes(self):
        archive = 'Media/Wads/Players/TukurXX00.wad'
        before = (self.assets / archive).read_bytes()
        def mutate(*args):
            (self.assets / archive).write_bytes(b'changed after validation')
        with patch('character_packages._native_validate', side_effect=mutate):
            rows = {row[0]: row for row in records(self.export())}
        self.assertEqual(rows[archive][2], hashlib.sha256(before).digest())

    def test_existing_output_and_failed_write_preserve_owned_boundaries(self):
        exporter = self.exporter()
        output = self.root / 'session'
        output.mkdir()
        sentinel = output / 'owned-by-user'
        sentinel.write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            exporter.export_session(self.assets, self.packages, output)
        self.assertEqual(sentinel.read_bytes(), b'keep')
        output = self.root / 'partial'
        original = Path.write_bytes
        def fail(path, data):
            if path.name == 'manifest.bin':
                raise OSError('simulated full disk')
            return original(path, data)
        with patch.object(Path, 'write_bytes', fail):
            with self.assertRaises(OSError):
                exporter.export_session(self.assets, self.packages, output)
        self.assertFalse(output.exists())

    def test_asset_filename_case_is_preserved_for_linux_view_and_export(self):
        archive = 'Media/Wads/Players/TukurXX00.wad'
        renamed = 'Media/Wads/Players/tukurxx00.WAD'
        (self.assets / archive).rename(self.assets / renamed)
        output = self.export()
        self.assertIn(renamed, [row[0] for row in records(output)])
        with fixtures.character_packages.asset_session(self.assets, self.packages) as selected:
            self.assertEqual((selected / renamed).read_bytes(), b'new TukurXX0')

    def test_rejects_portable_invalid_payload_paths_even_on_posix(self):
        manifest = (self.package / 'manifest.json').read_text()
        for name in ('outfit0.wad:stream', 'NUL.wad', 'sub/COM1.txt', 'trailing. /file', 'bad?.wad', 'bad\x01.wad', 'outfit0.wad.', 'ümlaut.wad'):
            with self.subTest(name=name):
                (self.package / 'manifest.json').write_text(manifest)
                self.edit(lambda m: m['files']['Media/Wads/Players/TukurXX00.wad'].update(path=name))
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(records(self.export('invalid-' + str(len(list(self.root.iterdir()))))), [])

    @unittest.skipIf(sys.platform == 'win32', 'case-sensitive collision fixture needs POSIX')
    def test_case_colliding_payload_names_reject_entire_slot(self):
        (self.package / 'OUTFIT0.WAD').write_bytes(b'new TukurXX0')
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(records(self.export()), [])

    def test_oversized_payload_falls_back_before_export(self):
        with patch('character_packages.MAX_FILE', 8), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(records(self.export()), [])

    def test_export_protocol_bounds_fail_and_remove_partial_output(self):
        exporter = self.exporter()
        for replacements, originals in (({f'unknown-{i}': b'x' for i in range(97)}, {}),
                                        ({'Media/Wads/Players/TukurXX00.wad': b'oversized'},
                                         {'Media/Wads/Players/TukurXX00.wad': bytes(32)}),
                                        ({'Media/Wads/Players/Unknown00.wad': b'x'},
                                         {'Media/Wads/Players/Unknown00.wad': bytes(32)})):
            output = self.root / 'bounded'
            with patch.object(exporter, 'collect_replacements', return_value=(replacements, originals)), \
                    patch.object(exporter, 'MAX_FILE', 4), self.assertRaises(ValueError):
                exporter.export_session(self.assets, self.packages, output)
            self.assertFalse(output.exists())

    @unittest.skipIf(sys.platform == 'win32', 'POSIX symlink security regression; Windows has native junction fixtures')
    def test_symlink_original_or_payload_ancestry_rejects_slot(self):
        payload = self.package / 'outfit0.wad'
        external = self.root / 'outside'
        external.mkdir()
        payload.rename(external / 'outfit0.wad')
        (self.package / 'nested').symlink_to(external, target_is_directory=True)
        self.edit(lambda m: m['files']['Media/Wads/Players/TukurXX00.wad'].update(path='nested/outfit0.wad'))
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(records(self.export('symlink-payload')), [])
        self.edit(lambda m: m['files']['Media/Wads/Players/TukurXX00.wad'].update(path='outfit0.wad'))
        (external / 'outfit0.wad').rename(payload)
        source = self.assets / 'Media/Wads/Players/TukurXX00.wad'
        source.rename(external / 'original.wad')
        source.symlink_to(external / 'original.wad')
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(records(self.export('symlink-original')), [])

    def test_cli_supports_unicode_and_spaces_without_packages(self):
        module = self.exporter()
        output = self.root / 'sesión with spaces'
        result = subprocess.run([sys.executable, module.__file__, '--assets', str(self.assets),
                                 '--characters', str(self.root / 'missing'), '--output', str(output)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(records(output), [])


if __name__ == '__main__':
    unittest.main()
