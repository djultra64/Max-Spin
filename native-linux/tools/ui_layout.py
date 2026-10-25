#!/usr/bin/env python3
"""Private original RUI layout copies; originals remain unchanged. djultra64."""
import argparse,hashlib,json,os,shutil,struct,tempfile,zipfile,zlib
from pathlib import Path
from rui_scene_inventory import decode
from menu_pc_scene import encode

NATIVE=Path(__file__).resolve().parents[1]
FOOTER_NAMES=('Left_TextBox','Right_TextBox')
FOOTER_DY=-16.
INTRO_SHA='fe610251857a553600d494607910d1ba8a87c9bf3d0e13af1be0797d8b83a94b'
def sha(data):return hashlib.sha256(data).hexdigest()
def read_member(assets,relative,name):
 path=Path(assets)/relative;known=json.loads((NATIVE/'config/ui_layout_sources.json').read_text())
 raw=path.read_bytes()
 if known.get(relative)!=sha(raw):raise ValueError('Original UI archive differs: '+relative)
 with zipfile.ZipFile(path)as archive:
  info=archive.getinfo(name)
  if info.file_size>16*1024*1024 or info.flag_bits&1:raise ValueError('Unsupported UI member')
  try:return archive.read(name)
  except zipfile.BadZipFile:
   # Several known shipped RUI members carry a stale CRC. The complete archive
   # SHA above authenticates every byte; do not extend this to unknown files.
   at=info.header_offset
   if raw[at:at+4]!=b'PK\x03\x04':raise ValueError('Bad original local header')
   names,extra=struct.unpack_from('<HH',raw,at+26);start=at+30+names+extra
   stored=raw[start:start+info.compress_size]
   if info.compress_type==0:data=stored
   elif info.compress_type==8:data=zlib.decompress(stored,-15)
   else:raise ValueError('Unsupported original compression')
   if len(data)!=info.file_size:raise ValueError('Original member length mismatch')
   return data

class UnsupportedScene(ValueError):pass

def patch_chunk(original,intro=False):
 try:table=decode(original)
 except ValueError as error:raise UnsupportedScene(str(error))from error
 records=[]
 def visit(value,key=''):
  if not isinstance(value,dict):return
  name=value.get('Name')or key.rsplit('|',1)[-1]
  if name in FOOTER_NAMES and isinstance(value.get('LPM'),str):
   raw=value['LPM'].encode('latin1')
   if len(raw)not in (12,48):raise ValueError('Unknown footer position')
   y=struct.unpack_from('<f',raw,4)[0]
   if -200<=y<=-175:
    value['LPM']=(raw[:4]+struct.pack('<f',y+FOOTER_DY)+raw[8:]).decode('latin1')
    records.append({'node':name,'before_y':y,'after_y':y+FOOTER_DY})
  for k,item in value.items():
   if isinstance(item,dict):visit(item,str(k))
 visit(table)
 if intro:
  if sha(original)!=INTRO_SHA:raise ValueError('Unknown original match intro')
  animation=table['SceneData']['AnimationData'];data=animation['ClipsKeyframeData'].encode('latin1')
  names=animation['OrderedNames'];clips={v['Name']:int(v['SourceClip'])for v in animation['ClipsInfo'].values()}
  if clips['IntroAnim']!=1 or clips['PressStartAnim']!=11 or len(data)!=1884 or struct.unpack_from('<I',data)[0]!=12:raise ValueError('Intro clips differ')
  if names[3]!='group_text'or struct.unpack_from('<3I',data,0xb4)!=(2,0,1)or struct.unpack_from('<II',data,0xc0)!=(3,1):raise ValueError('Competing intro track differs')
  if struct.unpack_from('<2f',data,0xc8)!=(0.,1.):raise ValueError('Intro parent alpha differs')
  replacement=data[0xa4:0xa8]+struct.pack('<II',1,0)+data[0xd0:0xf4]
  animation['ClipsKeyframeData']=(data[:0xa4]+replacement+data[0xf4:]).decode('latin1')
  records.append({'intro':'IntroAnim','removed':'group_text alpha=1','preserved':'all other clips, LoadingAnim and PressStartAnim hide track'})
 return (encode(original,table)if records else original),records

def seal_tree(root):
 # Never follow links into originals when sealing our own derived files.
 for current,dirs,files in os.walk(root,topdown=False):
  for name in files:
   item=Path(current)/name
   if not item.is_symlink():item.chmod(0o444)
  Path(current).chmod(0o555)

def discard_tree(root):
 for current,dirs,files in os.walk(root):
  Path(current).chmod(0o700)
  for name in files:
   item=Path(current)/name
   if not item.is_symlink():item.chmod(0o600)
  for name in dirs:
   item=Path(current)/name
   if not item.is_symlink():item.chmod(0o700)
 shutil.rmtree(root)

def resource_header(report):
 lines=['/* Exact private RUI identities; no original resources. Own work: djultra64. */',
        '#ifndef TOPSPIN_UI_LAYOUT_RESOURCES_H','#define TOPSPIN_UI_LAYOUT_RESOURCES_H',
        'static const struct TopspinUILayoutResource {const char *path,*original,*patched;} topspin_ui_layout_resources[]={']
 for relative,record in sorted(report['archives'].items()):
  lines.append(' {"'+relative+'","'+record['original_sha256']+'","'+record['patched_sha256']+'"},')
 lines+=['};','#endif','']
 return '\n'.join(lines).encode('ascii')

def prepare(assets,output,asset_view=None):
 assets=Path(assets).resolve();output=Path(output).absolute()
 if asset_view is None:asset_view=os.name!='nt'
 if output.exists()or output.is_symlink():raise ValueError('UI output already exists; preserve it and use a new directory')
 output.parent.mkdir(parents=True,exist_ok=True)
 temporary=Path(tempfile.mkdtemp(prefix='.ui-layout-',dir=output.parent));changed={};footer_nodes=0;intro_fixed=False;unsupported=[]
 try:
  known=json.loads((NATIVE/'config/ui_layout_sources.json').read_text())
  for relative,expected in known.items():
   source=assets/relative
   if sha(source.read_bytes())!=expected:raise ValueError('Original UI archive differs: '+relative)
   entries=[];changes=[]
   with zipfile.ZipFile(source)as archive:
    for info in archive.infolist():
     data=read_member(assets,relative,info.filename);new=data
     if info.filename.endswith('.sceneData'):
      is_intro=relative=='Media/Rui/MatchIntro.rui'and info.filename=='Scenes/InGameIntro.sceneData'
      try:new,records=patch_chunk(data,intro=is_intro)
      except UnsupportedScene as error:
       if is_intro:raise
       # Non-declarative scenes cannot be guessed or executed by this preparer.
       unsupported.append({'archive':relative,'member':info.filename,'reason':str(error)});records=[]
      if records:
       changes.append({'member':info.filename,'original_sha256':sha(data),'patched_sha256':sha(new),'changes':records})
       footer_nodes+=sum('node'in row for row in records);intro_fixed|=is_intro
     entries.append((info,new))
   if not changes:continue
   target=temporary/'patches'/relative;target.parent.mkdir(parents=True,exist_ok=True)
   with zipfile.ZipFile(target,'w')as archive:
    for info,data in entries:archive.writestr(info,data)
   changed[relative]={'original_sha256':expected,'patched_sha256':sha(target.read_bytes()),'members':changes}
  if not changed or not intro_fixed:raise ValueError('Required UI changes not prepared')
  # Expand just paths to patched leaves; unchanged directories/files link to
  # the user's read-only originals. Never copy or modify saves or originals.
  view=temporary/'view'
  def link_tree(source,destination,prefix=''):
   destination.mkdir()
   for item in source.iterdir():
    relative=(prefix+'/'+item.name).lstrip('/');target=destination/item.name
    if relative in changed:
     os.symlink(os.path.relpath(temporary/'patches'/relative,destination),target)
    elif any(p.startswith(relative+'/')for p in changed):
     if not item.is_dir():raise ValueError('UI path is not a directory')
     link_tree(item,target,relative)
    else:os.symlink(str(item),target)
  if asset_view:link_tree(assets,view)
  report={'version':1,'author':'djultra64','source':str(assets),'footer_delta':FOOTER_DY,'footer_nodes':footer_nodes,'intro_fixed':intro_fixed,'archives':changed,'unsupported':unsupported}
  (temporary/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
  seal_tree(temporary)
  temporary.rename(output)
  return report
 except BaseException:
  if temporary.exists():discard_tree(temporary)
  raise

def select_assets(assets,cache):
 assets=Path(assets).resolve();cache=Path(cache).absolute()
 if not cache.exists()and not cache.is_symlink():return assets
 if cache.is_symlink()or not cache.is_dir():raise ValueError('UI cache must be a plain directory')
 report=json.loads((cache/'manifest.json').read_text())
 if report.get('version')!=1 or report.get('source')!=str(assets)or report.get('footer_delta')!=FOOTER_DY or not report.get('intro_fixed'):raise ValueError('UI cache does not match this installation; prepare a new copy')
 known=json.loads((NATIVE/'config/ui_layout_sources.json').read_text())
 if 'Media/Rui/MatchIntro.rui'not in report['archives']or report.get('footer_nodes',0)<12:raise ValueError('UI layout cache is incomplete')
 for relative,record in report['archives'].items():
  if known.get(relative)!=record['original_sha256']:raise ValueError('Unknown UI override')
  patched=cache/'patches'/relative
  if (cache/'view'/relative).resolve()!=patched.resolve()or patched.is_symlink():raise ValueError('UI layout resource path changed: '+relative)
  if sha((assets/relative).read_bytes())!=record['original_sha256']or sha(patched.read_bytes())!=record['patched_sha256']:raise ValueError('UI layout resource changed: '+relative)
 # The view is part of the selected input, including unchanged executables.
 # Validate only expanded directories; all other subtrees stay single links.
 changed=set(report['archives'])
 def validate(source,view,prefix=''):
  if view.is_symlink()or not view.is_dir():raise ValueError('UI view directory changed')
  if {p.name for p in source.iterdir()}!={p.name for p in view.iterdir()}:raise ValueError('UI view inventory changed')
  for item in source.iterdir():
   relative=(prefix+'/'+item.name).lstrip('/');leaf=view/item.name
   if relative in changed:expected=cache/'patches'/relative
   elif any(p.startswith(relative+'/')for p in changed):
    validate(item,leaf,relative);continue
   else:expected=item
   if not leaf.is_symlink()or leaf.resolve()!=expected.resolve():raise ValueError('UI view link changed: '+relative)
 validate(assets,cache/'view')
 return cache/'view'

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--assets',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--overlay-only',action='store_true',help='prepare modified archives without the Linux symlink view');args=parser.parse_args()
 result=prepare(args.assets,args.output,asset_view=False if args.overlay_only else None);print('Prepared',result['footer_nodes'],'footer placements across',len(result['archives']),'archives; match intro ownership fixed; originals unchanged.')
