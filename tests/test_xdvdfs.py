"""Real synthetic XDVDFS images; own work: djultra64."""
import importlib
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'


def record(name, sector=40, size=5, *, directory=False, left=0, right=0):
    if isinstance(name, str):
        name = name.encode('latin-1')
    data = struct.pack('<HHIIBB', left, right, sector, size, 0x10 if directory else 0x20, len(name)) + name
    return data + b'\xff' * (-len(data) % 4)


def image(path, table=None, *, base=0, table_size=None, root_sector=34,
          footer=True, blocks=None):
    if table is None:
        table = record('default.xbe')
    descriptor = bytearray(SECTOR)
    descriptor[:20] = MAGIC
    struct.pack_into('<II', descriptor, 20, root_sector, len(table) if table_size is None else table_size)
    if footer:
        descriptor[-20:] = MAGIC
    with path.open('wb') as output:
        output.seek(base + 0x10000)
        output.write(descriptor)
        output.seek(base + root_sector * SECTOR)
        output.write(table)
        for sector, data in (blocks or {40: b'hello'}).items():
            output.seek(base + sector * SECTOR)
            output.write(data)
    return path


class XboxISOTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        try:
            self.module = importlib.import_module('portable.xdvdfs')
        except ImportError:
            self.fail('shared Xbox ISO reader is missing')

    def open(self, *args, **kwargs):
        return self.module.XboxISO(image(self.root / 'game.iso', *args, **kwargs))

    def test_reads_and_extracts_original_bytes_with_progress(self):
        table = record('Media', 35, 24, directory=True, right=6) + record('default.xbe')
        # Media record is 20 bytes, so its sibling pointer is 5 dwords.
        table = bytearray(table)
        struct.pack_into('<H', table, 2, 5)
        with self.open(bytes(table), blocks={35: record('data.wad', 41, 3), 40: b'hello', 41: b'wad'}) as iso:
            self.assertEqual({(str(e.path), e.size, e.is_dir) for e in iso.entries},
                             {('Media', 24, True), ('Media/data.wad', 3, False), ('default.xbe', 5, False)})
            self.assertTrue(all(isinstance(e.path, PurePosixPath) for e in iso.entries))
            self.assertEqual(iso.total_bytes, 8)
            entry = next(e for e in iso.entries if str(e.path) == 'default.xbe')
            self.assertEqual(iso.read(entry), b'hello')
            self.assertEqual(iso.read(entry, limit=2), b'he')
            updates = []
            destination = self.root / 'output'
            iso.extract(destination, progress=lambda *args: updates.append(args))
            self.assertEqual((destination / 'default.xbe').read_bytes(), b'hello')
            self.assertEqual((destination / 'Media/data.wad').read_bytes(), b'wad')
            self.assertEqual(updates[-1][:2], (8, 8))
        self.assertEqual((self.root / 'game.iso').read_bytes()[40 * SECTOR:40 * SECTOR + 5], b'hello')

    def test_full_original_xbox_dump_partition_offset(self):
        with self.open(base=0x18300000) as iso:
            self.assertEqual(iso.read(iso.entries[0]), b'hello')

    def test_empty_directory_tables_and_empty_files(self):
        with self.open(record('empty', 35, 0, directory=True, right=5) + record('zero', 0, 0)) as iso:
            iso.extract(self.root / 'output')
            self.assertTrue((self.root / 'output/empty').is_dir())
            self.assertEqual((self.root / 'output/zero').read_bytes(), b'')
            self.assertEqual(iso.total_bytes, 0)
        with self.open(b'\xff' * SECTOR) as iso:
            self.assertEqual(list(iso.entries), [])

    def test_padding_pointer_advances_to_next_sector(self):
        table = record('first', right=8).ljust(SECTOR, b'\xff') + record('last', 41, 3)
        with self.open(table, blocks={40: b'hello', 41: b'end'}) as iso:
            self.assertEqual([str(e.path) for e in iso.entries], ['first', 'last'])

    def test_requires_both_descriptor_signatures(self):
        with self.assertRaises(ValueError):
            self.open(footer=False)
        path = self.root / 'not.iso'
        path.write_bytes(b'not an Xbox ISO')
        with self.assertRaises(ValueError):
            self.module.XboxISO(path)

    def test_rejects_truncated_extent_before_creating_output(self):
        with self.assertRaises(ValueError):
            self.open(record('bad', 99, 10))
        with self.assertRaises(ValueError):
            self.open(table_size=2**32 - 1)
        with self.assertRaises(ValueError):
            self.open(record('bad')[:15])
        self.assertFalse((self.root / 'output').exists())

    def test_rejects_invalid_tree_pointers_cycles_and_overlapping_records(self):
        cases = [record('bad', left=65534),
                 record('one', right=5) + record('two', right=5),
                 record('overlapping', right=1),
                 record('bad', right=8).ljust(40, b'\xff')]
        for table in cases:
            with self.subTest(table=table), self.assertRaises(ValueError):
                self.open(table)

    def test_rejects_directory_cycles_and_extent_aliases(self):
        with self.assertRaises(ValueError):
            self.open(record('loop', 34, 20, directory=True))
        with self.assertRaises(ValueError):
            self.open(record('one', 35, 20, directory=True, right=5) + record('two', 35, 20, directory=True),
                      blocks={35: record('file', 40, 5), 40: b'hello'})

    def test_rejects_unsafe_windows_names_on_every_platform(self):
        names = ['..', '.', '../escape', 'a/b', r'a\b', 'C:evil', 'NUL', 'nul.txt',
                 'COM1', 'LPT9.log', 'COM\xb9.txt', 'CONOUT$', 'a.', 'a ', 'a*',
                 'a?', 'a<', 'a>', 'a|', 'a"', 'a\0b', 'a\x01b', 'FOOBAR~1.TXT', '']
        for name in names:
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.open(record(name))

    def test_rejects_casefold_aliases_and_duplicate_paths(self):
        for second in ['SAVE', 'save']:
            with self.subTest(second=second), self.assertRaises(ValueError):
                self.open(record('save', right=5) + record(second))

    def test_refuses_existing_destination_without_overwrite(self):
        destination = self.root / 'output'
        destination.mkdir()
        (destination / 'default.xbe').write_bytes(b'original')
        with self.open() as iso, self.assertRaises(FileExistsError):
            iso.extract(destination)
        self.assertEqual((destination / 'default.xbe').read_bytes(), b'original')

    @unittest.skipUnless(hasattr(os, 'symlink'), 'requires symlinks')
    def test_refuses_symlink_destination_and_ancestor(self):
        outside = self.root / 'outside'
        outside.mkdir()
        link = self.root / 'link'
        try:
            link.symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest('symlinks unavailable for current account')
        with self.open() as iso:
            for path in [link, link / 'output']:
                with self.subTest(path=path), self.assertRaises((ValueError, FileExistsError)):
                    iso.extract(path)
        self.assertEqual(list(outside.iterdir()), [])

    def test_cancellation_stops_before_creating_files(self):
        with self.open() as iso, self.assertRaises(InterruptedError):
            iso.extract(self.root / 'output', cancelled=lambda: True)
        self.assertFalse((self.root / 'output/default.xbe').exists())

    def test_entry_from_other_image_is_not_accepted(self):
        with self.open() as first:
            with self.module.XboxISO(self.root / 'game.iso') as second:
                with self.assertRaises(ValueError):
                    second.read(first.entries[0])

    def test_rejects_overlapping_file_and_descriptor_extents(self):
        for table in [record('one', right=5) + record('two', 40, 5),
                      record('descriptor', 32, 5)]:
            with self.subTest(table=table), self.assertRaises(ValueError):
                self.open(table)

    def test_limits_entry_count_and_cumulative_table_memory(self):
        with patch.object(self.module, 'MAX_ENTRIES', 1):
            with self.assertRaises(ValueError):
                self.open(record('one', right=5) + record('two', 41, 0))
        with patch.object(self.module, 'MAX_TOTAL_TABLE_BYTES', 39):
            with self.assertRaises(ValueError):
                self.open(record('dir', 35, 20, directory=True),
                          blocks={35: record('file'), 40: b'hello'})

    def test_rejects_excessive_directory_depth_without_recursion(self):
        blocks = {sector: record('d', sector + 1, 16, directory=True)
                  for sector in range(35, 100)}
        blocks[100] = record('end', 120, 0)
        with self.assertRaises(ValueError):
            self.open(record('d', 35, 16, directory=True), blocks=blocks)

    def test_cancels_between_chunks_and_preserves_existing_files(self):
        payload = b'x' * (2 * 1024 * 1024 + 1)
        stopped = []
        with self.open(record('large', 40, len(payload)), blocks={40: payload}) as iso:
            with self.assertRaises(InterruptedError):
                iso.extract(self.root / 'output',
                            progress=lambda *args: stopped.append(args),
                            cancelled=lambda: bool(stopped))
        copied = (self.root / 'output/large').stat().st_size
        self.assertGreater(copied, 0)
        self.assertLess(copied, len(payload))

    def test_detects_iso_truncation_during_extraction(self):
        with self.open() as iso:
            with (self.root / 'game.iso').open('r+b') as output:
                output.truncate(SECTOR)
            with self.assertRaises(ValueError):
                iso.extract(self.root / 'output')

    def test_windows_path_extraction_behavior_on_host(self):
        with self.open() as iso:
            iso._extract_paths(self.root / 'output', None, None)
            self.assertEqual((self.root / 'output/default.xbe').read_bytes(), b'hello')
            with self.assertRaises(FileExistsError):
                iso._extract_paths(self.root / 'output', None, None)

    @unittest.skipUnless(hasattr(os, 'symlink'), 'requires symlinks')
    def test_rejects_link_injected_by_progress_callback(self):
        outside = self.root / 'outside'
        outside.mkdir()
        table = record('dir', 35, 20, directory=True, right=5) + record('first')
        with self.open(table, blocks={35: record('last', 41, 3), 40: b'hello', 41: b'end'}) as iso:
            for method in ['_extract_paths', '_extract_posix']:
                if method == '_extract_posix' and os.name != 'posix':
                    continue
                destination = self.root / method

                def inject(*_):
                    directory = destination / 'dir'
                    if directory.is_symlink():
                        return
                    directory.rmdir()
                    try:
                        directory.symlink_to(outside, target_is_directory=True)
                    except OSError:
                        self.skipTest('symlinks unavailable for current account')

                with self.subTest(method=method), self.assertRaises((ValueError, OSError)):
                    getattr(iso, method)(destination, inject, None)
                self.assertFalse((outside / 'last').exists())

    @unittest.skipUnless(os.name == 'nt', 'requires Windows junction support')
    def test_refuses_windows_junction_ancestor(self):
        import subprocess
        outside = self.root / 'outside'
        outside.mkdir()
        junction = self.root / 'junction'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(outside)],
                                capture_output=True)
        if result.returncode:
            self.skipTest('junction creation unavailable for current account')
        try:
            with self.open() as iso, self.assertRaises(ValueError):
                iso.extract(junction / 'output')
            self.assertEqual(list(outside.iterdir()), [])
        finally:
            junction.rmdir()


if __name__ == '__main__':
    unittest.main()
