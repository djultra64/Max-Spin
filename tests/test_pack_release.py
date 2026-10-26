"""Synthetic local packaging contracts. Own work: djultra64."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('pack_release', Path(__file__).resolve().parents[1]/'tools/pack_release.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class Contracts(unittest.TestCase):
    def test_source_excludes_game_state_but_retains_translator(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source';out=root/'out';source.mkdir();out.mkdir()
            files=('vendor/xboxrecomp/tools/recomp/translator.py',
                   'native-linux/src/recomp/menu/generated.c', 'assets/default.xbe',
                   'native-linux/cache/original.xpr', 'build/private.rsp',
                   'native-linux/assets/controller-prompts-xelu/art.png')
            for name in files:
                path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            pack.copy_source(source,out)
            self.assertEqual(sorted(str(p.relative_to(out))for p in out.rglob('*')if p.is_file()),
                             ['native-linux/assets/controller-prompts-xelu/art.png','vendor/xboxrecomp/tools/recomp/translator.py'])

    def test_windows_path_mapping_and_response_expansion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'CMakeFiles').mkdir();(root/'CMakeFiles/a.obj').write_bytes(b'object')
            (root/'runtime').mkdir();(root/'runtime/libxbox_apu.a').write_bytes(b'archive')
            (root/'link.rsp').write_text('"C:\\private\\build\\CMakeFiles\\a.obj" runtime/libxbox_apu.a -lkernel32')
            paths=pack.Paths([('C:/private/build',root)])
            out=root/'relink';out.mkdir()
            collector=pack.Relink(out,paths,['C:/private/build'])
            name,record=collector.command(dict(directory='C:/private/build',arguments=[
                'C:/toolchain/clang.exe','@link.rsp','-o','title/topspin_game.exe',
                '-Wl,--out-implib,title/libtopspin_game.dll.a','-Wl,--dependency-file=CMakeFiles/private/link.d']), 'windows')
            self.assertEqual(name,'topspin_game.exe')
            self.assertNotIn('C:/private',json.dumps(record))
            self.assertFalse(any('dependency-file'in value for value in record['arguments']))
            self.assertEqual(len(collector.artifacts),2)
            self.assertTrue(any(v['runtime_archive']for v in collector.artifacts.values()))

    def test_private_debug_path_refused(self):
        with tempfile.TemporaryDirectory()as temporary:
            path=Path(temporary)/'object.o';path.write_bytes(b'fixture /home/private/build/source.c')
            with self.assertRaisesRegex(ValueError,'private path embedded'):
                pack.ensure_no_private_paths(path,['/home/private/build'])

    def test_pe_normal_import(self):
        with tempfile.TemporaryDirectory()as temporary:
            data=bytearray(1024);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,0x80)
            data[0x80:0x84]=b'PE\0\0';struct.pack_into('<H',data,0x86,1);struct.pack_into('<H',data,0x94,240)
            optional=0x98;struct.pack_into('<H',data,optional,0x20b);struct.pack_into('<Q',data,optional+24,0x140000000)
            struct.pack_into('<I',data,optional+108,16);struct.pack_into('<2I',data,optional+112+8,0x1000,40)
            section=optional+240;struct.pack_into('<4I',data,section+8,512,0x1000,512,512)
            struct.pack_into('<5I',data,512,0,0,0,0x1080,0);data[640:653]=b'KERNEL32.dll\0'
            path=Path(temporary)/'synthetic.exe';path.write_bytes(data)
            self.assertEqual(pack.pe_imports(path),['KERNEL32.dll'])

    def test_recursive_dll_closure_and_missing_dependency(self):
        def image(name):
            data=bytearray(1024);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,0x80)
            data[0x80:0x84]=b'PE\0\0';struct.pack_into('<H',data,0x86,1);struct.pack_into('<H',data,0x94,240)
            optional=0x98;struct.pack_into('<H',data,optional,0x20b);struct.pack_into('<Q',data,optional+24,0x140000000)
            struct.pack_into('<I',data,optional+108,16);struct.pack_into('<2I',data,optional+120,0x1000,40)
            struct.pack_into('<4I',data,optional+248,512,0x1000,512,512)
            struct.pack_into('<5I',data,512,0,0,0,0x1080,0)
            raw=name.encode()+b'\0';data[640:640+len(raw)]=raw
            return data
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);search=root/'dlls';search.mkdir();out=root/'package';out.mkdir()
            executable=root/'owner.exe';executable.write_bytes(image('First.dll'))
            (search/'First.dll').write_bytes(image('Second.dll'))
            with self.assertRaisesRegex(ValueError,'missing non-system DLL: Second.dll'):
                pack.collect_dlls([executable],[search],out)
            (search/'Second.dll').write_bytes(image('KERNEL32.dll'))
            result=pack.collect_dlls([executable],[search],out)
            self.assertEqual(set(result),{'First.dll','Second.dll'})

    @unittest.skipUnless(shutil.which('cc') and shutil.which('ar'), 'requires native C/archive tools')
    def test_actual_native_runtime_archive_replacement(self):
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);build=root/'build';build.mkdir();relink=root/'relink';relink.mkdir()
            (build/'module.c').write_text('#include <stdio.h>\nextern int value(void);int main(void){printf("%d\\n",value());return 0;}\n')
            (build/'runtime.c').write_text('int value(void){return 7;}\n')
            subprocess.run(['cc','-c','module.c','-o','module.o'],cwd=build,check=True)
            subprocess.run(['cc','-c','runtime.c','-o','runtime.o'],cwd=build,check=True)
            subprocess.run(['ar','rcs','libxbox_apu.a','runtime.o'],cwd=build,check=True)
            collector=pack.Relink(relink,pack.Paths([]),[str(build)])
            name,record=collector.command(dict(directory=str(build),arguments=['cc','module.o','libxbox_apu.a','-o','topspin_game']), 'linux')
            (relink/'targets.json').write_text(json.dumps(dict(targets={name:record},artifacts=collector.artifacts)))
            (relink/'relink.py').write_text(pack.RELINK_RUNNER)
            subprocess.run([sys.executable,str(relink/'relink.py'),name],check=True)
            self.assertEqual(subprocess.check_output([str(root/'bin/topspin_game')],text=True).strip(),'7')
            replacement=root/'rebuilt';replacement.mkdir();(replacement/'runtime.c').write_text('int value(void){return 42;}\n')
            subprocess.run(['cc','-c','runtime.c','-o','runtime.o'],cwd=replacement,check=True)
            subprocess.run(['ar','rcs','libxbox_apu.a','runtime.o'],cwd=replacement,check=True)
            subprocess.run([sys.executable,str(relink/'relink.py'),name,'--runtime-build',str(replacement)],check=True)
            self.assertEqual(subprocess.check_output([str(root/'bin/topspin_game')],text=True).strip(),'42')

    def test_complete_linux_manifest_and_exclusive_output(self):
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);source=root/'source';source.mkdir();build=root/'build';build.mkdir()
            for name in ('LICENSE','THIRD_PARTY.md','AI_DISCLOSURE.md','vendor/xboxrecomp/LICENSE',
                         'vendor/xboxrecomp/NOTICE','vendor/xboxrecomp/LICENSES/LGPL-2.1.txt'):
                path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('synthetic notice')
            (build/'module.o').write_bytes(b'synthetic object');(build/'libxbox_apu.a').write_bytes(b'synthetic archive')
            commands=[]
            for image in (*pack.IMAGES,'window_host'):
                name='topspin_'+image;(build/name).write_bytes(b'\x7fELFsynthetic')
                commands.append(dict(directory=str(build),arguments=['cc','module.o','libxbox_apu.a','-o',name,'-lm']))
            link=root/'links.json';link.write_text(json.dumps(dict(commands=commands,toolchain={'compiler':'fixture cc'})))
            bundle=root/'third-party-sources';bundle.mkdir();(bundle/'SOURCE_MANIFEST.json').write_text('{"fixture":true}\n')
            args=argparse.Namespace(source_root=source,build_root=build,output=root/'package',platform='linux',
                link_commands=link,path_map=[],private_prefix=[],strip_tool=None,dll_dir=[],system_dll=[],extra_license=[],dependency_bundle=[bundle])
            features=source/'FEATURES.json'
            features.write_text(json.dumps({'schema':'maxspin-feature-parity-v1','platforms':{
                'linux':{'athlete-packages':1},'windows':{}}}))
            with self.assertRaisesRegex(ValueError,'feature parity'):
                pack.package(args)
            self.assertFalse(args.output.exists())
            features.write_text(json.dumps({'schema':'maxspin-feature-parity-v1','platforms':{
                'linux':{'athlete-packages':1},'windows':{'athlete-packages':1}}}))
            self.assertGreater(pack.package(args),10)
            manifest=json.loads((args.output/'RELEASE_MANIFEST.json').read_text())
            self.assertEqual(len(json.loads((args.output/'relink/targets.json').read_text())['targets']),10)
            self.assertEqual(manifest['features'],{'athlete-packages':1})
            self.assertFalse(any('links.json'in name for name in manifest['files']))
            self.assertIn('third-party-sources/third-party-sources/SOURCE_MANIFEST.json',manifest['files'])
            with self.assertRaisesRegex(ValueError,'must be new'):pack.package(args)


if __name__=='__main__':unittest.main()
