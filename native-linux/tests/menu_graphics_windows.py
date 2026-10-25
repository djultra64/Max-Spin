"""Private Graphics RUI preparation on Windows Python. Own work: djultra64."""
from pathlib import Path
import hashlib,os,stat,subprocess,sys,tempfile,unittest
from unittest.mock import patch
N=Path(__file__).resolve().parents[1];R=N.parent;sys.path.insert(0,str(N/'tools'))
import menu_graphics_scene as graphics
PACKAGE=R/'assets/Media/MenuPart/Rui/MainMenu.rui'
class WindowsPreparation(unittest.TestCase):
 def test_cache_and_header_cli_without_no_follow_flag(self):
  # Removing the absent-on-Windows flag must not disable exclusive creation.
  with tempfile.TemporaryDirectory()as folder:
   root=Path(folder);cache=root/'cache';header=root/'guards.h'
   driver="import os,runpy,sys;os.__dict__.pop('O_NOFOLLOW',None);sys.path.insert(0,sys.argv[1]);sys.argv=sys.argv[2:];runpy.run_path(sys.argv[0],run_name='__main__')"
   result=subprocess.run([sys.executable,'-c',driver,str(N/'tools'),str(N/'tools/menu_graphics_scene.py'),str(PACKAGE),'--output',str(cache),'--header',str(header)],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stderr)
   self.assertEqual(header.read_bytes(),(N/'src/menu_graphics_resources.h').read_bytes())
   chunks=tuple((cache/name).read_bytes()for name in graphics.contract()['resources'])
   self.assertEqual(graphics.resource_header(chunks),header.read_bytes())
   # Existing output is preserved even with no O_NOFOLLOW on the host.
   result=subprocess.run([sys.executable,'-c',driver,str(N/'tools'),str(N/'tools/menu_graphics_scene.py'),str(PACKAGE),'--output',str(cache)],capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0)
   self.assertEqual(tuple((cache/name).read_bytes()for name in graphics.contract()['resources']),chunks)
   # Unseal only this fixture's own outputs for native Windows temp cleanup.
   for p in cache.iterdir():p.chmod(0o600)
   cache.chmod(0o700)
 def test_failed_prepare_unseals_owned_files_before_rollback(self):
  with tempfile.TemporaryDirectory()as folder:
   output=Path(folder)/'cache';real_open=os.open;real_unlink=Path.unlink;calls=0
   def fail_second_create(*args,**kwargs):
    nonlocal calls
    calls+=1
    if calls==2:raise OSError('injected second create failure')
    return real_open(*args,**kwargs)
   def windows_unlink(path,*args,**kwargs):
    # The native Windows API refuses deleting readonly files. Keep the real
    # first file/write/chmod and real unlink once the preparer unseals it.
    if path.exists()and not stat.S_IMODE(path.stat().st_mode)&0o222:raise PermissionError('Windows readonly file')
    return real_unlink(path,*args,**kwargs)
   before=hashlib.sha256(PACKAGE.read_bytes()).hexdigest()
   with patch.object(os,'open',fail_second_create),patch.object(Path,'unlink',windows_unlink):
    with self.assertRaisesRegex(OSError,'injected second create failure'):graphics.prepare(PACKAGE,output)
   self.assertFalse(output.exists(),'failed invocation left its private output behind')
   self.assertEqual(hashlib.sha256(PACKAGE.read_bytes()).hexdigest(),before)
if __name__=='__main__':unittest.main()
