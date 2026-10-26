"""Character asset root stays owned across actual image handoffs. djultra64."""
from pathlib import Path
import json,os,subprocess,sys,tempfile,unittest
NATIVE=Path(__file__).resolve().parents[1]
class CharacterLaunchTests(unittest.TestCase):
 def test_missing_slot_directory_keeps_default_assets_across_handoffs(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);build=root/'build';assets=root/'assets';saves=root/'saves'
   for p in (build,assets,saves):p.mkdir()
   for name in ('TopSpin(Menu).xbe','TopSpin(Game).xbe'):(assets/name).write_bytes(b'original image')
   (assets/'resource').write_bytes(b'original player')
   worker='''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
image=Path(sys.argv[0]).name[8:]
with open(os.environ['CHARACTER_RECORD'],'a') as f:f.write(json.dumps({'image':image,'assets':sys.argv[1],'resource':(Path(sys.argv[1])/'resource').read_text()})+'\\n')
if image=='menu':
 page=bytearray(4096);dest=b'D:\\\\TopSpin(Game).xbe';page[8:8+len(dest)]=dest
 Path(os.environ['TOPSPIN_LAUNCH_PAGE_OUT']).write_bytes(page)
'''
   for name in ('menu','game'):
    f=build/('topspin_'+name);f.write_text(worker);f.chmod(0o755)
   record=root/'record.jsonl'
   env=dict(os.environ,CHARACTER_RECORD=str(record));env.pop('TOPSPIN_PERSISTENT_WINDOW',None)
   args=[sys.executable,str(NATIVE/'launch.py'),'menu','--build-dir',str(build),'--assets',str(assets),'--saves',str(saves),'--characters',str(root/'missing-slots')]
   result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=10)
   self.assertEqual(result.returncode,0,result.stderr)
   rows=[json.loads(s) for s in record.read_text().splitlines()]
   self.assertEqual([r['image'] for r in rows],['menu','game'])
   self.assertTrue(all(r['assets']==str(assets) and r['resource']=='original player' for r in rows))
   self.assertEqual((assets/'resource').read_bytes(),b'original player')
if __name__=='__main__':unittest.main()
