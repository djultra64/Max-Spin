"""Face Builder source preservation, geometry and recipe contracts. djultra64."""
from pathlib import Path
import importlib.util
import json
import hashlib
import sys
import struct
sys.dont_write_bytecode = True
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT/'face-builder'
if not (BUILDER/'tests/compiler-tests.py').is_file():
    raise unittest.SkipTest('optional Face Builder research source is not included')

class GeometryTests(unittest.TestCase):
    def test_geometry_recipe_correspondence_and_export(self):
        result = subprocess.run(['mujs',str(BUILDER/'tests/geometry-tests.js'),str(BUILDER/'geometry.js')],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('PASS:',result.stdout)

class BuildTests(unittest.TestCase):
    def test_real_character_builds_preserve_sources_and_distinguish_meshes(self):
        path=BUILDER/'build.py'
        self.assertTrue(path.exists(), 'Face Builder packager is missing')
        spec=importlib.util.spec_from_file_location('face_builder_build',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        legacy=ROOT.parent/'character-research/legacy'
        watched=[legacy/'build/Federer-Hairline-Experiment-03/custom-head/federer-custom-head.obj',
                 legacy/'build/Federer-Custom-Mesh-Experiment-01/custom-head/federer-face-albedo-01.png',
                 legacy/'build/Federer-Creator-Test/Media/Wads/Players/Houston00.wad']
        before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
        with tempfile.TemporaryDirectory() as folder:
            proofs=[]
            for character in ['federer','nadal']:
                output=Path(folder)/character/'index.html'
                proof=module.build(character,output)
                self.assertTrue(output.is_file())
                page=output.read_text()
                start=page.index('<script id="asset" type="application/json">')+len('<script id="asset" type="application/json">')
                asset=json.loads(page[start:page.index('</script>',start)])
                self.assertEqual(asset['source']['character'],character)
                self.assertGreater(asset['triangleCount'],1000)
                self.assertGreater(len(asset['faceTriangles']),1000)
                self.assertEqual(len(asset['export']['positions']),asset['sourceVertexCount'])
                self.assertEqual(proof['source_mesh_sha256'],asset['source']['meshSha256'])
                self.assertIn(asset['faceTexture'],asset['textures'])
                self.assertTrue((output.parent/'sources/native-bindings.json').exists(), 'Native binding sidecar is missing')
                mapping=json.loads((output.parent/'sources/native-bindings.json').read_text())
                native=(output.parent/'sources/native-source.ooo').read_bytes()
                self.assertEqual(hashlib.sha256(native).hexdigest(),mapping['source_native_model_sha256'])
                self.assertEqual(mapping['source_obj_sha256'],asset['source']['meshSha256'])
                self.assertEqual(len(mapping['vertices']),asset['sourceVertexCount'])
                uv=[list(map(float,line.split()[1:3])) for line in asset['export']['obj'].splitlines() if line.startswith('vt ')]
                for i,binding in enumerate(mapping['vertices']):
                    self.assertIsNotNone(binding,'Every source vertex must have a native binding')
                    u,v=struct.unpack_from('<2f',native,binding['position_offset']+24)
                    self.assertAlmostEqual(u,uv[i][0],places=5)
                    self.assertAlmostEqual(1-v,uv[i][1],places=5)
                self.assertNotIn('__EMBEDDED_',page)
                self.assertNotIn('http://',page)
                proofs.append(proof)
            self.assertNotEqual(proofs[0]['source_mesh_sha256'],proofs[1]['source_mesh_sha256'])
            self.assertEqual(proofs[0]['source_mesh_sha256'],before[watched[0]])
            self.assertIn(before[watched[1]],proofs[0]['texture_sha256'])
        self.assertEqual(before,{p:hashlib.sha256(p.read_bytes()).hexdigest() for p in watched})

    def test_build_rejects_unknown_character_without_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'unknown.html'
            result=subprocess.run(['python3',str(BUILDER/'build.py'),'--character','unknown','--output',str(output)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(output.exists())

compiler_spec=importlib.util.spec_from_file_location('face_builder_compiler_tests',BUILDER/'tests/compiler-tests.py')
compiler_tests=importlib.util.module_from_spec(compiler_spec);compiler_spec.loader.exec_module(compiler_tests)
CompilerTests=compiler_tests.CompilerTests

if __name__=='__main__':unittest.main()
