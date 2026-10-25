"""First-start missing originals/build/resources and ready path. djultra64."""
from pathlib import Path
import importlib.util,tempfile,json,subprocess,sys
N=Path(__file__).resolve().parents[1]/'native-linux';path=N/'tools/startup_check.py'
spec=importlib.util.spec_from_file_location('readiness',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='max spin startup ')as temporary:
 root=Path(temporary);native=root/'native-linux';(native/'config').mkdir(parents=True)
 images={name:name+'.xbe'for name in ('boot','menu','game','career','training','playercreator','shop','online','stop')}
 (native/'config/images.json').write_text(json.dumps(images));build=root/'bin';build.mkdir()
 result=module.check(root,build);assert len(result)==2 and 'ISO' in result[0]and 'compilada' in result[1]
 (root/'disc.iso').write_bytes(b'not-read');assert module.check(root,build)==result
 assets=root/'assets';(assets/'Media').mkdir(parents=True)
 for name in images.values():(assets/name).touch()
 assert len(module.check(root,build))==1 and 'compilada' in module.check(root,build)[0]
 for name in [*images,'window_host']:
  p=build/('topspin_'+name);p.touch();p.chmod(0o700)
 assert len(module.check(root,build))==1 and 'preparar' in module.check(root,build)[0]
 (build/'manifest.json').write_text('{broken');assert module.check(root,build)
 (build/'manifest.json').write_text(json.dumps({'ui_cache_version':4,'controller_cache_version':4}))
 for name,files in module.OUTPUTS.items():
  for file in files:
   p=native/'cache'/name/file;p.parent.mkdir(parents=True,exist_ok=True);p.touch()
 assert not module.check(root,build)
 run=subprocess.run([sys.executable,str(path),'--root',str(root),'--build-dir',str(build)],capture_output=True,text=True)
 assert run.returncode==0 and not run.stdout and not run.stderr
 (assets/'menu.xbe').unlink()
 run=subprocess.run([sys.executable,str(path),'--root',str(root),'--build-dir',str(build)],capture_output=True,text=True)
 assert run.returncode==2 and 'ISO' in run.stdout and 'docs/install.md' in run.stdout
 (build/'topspin_game').chmod(0o600);assert any('compilada' in x for x in module.check(root,build))
print('PASS first-start: ISO/originals missing, source not built/incomplete/unexecutable, setup missing/malformed, ready and CLI status; read-only, no title/SDL')
