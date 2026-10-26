"""Read-only source-pose flag recorder; no game/GPU. djultra64."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
src=root/'native-linux/src'
with tempfile.TemporaryDirectory(prefix='crowd-pose-cpu-') as temporary:
    binary=Path(temporary)/'fixture'
    subprocess.run(['cc','-std=gnu11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
        '-DTOPSPIN_CROWD_POSE_TRACE_LIMIT=8','-I'+str(src),'-I'+str(src/'recomp/game'),
        str(root/'native-linux/tests/crowd_pose_trace.c'),str(src/'crowd_pose_trace.c'),str(src/'actor_binding_cpu_guard.c'),'-lm','-o',str(binary)],check=True)
    clean={k:v for k,v in os.environ.items() if k!='TOPSPIN_CROWD_POSE_TRACE'}
    def run(mode,on=None):
        env=dict(clean)
        if on is not None:env['TOPSPIN_CROWD_POSE_TRACE']=on
        result=subprocess.run([str(binary),mode],capture_output=True,text=True,env=env,check=True)
        assert result.stdout.startswith('PASS ')
        return result
    for value in (None,'','0','true','1bad'):
        assert not run('invalid',value).stderr
    assert not run('invalid','1').stderr
    for mode,lines in [('good',1),('dedup',4),('budget',8)]:
        off,on=run(mode),run(mode,'1')
        assert off.stdout==on.stdout
        assert not off.stderr
        rows=on.stderr.splitlines()
        assert len(rows)==lines,(mode,len(rows),on.stderr)
        assert all('[CROWD-POSE]' in row and 'author=djultra64' in row for row in rows)
        assert 'source=00010000' in rows[0] and 'animation=00020000' in rows[0]
        if mode=='good':assert 'source38=0>1' in rows[0] and 'animation269=1>1' in rows[0]
    print('PASS exact opt-in, checked pointers, read-only observations, original once/CPU/fenv/memory equivalence, transition dedup and bounded logs; no GPU/game')
