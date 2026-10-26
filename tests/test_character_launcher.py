"""Real supervisor prep processes/files, without game/window/GPU. djultra64."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'windows/native/src'


@unittest.skipIf(sys.platform == 'win32', 'native Windows fixture covers the Win32 adapter')
class LauncherContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        binary = Path(cls.build.name) / 'launcher.so'
        production = SRC / 'character_launcher.c'
        sources = [str(ROOT / 'tests/character_launcher_posix.c')]
        if production.exists():
            sources.append(str(production))
        else:
            stub = Path(cls.build.name) / 'absent.c'
            stub.write_text('#include <wchar.h>\nint topspin_windows_characters_begin(const wchar_t*a,const wchar_t*b,const wchar_t*c,const wchar_t*d,wchar_t*e,unsigned f){return 0;}\nvoid topspin_windows_characters_end(const wchar_t*a){}\n')
            sources.append(str(stub))
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared', '-I', str(SRC), *sources, '-o', str(binary)], check=True)
        cls.lib = ctypes.CDLL(str(binary))
        cls.lib.test_character_locale()
        cls.lib.topspin_windows_characters_begin.argtypes = [ctypes.c_wchar_p] * 5 + [ctypes.c_uint]
        cls.lib.topspin_windows_characters_end.argtypes = [ctypes.c_wchar_p]

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='MaxSpin parity ñ ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'assets con espacios'
        self.packages = self.root / 'characters'
        self.session = self.root / 'session'
        for folder in (self.assets, self.packages, self.session):
            folder.mkdir()
        self.message = ctypes.create_unicode_buffer(2048)
        self.environment = {name: os.environ.get(name) for name in ('MAX_SPIN_PYTHON', 'MAX_SPIN_CHARACTER_SESSION', 'MAX_SPIN_CHARACTER_MANIFEST_SHA256', 'MAX_SPIN_CHARACTER_ASSETS')}
        self.addCleanup(self.restore_environment)
        self.addCleanup(self.lib.topspin_windows_characters_end, str(self.session))
        os.environ['MAX_SPIN_PYTHON'] = str(Path(sys.executable).resolve())

    def restore_environment(self):
        for key, value in self.environment.items():
            if value is None:
                os.unsetenv(key)
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    def begin(self, install=None):
        return self.lib.topspin_windows_characters_begin(*map(str, (install or self.root, self.assets, self.packages, self.session)), self.message, len(self.message))

    def env(self, key):
        libc = ctypes.CDLL(None)
        libc.getenv.argtypes = [ctypes.c_char_p]
        libc.getenv.restype = ctypes.c_char_p
        return libc.getenv(key.encode())

    def helper(self, code=None):
        package = self.packages / '09'
        package.mkdir()
        (package / 'manifest.json').write_text('{}')
        helper = self.root / 'native-linux/tools/character_session.py'
        helper.parent.mkdir(parents=True)
        helper.write_text(code or '''import os,sys,struct
from pathlib import Path
assert sys.argv[1] == '--assets' and sys.argv[3] == '--characters' and sys.argv[5] == '--output'
assert not any(os.environ.get(k) for k in ('MAX_SPIN_CHARACTER_SESSION','MAX_SPIN_CHARACTER_MANIFEST_SHA256','MAX_SPIN_CHARACTER_ASSETS'))
p=Path(sys.argv[6]);p.mkdir();(p/'files').mkdir()
(p/'manifest.bin').write_bytes(b'MSCHAR1\\0'+struct.pack('<I',0))
print('Character slot 9: keeping defaults.',file=sys.stderr)
''')

    def assert_clear(self):
        for key in ('MAX_SPIN_CHARACTER_SESSION', 'MAX_SPIN_CHARACTER_MANIFEST_SHA256', 'MAX_SPIN_CHARACTER_ASSETS'):
            self.assertIsNone(self.env(key))

    def test_empty_selection_scrubs_inherited_state_without_python_or_copies(self):
        for key in ('MAX_SPIN_CHARACTER_SESSION', 'MAX_SPIN_CHARACTER_MANIFEST_SHA256', 'MAX_SPIN_CHARACTER_ASSETS'):
            os.environ[key] = 'stale'
        os.environ['MAX_SPIN_PYTHON'] = '/missing/python'
        self.assertEqual(self.begin(), 1, self.message.value)
        self.assert_clear()
        self.assertEqual(list(self.session.iterdir()), [])

    def test_real_helper_unicode_spaces_and_environment_survive_worker_handoffs(self):
        self.helper()
        self.assertEqual(self.begin(), 1, self.message.value)
        self.assertIn('keeping defaults', self.message.value)
        self.assertEqual(self.env('MAX_SPIN_CHARACTER_SESSION').decode(), str(self.session / 'characters'))
        self.assertEqual(self.env('MAX_SPIN_CHARACTER_ASSETS').decode(), str(self.assets))
        self.assertEqual(self.env('MAX_SPIN_CHARACTER_MANIFEST_SHA256'), b'0' * 64)
        script = "import os,json; print(json.dumps([os.environ[k] for k in ('MAX_SPIN_CHARACTER_SESSION','MAX_SPIN_CHARACTER_MANIFEST_SHA256','MAX_SPIN_CHARACTER_ASSETS')]))"
        first = subprocess.check_output([sys.executable, '-c', script])
        for _ in range(9):
            self.assertEqual(subprocess.check_output([sys.executable, '-c', script]), first)
        self.lib.topspin_windows_characters_end(str(self.session))
        self.assert_clear()
        self.assertEqual(list(self.session.iterdir()), [])

    def test_missing_interpreter_fails_before_export(self):
        self.helper()
        os.environ['MAX_SPIN_PYTHON'] = '/missing/python'
        self.assertEqual(self.begin(), 0)
        self.assertTrue(self.message.value)
        self.assert_clear()
        self.assertEqual(list(self.session.iterdir()), [])

    def test_real_shared_exporter_keeps_invalid_slot_defaults_and_originals(self):
        self.helper()
        original = self.assets / 'original.bin'
        original.write_bytes(b'original bytes')
        self.assertEqual(self.begin(ROOT), 1, self.message.value)
        self.assertIn('keeping defaults', self.message.value)
        self.assertEqual((self.session / 'characters/manifest.bin').read_bytes(), b'MSCHAR1\0' + b'\0' * 4)
        self.assertEqual(original.read_bytes(), b'original bytes')

    def test_noisy_failed_helper_is_drained_with_bounded_diagnostic(self):
        self.helper("import sys\nprint('failure start: '+('x'*200000))\nsys.exit(9)\n")
        self.assertEqual(self.begin(), 0)
        self.assertTrue(self.message.value.startswith('failure start: '))
        self.assertLessEqual(len(self.message.value), 2047)
        self.assert_clear()

    def test_helper_failure_is_reported_and_owned_partial_export_removed(self):
        self.helper("import sys\nfrom pathlib import Path\np=Path(sys.argv[6]);p.mkdir();(p/'files').mkdir();(p/'files/0000.wad').write_bytes(b'partial')\nprint('bad original hash',file=sys.stderr)\nsys.exit(7)\n")
        self.assertEqual(self.begin(), 0)
        self.assertIn('bad original hash', self.message.value)
        self.assert_clear()
        self.assertEqual(list(self.session.iterdir()), [])

    def test_invalid_manifest_fails_activation_and_cleans_up(self):
        self.helper("import sys\nfrom pathlib import Path\np=Path(sys.argv[6]);p.mkdir();(p/'files').mkdir();(p/'manifest.bin').write_bytes(b'bad')\n")
        self.assertEqual(self.begin(), 0)
        self.assertTrue(self.message.value)
        self.assert_clear()
        self.assertEqual(list(self.session.iterdir()), [])

    def test_cleanup_retains_unknown_files_and_never_touches_other_session(self):
        self.helper()
        self.assertEqual(self.begin(), 1, self.message.value)
        output = self.session / 'characters'
        (output / 'unowned.txt').write_text('preserve')
        (output / 'files/0096.wad').write_text('outside protocol')
        foreign = self.root / 'foreign'
        (foreign / 'characters').mkdir(parents=True)
        (foreign / 'characters/manifest.bin').write_text('preserve')
        self.lib.topspin_windows_characters_end(str(foreign))
        self.assertTrue((foreign / 'characters/manifest.bin').exists())
        self.lib.topspin_windows_characters_end(str(self.session))
        self.assertEqual((output / 'unowned.txt').read_text(), 'preserve')
        self.assertEqual((output / 'files/0096.wad').read_text(), 'outside protocol')
        self.assertFalse((output / 'manifest.bin').exists())

    def test_preexisting_output_is_preserved(self):
        self.helper()
        output = self.session / 'characters'
        output.mkdir()
        (output / 'manifest.bin').write_bytes(b'preexisting')
        self.assertEqual(self.begin(), 0)
        self.assertEqual((output / 'manifest.bin').read_bytes(), b'preexisting')

    def test_cleanup_does_not_follow_reparse_like_files_directory(self):
        self.helper()
        self.assertEqual(self.begin(), 1, self.message.value)
        output = self.session / 'characters'
        (output / 'files').rmdir()
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / '0000.wad').write_text('preserve')
        (output / 'files').symlink_to(outside, target_is_directory=True)
        self.lib.topspin_windows_characters_end(str(self.session))
        self.assertEqual((outside / '0000.wad').read_text(), 'preserve')
