"""CPU behavior of the paths consumed by the native Windows supervisor. djultra64."""
import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

@unittest.skipIf(sys.platform == 'win32', 'native Windows launcher fixture covers portable paths')
class PortablePaths(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        source = Path(cls.build.name) / 'paths.c'
        source.write_text(r"""
#include <wchar.h>
#if __has_include("portable_paths.h")
#include "portable_paths.h"
int resolve(wchar_t*out,unsigned cap,const wchar_t*root,const wchar_t*override,const wchar_t*leaf,int portable){return topspin_portable_path(out,cap,root,override,leaf,portable);}
#else
int resolve(wchar_t*out,unsigned cap,const wchar_t*root,const wchar_t*override,const wchar_t*leaf,int portable){(void)out;(void)cap;(void)root;(void)override;(void)leaf;(void)portable;return 0;}
#endif
""")
        binary = Path(cls.build.name) / 'paths.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-I',str(ROOT / 'windows/native/src'),str(source),'-o',str(binary)],check=True)
        cls.lib = ctypes.CDLL(str(binary))
        cls.lib.resolve.argtypes = [ctypes.c_wchar_p,ctypes.c_uint,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_int]

    def resolve(self, root='D:/Juegos/Max Spin ñ', override=None, leaf='Saves', portable=True, cap=1024):
        out = ctypes.create_unicode_buffer(cap)
        ok = self.lib.resolve(out, cap, root, override, leaf, portable)
        return ok, out.value

    def test_all_defaults_follow_relocated_install(self):
        for root in ('D:/Juegos/Max Spin ñ','E:/Moved Max Spin'):
            for leaf in ('assets','native-linux/cache','Saves','Settings/graphics.ini','Cache/Sessions','characters'):
                with self.subTest(root=root,leaf=leaf):
                    self.assertEqual(self.resolve(root=root,leaf=leaf),(1,root+'/'+leaf))

    def test_portable_overrides_cannot_escape_install(self):
        for override in ('C:/Users/user/AppData/Saves','D:/Juegos/Max Spin ñ-other/Saves','../Saves','D:/Juegos/Max Spin ñ/../Saves','D:/Juegos/Max Spin ñ/Cache/../../Saves','//server/share/Saves','D:relative','D:/Juegos/Max Spin ñ/Saves:stream','D:/Juegos/Max Spin ñ/Saves.','D:/Juegos/Max Spin ñ/Saves /file','D:/Juegos/Max Spin ñ/CON'):
            with self.subTest(override=override):
                self.assertEqual(self.resolve(override=override)[0],0)

    def test_portable_override_accepts_case_and_separator_variants(self):
        self.assertEqual(self.resolve(override=r'd:\JUEGOS\Max Spin ñ\Saves 2'),(1,'d:/JUEGOS/Max Spin ñ/Saves 2'))

    def test_developer_override_is_explicit_and_absolute(self):
        self.assertEqual(self.resolve(override='C:/Private/Saves',portable=False),(1,'C:/Private/Saves'))
        self.assertEqual(self.resolve(override='relative',portable=False)[0],0)

    def test_incomplete_root_or_short_buffer_fail_without_partial_result(self):
        self.assertEqual(self.resolve(root='relative')[0],0)
        self.assertEqual(self.resolve(cap=8),(0,''))
