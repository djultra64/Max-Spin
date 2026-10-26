"""Fixed-mesh face texture landmark correspondence. Own work: djultra64."""
from pathlib import Path
import importlib.util,json,subprocess,sys,tempfile,unittest,struct
NATIVE=Path(__file__).resolve().parents[1];PROJECT=NATIVE.parent

def triangle(u,v):
 return [{'uv':[u,v],'position':[0,0,0]}, {'uv':[u+.01,v],'position':[1,0,0]}, {'uv':[u,v+.01],'position':[0,1,0]}]

class FaceUVTests(unittest.TestCase):
 def module(self):
  p=NATIVE/'tools/character_uv.py';self.assertTrue(p.exists(),'native UV landmark compiler missing')
  spec=importlib.util.spec_from_file_location('character_uv_test_module',p);m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m);return m
 def test_exact_anchor_correspondence_and_immutable_boundary(self):
  m=self.module();tris=[triangle(.5,.5),triangle(.43,.38)]
  alignment={'nose':{'triangle':0,'weights':[1,0,0],'targetUV':[.51,.52]},'leftEye':{'triangle':1,'weights':[1,0,0],'targetUV':[.425,.39]}}
  before=json.dumps(tris);warp=m.compile_alignment(alignment,tris)
  self.assertAlmostEqual(warp.apply([.5,.5])[0],.51,places=12);self.assertAlmostEqual(warp.apply([.5,.5])[1],.52,places=12)
  self.assertAlmostEqual(warp.apply([.43,.38])[0],.425,places=12)
  self.assertEqual(warp.apply([.1,.1]),[.1,.1]);self.assertEqual(json.dumps(tris),before)
  self.assertEqual(m.compile_alignment({},tris).apply([.5,.5]),[.5,.5])
  with self.assertRaises(ValueError):m.compile_alignment({'nose':{'triangle':0,'weights':[True,0,0],'targetUV':[.5,.5]}},tris)
  with self.assertRaises(ValueError):m.compile_alignment({'nose':{'triangle':0,'weights':[1,0,0],'targetUV':[.9,.5]}},tris)
 @unittest.skipUnless((PROJECT/'face-builder/uv-alignment.js').is_file(), 'optional native Face Builder fixture unavailable')
 def test_javascript_python_warp_parity(self):
  m=self.module();kernel=PROJECT/'face-builder/uv-alignment.js';self.assertTrue(kernel.exists(),'viewer UV kernel missing')
  tris=[triangle(.5,.5),triangle(.43,.38)];points=[[.5,.5],[.43,.38],[.48,.43],[.7,.6],[.1,.1]]
  alignment={'nose':{'triangle':0,'weights':[1,0,0],'targetUV':[.51,.52]},'leftEye':{'triangle':1,'weights':[1,0,0],'targetUV':[.425,.39]}}
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'check.js';p.write_text('load('+json.dumps(str(kernel))+');var w=FaceUV.compile('+json.dumps(alignment)+','+json.dumps(tris)+');print(JSON.stringify('+json.dumps(points)+'.map(function(p){return w.apply(p);})));')
   result=subprocess.run(['mujs',str(p)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
  want=m.compile_alignment(alignment,tris);actual=json.loads(result.stdout)
  for point,got in zip(points,actual):
   for a,b in zip(want.apply(point),got):self.assertAlmostEqual(a,b,places=12)
 def test_interior_pick_matches_rendered_interpolated_texture_point(self):
  m=self.module();tris=[triangle(.50,.50)]
  target=[.506,.518]
  pair={'mouth':{'triangle':0,'weights':[1/3,1/3,1/3],'targetUV':target}}
  warp=m.compile_alignment(pair,tris)
  rendered=[sum(warp.apply(v['uv'])[axis]/3 for v in tris[0]) for axis in range(2)]
  for got,want in zip(rendered,target):self.assertAlmostEqual(got,want,places=12)
 @unittest.skipUnless((PROJECT/'face-builder/uv-alignment.js').is_file(), 'optional native Face Builder fixture unavailable')
 def test_nine_feature_pairs_include_local_ear_atlas_island(self):
  m=self.module();tris=[triangle(.04,.70),triangle(.45,.45),triangle(.5,.3)]
  pairs={'earLeft':{'triangle':0,'weights':[1/3]*3,'targetUV':[.047,.707]},'mouthLeft':{'triangle':1,'weights':[1,0,0],'targetUV':[.452,.451]},'chin':{'triangle':2,'weights':[1,0,0],'targetUV':[.5,.302]}}
  warp=m.compile_alignment(pairs,tris)
  self.assertEqual(warp.apply([.9,.1]),[.9,.1])
  for point in pairs.values():
   actual=[sum(warp.apply(v['uv'])[axis]*w for v,w in zip(tris[point['triangle']],point['weights'])) for axis in range(2)]
   for got,want in zip(actual,point['targetUV']):self.assertAlmostEqual(got,want,places=12)
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'nine.js';path.write_text('load('+json.dumps(str(PROJECT/'face-builder/uv-alignment.js'))+');var w=FaceUV.compile('+json.dumps(pairs)+','+json.dumps(tris)+');print(JSON.stringify('+json.dumps([[.04,.70],[.45,.45],[.5,.3]])+'.map(function(p){return w.apply(p);})));')
   run=subprocess.run(['mujs',str(path)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
  for p,actual in zip([[.04,.70],[.45,.45],[.5,.3]],json.loads(run.stdout)):
   for a,b in zip(warp.apply(p),actual):self.assertAlmostEqual(a,b,places=12)
 @unittest.skipUnless((PROJECT.parent/'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').is_file(), 'optional native Face Builder fixture unavailable')
 def test_serialized_native_low_lod_fold_is_rejected(self):
  m=self.module();self.assertTrue(hasattr(m,'validate_native_uv'),'native LOD triangle validation missing')
  sys.path.insert(0,str(NATIVE/'tools'));import character_formats as f
  archive=PROJECT.parent/'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad'
  raw=f.unpack_zlb(f.payloads(archive.read_bytes())['media/gamepart/players/tukurxx00/playerlod01.ooo'])
  for node in f.parse_model(raw)['nodes']:
   if not node['name'].startswith('M_MRF_VSG'):continue
   for draw in node['draw_ranges']:
    stream=node['streams'][draw['format']];indices=draw['indices']
    for i in range(len(indices)-2):
     ids=indices[i:i+3]
     if len(set(ids))<3:continue
     ats=[stream['offset']+j*stream['stride']+24 for j in ids]
     uv=[struct.unpack_from('<2f',raw,at) for at in ats]
     area=(uv[1][0]-uv[0][0])*(uv[2][1]-uv[0][1])-(uv[1][1]-uv[0][1])*(uv[2][0]-uv[0][0])
     if abs(area)<1e-8:continue
     revised=bytearray(raw);revised[ats[0]:ats[0]+8]=raw[ats[1]:ats[1]+8];revised[ats[1]:ats[1]+8]=raw[ats[0]:ats[0]+8]
     with self.assertRaises(ValueError):m.validate_native_uv(raw,bytes(revised))
     m.validate_native_uv(raw,raw)
     return
  self.fail('No nondegenerate native low-LOD face triangle found')
 @unittest.skipUnless((PROJECT/'face-builder/build/nadal/sources/native-bindings.json').is_file() and (PROJECT.parent/'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').is_file(), 'optional native Face Builder fixture unavailable')
 def test_native_alignment_changes_only_face_uv_records_all_variants(self):
  m=self.module();sys.path.insert(0,str(NATIVE/'tools'));import character_formats as f
  legacy=PROJECT.parent/'character-research/legacy';source=legacy/'build/Federer-Creator-Test/Media/Wads/Players'
  for character,code in [('federer','TukurXX'),('nadal','Houston')]:
   data=m.trusted_face(character);i=next(j for j,t in enumerate(data) if .49<t[0]['uv'][0]<.51 and .49<t[0]['uv'][1]<.55)
   uv=data[i][0]['uv'];alignment={'nose':{'triangle':i,'weights':[1,0,0],'targetUV':[uv[0]+.002,uv[1]+.002]}}
   for outfit in (0,1):
    entries=f.payloads((source/(code+f'{outfit:02d}.wad')).read_bytes())
    for lod in (0,1):
     raw=f.unpack_zlb(entries[f'media/gamepart/players/{code.lower()}{outfit:02d}/playerlod{lod:02d}.ooo'])
     result,proof=m.apply_uv_alignment(character,raw,outfit,lod,alignment)
     self.assertEqual(len(result),len(raw));self.assertGreater(proof['changed_bytes'],0)
     allowed=set()
     for node in f.parse_model(raw)['nodes']:
      if node['name'].startswith('M_MRF_VSG'):
       for stream in node['streams'].values():
        for index in range(stream['count']):
         at=stream['offset']+index*stream['stride']+24;allowed.update(range(at,at+8))
         original_uv=struct.unpack_from('<2f',raw,at)
         if outfit==0 and lod==0 and abs(original_uv[0]-uv[0])<1e-7 and abs(1-original_uv[1]-uv[1])<1e-7:
          revised=struct.unpack_from('<2f',result,at)
          self.assertAlmostEqual(revised[0],original_uv[0]+.002,places=6)
          self.assertAlmostEqual(revised[1],original_uv[1]-.002,places=6)
     self.assertTrue(all(a==b or i in allowed for i,(a,b) in enumerate(zip(raw,result))))
     self.assertEqual(m.apply_uv_alignment(character,raw,outfit,lod,{})[0],raw)
if __name__=='__main__':unittest.main()
