"""Original-resource footer placement and intro clip ownership. djultra64."""
from pathlib import Path
import importlib.util,sys,zipfile,struct,tempfile,hashlib,json
N=Path(__file__).resolve().parents[1];sys.path.insert(0,str(N/'tools'))
import ui_layout
from rui_scene_inventory import decode
R=N.parent
intro=ui_layout.read_member(R/'assets','Media/Rui/MatchIntro.rui','Scenes/InGameIntro.sceneData')
after,records=ui_layout.patch_chunk(intro,intro=True)
old=decode(intro);new=decode(after)
a=old['SceneData']['AnimationData']['ClipsKeyframeData'].encode('latin1');b=new['SceneData']['AnimationData']['ClipsKeyframeData'].encode('latin1')
assert len(a)==1884 and len(b)==1852 and b[:0xa4]==a[:0xa4]and b[0xd4:]==a[0xf4:]
new['SceneData']['AnimationData']['ClipsKeyframeData']=old['SceneData']['AnimationData']['ClipsKeyframeData']
assert new==old,'all other intro tables/actions/controls/clips unchanged'
base=ui_layout.read_member(R/'assets','Media/MenuPart/Rui/Base.rui','Templates/Help.sceneData')
after,records=ui_layout.patch_chunk(base)
def nodes(v):
 if not isinstance(v,dict):return
 yield v
 for x in v.values():yield from nodes(x)
before={n['Name']:n for n in nodes(decode(base))if n.get('Name')in ui_layout.FOOTER_NAMES}
changed={n['Name']:n for n in nodes(decode(after))if n.get('Name')in ui_layout.FOOTER_NAMES}
assert len(before)==2
for name,original in before.items():
 current=changed[name];oldpos=struct.unpack('<3f',original['LPM'].encode('latin1'));newpos=struct.unpack('<3f',current['LPM'].encode('latin1'))
 assert newpos==(oldpos[0],oldpos[1]-16,oldpos[2])
 current['LPM']=original['LPM'];assert current==original
assert changed==before
# Explicit screen overrides move once; modal and unrelated text stay put.
from menu_pc_scene import encode
fixture=decode(base)
fixture['SceneData']['Fixture']={'Menu|Left_TextBox':{'LPM':struct.pack('<3f',0,-187,0).decode('latin1')},'Modal':{'Name':'Left_TextBox','LPM':struct.pack('<3f',0,-40,0).decode('latin1')},'Other':{'Name':'Title','LPM':struct.pack('<3f',0,-187,0).decode('latin1')}}
patched,rows=ui_layout.patch_chunk(encode(base,fixture))
result=decode(patched)['SceneData']['Fixture']
assert struct.unpack('<3f',result['Menu|Left_TextBox']['LPM'].encode('latin1'))[1]==-203
assert result['Modal']==fixture['SceneData']['Fixture']['Modal'] and result['Other']==fixture['SceneData']['Fixture']['Other']
with tempfile.TemporaryDirectory()as temporary:
 out=Path(temporary)/'private'
 report=ui_layout.prepare(R/'assets',out)
 assert report['footer_nodes']>=12 and report['intro_fixed']
 selected=ui_layout.select_assets(R/'assets',out)
 assert selected==out/'view'and (selected/'TopSpin(Menu).xbe').resolve()==(R/'assets/TopSpin(Menu).xbe')
 assert (selected/'Media/Loadings/LoadEng00.jpg').resolve()==(R/'assets/Media/Loadings/LoadEng00.jpg')
 assert ui_layout.select_assets(R/'assets',Path(temporary)/'absent')==R/'assets'
 assert out.stat().st_mode&0o222==0 and (out/'patches/Media/Rui/MatchIntro.rui').stat().st_mode&0o222==0
 # The selected original executable must remain its exact original link.
 selected.chmod(0o700);link=selected/'TopSpin(Game).xbe';link.unlink();link.symlink_to(R/'assets/Media/Loadings/LoadEng00.jpg')
 try:ui_layout.select_assets(R/'assets',out)
 except ValueError:pass
 else:raise AssertionError('changed original executable link accepted')
 link.unlink();link.symlink_to(R/'assets/TopSpin(Game).xbe');selected.chmod(0o555)
 target=selected/'Media/Rui/MatchIntro.rui';target.chmod(0o600);target.write_bytes(target.read_bytes()+b'corrupt')
 try:ui_layout.select_assets(R/'assets',out)
 except ValueError:pass
 else:raise AssertionError('modified private archive accepted')
 ui_layout.discard_tree(out)
print('PASS original footer glyph+text moved together16 units, exact other intro tables preserved, competing alpha removed, all-package preparation, originals untouched via asset view, corrupt overlay rejects')
