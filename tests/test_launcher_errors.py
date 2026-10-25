"""Actual MaxSpin.sh startup errors with CPU-only children. djultra64."""
from pathlib import Path
import os,shutil,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
source=R/'MaxSpin.sh'
def verify(source):
 with tempfile.TemporaryDirectory(prefix='max spin launcher ')as temporary:
  root=Path(temporary);native=root/'native-linux';native.mkdir();(root/'bin').mkdir();(native/'tools').mkdir();(native/'tools/startup_check.py').write_text('raise SystemExit(0)\n')
  shutil.copy2(source,root/'MaxSpin.sh')
  (root/'bin/topspin_window_host').touch();(root/'bin/topspin_window_host').chmod(0o700)
  (native/'play.sh').write_text('#!/usr/bin/env bash\nprintf "%s" "$TOPSPIN_NATIVE_BUILD_DIR" > "$TEST_BUILD_MARKER"\nprintf "%s\\n" "$@" > "$TEST_ARGS_MARKER"\nprintf "Missing original image: TopSpin(Menu).xbe\\n" >&2\nexit "$TEST_EXIT"\n');(native/'play.sh').chmod(0o700)
  helpers=root/'helpers';helpers.mkdir();helper=helpers/'zenity'
  helper.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$TEST_DIALOG_MARKER"\nexit "$TEST_DIALOG_EXIT"\n');helper.chmod(0o700)
  (helpers/'kdialog').write_text('#!/usr/bin/env bash\nexit 0\n');(helpers/'kdialog').chmod(0o700)
  base=os.environ.copy();base.update(PATH=str(helpers)+':'+base['PATH'],DISPLAY=':cpu-fixture',TEST_BUILD_MARKER=str(root/'build-marker'),TEST_ARGS_MARKER=str(root/'args-marker'),TEST_DIALOG_MARKER=str(root/'dialog-marker'));base.pop('WAYLAND_DISPLAY',None)
  marker=root/'dialog-marker'
  for status,display,dialog_exit in [(0,True,0),(2,True,0),(2,True,1),(130,True,0),(2,False,0)]:
   marker.unlink(missing_ok=True);env=base|{'TEST_EXIT':str(status),'TEST_DIALOG_EXIT':str(dialog_exit)}
   if not display:env.pop('DISPLAY',None)
   result=subprocess.run(['bash',str(root/'MaxSpin.sh'),'--play','menu'],env=env,capture_output=True,text=True,timeout=5)
   assert result.returncode==status,(status,result.returncode,result.stderr)
   assert (root/'build-marker').read_text()==str(root/'bin')
   assert (root/'args-marker').read_text()=='menu\n'
   if status not in (0,130):
    logs=list((native/'logs').glob('launcher-*.log'));assert logs and any('Missing original image' in p.read_text()for p in logs),'early startup failure must be logged'
    assert 'TopSpin(Menu).xbe' in result.stderr,'CLI must keep the actual error'
    if display:assert marker.is_file()and 'TopSpin(Menu).xbe' in marker.read_text(),'double-click failure must show a useful dialog'
    else:assert not marker.exists()
   else:assert not marker.exists(),'success and user interruption do not show error dialogs'
  # Actual checker + actual wrapper: never start the child when first-start
  # prerequisites are absent; the file-manager dialog explains ISO/build.
  shutil.copy2(source.parent/'native-linux/tools/startup_check.py'if source.parent.name!='distribution'else source.parents[1]/'tools/startup_check.py',native/'tools/startup_check.py')
  (native/'config').mkdir()
  (native/'config/images.json').write_text(__import__('json').dumps({name:name+'.xbe'for name in ('boot','menu','game','career','training','playercreator','shop','online','stop')}))
  marker.unlink(missing_ok=True);(root/'build-marker').unlink()
  result=subprocess.run(['bash',str(root/'MaxSpin.sh')],env=base|{'TEST_EXIT':'0','TEST_DIALOG_EXIT':'0'},capture_output=True,text=True,timeout=5)
  assert result.returncode==2 and marker.is_file()and 'ISO' in marker.read_text()and 'compilada' in marker.read_text()
  assert not (root/'build-marker').exists(),'not-ready installation never starts game child'
  # A minimal desktop PATH without optional dialog packages still receives
  # a visible explanation through its normal text-file opener.
  for tool in ('bash','env','dirname','readlink','mkdir','mktemp','tee','tail','python3','cat'):
   target=helpers/tool
   if not target.exists():target.symlink_to(shutil.which(tool))
  fallback=root/'fallback-marker';base.update(TEST_FALLBACK_MARKER=str(fallback),TEST_FALLBACK_CONTENT=str(root/'fallback-content'))
  kd=helpers/'kdialog';kd.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$TEST_FALLBACK_MARKER"\n');kd.chmod(0o700)
  result=subprocess.run(['bash',str(root/'MaxSpin.sh')],env=base|{'PATH':str(helpers),'TEST_EXIT':'0','TEST_DIALOG_EXIT':'1'},capture_output=True,text=True,timeout=5)
  assert result.returncode==2 and fallback.is_file(),'failed zenity must fall back to kdialog'
  helper.unlink();kd.unlink();fallback.unlink()
  opener=helpers/'xdg-open';opener.write_text('#!/usr/bin/env bash\nprintf "%s" "$1" > "$TEST_FALLBACK_MARKER"\ncat "$1" > "$TEST_FALLBACK_CONTENT"\n');opener.chmod(0o700)
  result=subprocess.run(['bash',str(root/'MaxSpin.sh')],env=base|{'PATH':str(helpers),'TEST_EXIT':'0','TEST_DIALOG_EXIT':'0'},capture_output=True,text=True,timeout=5)
  assert result.returncode==2 and fallback.is_file()and 'ISO' in (root/'fallback-content').read_text(),'no dialog helper must still show first-start instructions'
 print('PASS real launcher: early error logged and visible, spaced path/arguments/build preserved, GUI failure keeps status, headless fallback, success and user interruption quiet')
if __name__=='__main__':verify(source)
