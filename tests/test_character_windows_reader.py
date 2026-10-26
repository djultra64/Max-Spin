"""Native Win32 bounded-reader CPU fixtures. Own work: djultra64."""
import importlib
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'native-linux/tools'))
import character_packages


class PortablePathTests(unittest.TestCase):
    def test_unsafe_windows_names_are_rejected_independently_of_host_filesystem(self):
        for name in ('outfit.wad:stream', 'NUL.wad', 'sub/COM1.txt', 'LPT9', 'aux',
                     'CONIN$', 'CONOUT$.txt', 'CLOCK$', 'CON .txt',
                     'bad?.wad', 'a<b', 'name.', 'name ', 'a\x1fb', 'ümlaut.wad',
                     'a\\b', '../file', '/absolute', 'a//b'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                character_packages._relative(name)

    def test_routes_are_limited_to_existing_native_athletes_and_shared_outputs(self):
        for name in ('TopSpin.xbe', 'Media/Wads/Players/Unknown00.wad', 'Media/Wads/GamePart.wad',
                     'Media/Wads/Players/TukurXX02.wad', 'U/save.wad'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                character_packages.resource_path(name)

    def test_native_reader_module_is_available_for_the_windows_distribution(self):
        path = Path(character_packages.__file__).with_name('character_windows_reader.py')
        self.assertTrue(path.is_file(), 'secure native Windows package reader is missing')
        if sys.platform != 'win32':
            reader = importlib.import_module('character_windows_reader')
            with self.assertRaises(OSError):
                reader.read_bounded('.', 'manifest.json', 1024)


@unittest.skipUnless(sys.platform == 'win32', 'requires actual native Win32 filesystem handles')
class WindowsReaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'package'
        (self.root / 'nested').mkdir(parents=True)
        (self.root / 'nested/payload.wad').write_bytes(b'bounded payload')
        self.reader = importlib.import_module('character_windows_reader')

    def test_reads_regular_unicode_root_with_case_insensitive_relative_lookup(self):
        renamed = self.root.with_name('paquete con ñ')
        self.root.rename(renamed)
        data, actual = self.reader.read_bounded(renamed, 'NESTED/PAYLOAD.WAD', 15)
        self.assertEqual(data, b'bounded payload')
        self.assertEqual(actual, 'nested/payload.wad')

    def test_refuses_oversized_files_and_directories(self):
        with self.assertRaises(ValueError):
            self.reader.read_bounded(self.root, 'nested/payload.wad', 14)
        with self.assertRaises(ValueError):
            self.reader.read_bounded(self.root, 'nested', 100)
        with (self.root / 'huge.wad').open('wb') as stream:
            stream.truncate(128 * 1024 * 1024 + 1)
        with self.assertRaises(ValueError):
            self.reader.read_bounded(self.root, 'huge.wad', 128 * 1024 * 1024)

    def test_junction_is_refused_at_root_ancestor_or_payload_component(self):
        junction = Path(self.temp.name) / 'junction'
        result = subprocess.run(['cmd', '/d', '/c', 'mklink', '/J', str(junction), str(self.root)],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.addCleanup(lambda: os.rmdir(junction) if junction.exists() else None)
        for root, relative in ((junction, 'nested/payload.wad'),
                               (junction / 'nested', 'payload.wad'),
                               (Path(self.temp.name), 'junction/nested/payload.wad')):
            with self.subTest(root=root), self.assertRaises(ValueError):
                self.reader.read_bounded(root, relative, 100)

    def test_file_symlink_is_refused_when_available(self):
        link = self.root / 'linked.wad'
        try:
            link.symlink_to(self.root / 'nested/payload.wad')
        except OSError:
            self.skipTest('Windows developer mode/symlink permission unavailable; junction test remains required')
        with self.assertRaises(ValueError):
            self.reader.read_bounded(self.root, 'linked.wad', 100)

    def test_directory_ancestry_is_pinned_until_payload_read_finishes(self):
        original = self.reader._read_handle
        def read(handle, limit):
            for path in (self.root, self.root / 'nested'):
                with self.assertRaises(OSError):
                    path.rename(path.with_name(path.name + '-moved'))
                # In-place junction/reparse mutation needs a writable directory
                # handle; deny that as well as rename/delete while checked.
                handle = self.reader._kernel.CreateFileW(str(path), 0x40000000, 7,
                                                         None, 3, 0x02000000, None)
                try:
                    self.assertEqual(handle, self.reader._INVALID_HANDLE)
                finally:
                    if handle != self.reader._INVALID_HANDLE:
                        self.reader._kernel.CloseHandle(handle)
            with self.assertRaises(OSError):
                (self.root / 'nested/payload.wad').open('wb')
            return original(handle, limit)
        with patch.object(self.reader, '_read_handle', side_effect=read):
            self.assertEqual(self.reader.read_bounded(self.root, 'nested/payload.wad', 100)[0], b'bounded payload')
        # Handles must be closed afterwards, including every directory ancestor.
        moved = self.root.with_name('moved')
        self.root.rename(moved)
        self.root = moved


if __name__ == '__main__':
    unittest.main()
