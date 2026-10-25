"""Real play/run shell branches with fake CPU child; no title/SDL/GPU. djultra64."""
from pathlib import Path
import tempfile,subprocess,os,json
N=Path(__file__).resolve().parents[1]/'native-linux'
with tempfile.TemporaryDirectory(prefix='topspin-play-quiet-')as tmp:
 root=Path(tmp);native=root/'native-linux';native.mkdir();assets=root/'assets';assets.mkdir();(assets/'TopSpin(Menu).xbe').touch()
 for name in ['play.sh','run.sh']:(native/name).write_bytes((N/name).read_bytes());(native/name).chmod(0o700)
 (native/'pipeline.py').write_text('print("TopSpin(Menu).xbe")\n')
 (native/'launch.py').write_text('''import os,json,pathlib
pathlib.Path(os.environ['TEST_MARKER']).write_text(json.dumps({k:os.environ.get(k)for k in ['TOPSPIN_MANUAL_DIAGNOSTICS','RECOMP_RUNTIME_QUIET','RECOMP_STUB_LOG','TOPSPIN_VIDEO_TRACE','TOPSPIN_MOVIE_TRACE','TOPSPIN_CAREER_TRACE','TOPSPIN_MENU_INPUT_TRACE','TOPSPIN_PERSISTENT_WINDOW','RECOMP_WATCHDOG_SECS','RECOMP_PAD_SCRIPT','TOPSPIN_VISUAL_CAMERA_BINDING','TOPSPIN_VISUAL_SCENE_RECORD','TOPSPIN_VISUAL_FUTURE_FLAG','RECOMP_EXECUTOR_INVOKE','CPUPROFILE','CPUPROFILESIGNAL','CPUPROFILE_REALTIME']}))
print('fake CPU launcher')
''')
 build=native/'snapshot';build.mkdir()
 for name in ['topspin_menu','topspin_window_host']:(build/name).touch();(build/name).chmod(0o700)
 marker=root/'record.json';base={k:v for k,v in os.environ.items()if not k.startswith(('TOPSPIN_','RECOMP_'))};base|={'TOPSPIN_NATIVE_BUILD_DIR':str(build),'TEST_MARKER':str(marker)}
 def run(extra=None,script='play.sh'):
  marker.unlink(missing_ok=True);env=base|{'RECOMP_PAD_SCRIPT':'stale','RECOMP_STUB_LOG':'1','TOPSPIN_CAREER_TRACE':'stale','TOPSPIN_MOVIE_TRACE':'1'}|(extra or{})
  result=subprocess.run(['bash',str(native/script),'menu'],env=env,capture_output=True,text=True,timeout=5)
  assert result.returncode==0,result.stderr
  return json.loads(marker.read_text())
 row=run();assert row['TOPSPIN_MANUAL_DIAGNOSTICS']=='0' and row['RECOMP_RUNTIME_QUIET']=='1',row
 assert row['RECOMP_STUB_LOG']is None and row['TOPSPIN_CAREER_TRACE']is None and row['TOPSPIN_MOVIE_TRACE']is None,row
 assert row['TOPSPIN_VIDEO_TRACE']is None and row['TOPSPIN_MENU_INPUT_TRACE']is None
 assert row['RECOMP_WATCHDOG_SECS']=='0' and row['RECOMP_PAD_SCRIPT']is None and row['TOPSPIN_PERSISTENT_WINDOW']=='1'
 row=run({'TOPSPIN_VISUAL_CAMERA_BINDING':'1','TOPSPIN_VISUAL_SCENE_RECORD':'1','TOPSPIN_VISUAL_FUTURE_FLAG':'1','RECOMP_EXECUTOR_INVOKE':'1','CPUPROFILE':'stale.prof','CPUPROFILESIGNAL':'27','CPUPROFILE_REALTIME':'1'})
 for key in ['TOPSPIN_VISUAL_CAMERA_BINDING','TOPSPIN_VISUAL_SCENE_RECORD','TOPSPIN_VISUAL_FUTURE_FLAG','RECOMP_EXECUTOR_INVOKE','CPUPROFILE','CPUPROFILESIGNAL','CPUPROFILE_REALTIME']:assert row[key]is None,(key,row)
 row=run({'TOPSPIN_MANUAL_DIAGNOSTICS':'1'});assert row['RECOMP_STUB_LOG']=='1' and row['RECOMP_RUNTIME_QUIET']=='0' and row['TOPSPIN_VIDEO_TRACE']=='1' and row['TOPSPIN_MOVIE_TRACE']=='1',row
 assert row['TOPSPIN_CAREER_TRACE'].endswith('.career.csv') and row['TOPSPIN_MENU_INPUT_TRACE'].endswith('.menu-input.csv')
 row=run({'TOPSPIN_MANUAL_DIAGNOSTICS':'0','RECOMP_RUNTIME_QUIET':'0'});assert row['RECOMP_RUNTIME_QUIET']=='0' and row['RECOMP_STUB_LOG']is None
 row=run({'TOPSPIN_MANUAL_DIAGNOSTICS':'1','RECOMP_RUNTIME_QUIET':'1'});assert row['RECOMP_RUNTIME_QUIET']=='1' and row['RECOMP_STUB_LOG']=='1'
 row=run(script='run.sh');assert row['RECOMP_STUB_LOG']is None and row['RECOMP_RUNTIME_QUIET']=='1' and row['TOPSPIN_MOVIE_TRACE']is None,row
 row=run({'TOPSPIN_MANUAL_DIAGNOSTICS':'1'},script='run.sh');assert row['RECOMP_STUB_LOG']=='1' and row['TOPSPIN_CAREER_TRACE'].endswith('.career.csv')
print('real play/run quiet defaults + explicit diagnostics/quiet override/trace scrubbing/manual input gates CPU PASS')
