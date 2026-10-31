"""Portable bundle release gates. Own work: djultra64."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('portable_pack',ROOT/'tools/pack_release.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)


class Bundle(unittest.TestCase):
    def test_missing_or_incomplete_frozen_runtime_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);out=root/'out';out.mkdir()
            with self.assertRaisesRegex(ValueError,'runtime'): pack.copy_portable_runtime(root/'missing',out,'linux')
            runtime=root/'runtime';runtime.mkdir();(runtime/'MaxSpin-Setup').write_bytes(b'\x7fELFfixture')
            with self.assertRaisesRegex(ValueError,'runtime'): pack.copy_portable_runtime(runtime,out,'linux')

    def test_bundle_copies_embedded_interpreter_and_records_no_external_requirement(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);runtime=root/'runtime';runtime.mkdir();out=root/'out';out.mkdir()
            (runtime/'MaxSpin-Setup').write_bytes(b'\x7fELFfixture')
            (runtime/'_internal').mkdir();(runtime/'_internal/libpython3.so').write_bytes(b'library')
            (runtime/'PORTABLE_RUNTIME.json').write_text(json.dumps({'schema':1,'platform':'linux','format':'onedir','self_check':True}))
            pack.copy_portable_runtime(runtime,out,'linux')
            self.assertEqual((out/'portable-runtime/_internal/libpython3.so').read_bytes(),b'library')

    def test_linux_non_system_dependencies_are_bundled(self):
        # A real private shared library absent from a target host must be copied.
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'dep.c').write_text('int maxspin_dep(void){return 7;}')
            (root/'main.c').write_text('extern int maxspin_dep(void);int main(void){return maxspin_dep();}')
            subprocess.run(['cc','-shared','-fPIC',str(root/'dep.c'),'-Wl,-soname,libmaxspin_test.so','-o',str(root/'libmaxspin_test.so')],check=True)
            subprocess.run(['cc',str(root/'main.c'),'-L'+str(root),'-lmaxspin_test','-Wl,-rpath,'+str(root),'-o',str(root/'worker')],check=True)
            out=root/'out';out.mkdir()
            record=pack.collect_linux_libraries([root/'worker'],out)
            self.assertIn('libmaxspin_test.so',record)
            self.assertEqual((out/'libmaxspin_test.so').read_bytes(),(root/'libmaxspin_test.so').read_bytes())


if __name__=='__main__':unittest.main()
