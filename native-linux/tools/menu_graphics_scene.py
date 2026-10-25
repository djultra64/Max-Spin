#!/usr/bin/env python3
"""Private native Graphics Options RUI page; no overlay/asset writes. djultra64."""
import argparse,copy,hashlib,json,os,struct,tempfile,zipfile
from pathlib import Path
from menu_pc_scene import patch as patch_pc,encode,_lpm_move
from rui_scene_inventory import decode
VERSION=4
ROWS=(('Resolution',217,245,'resolution'),('Antialiasing',220,246,'antialiasing'),('SpriteFilter',223,247,'sprite_filter'),('FrameLimit',226,248,'frame_limit'),('WindowMode',256,259,'window_mode'),('ControllerPrompts',250,253,'controller_prompts'),('Save',229,None,'save'),('Back',232,None,'back'))
LABELS={'pc_graphics_window_mode':'WINDOW MODE','pc_graphics_value_window_mode':'WINDOWED','pc_graphics_options':'GRAPHICS OPTIONS','pc_quit_game':'QUIT GAME','pc_graphics_title':'GRAPHICS OPTIONS','pc_graphics_resolution':'RESOLUTION','pc_graphics_antialiasing':'ANTIALIASING','pc_graphics_sprite_filter':'SPRITE FILTER','pc_graphics_frame_limit':'FRAME LIMIT','pc_graphics_controller_prompts':'CONTROLLER PROMPTS','pc_graphics_save':'SAVE','pc_graphics_back':'BACK','pc_graphics_status':'CHANGES APPLY ON NEXT START','pc_graphics_value_resolution':'1280 X 720','pc_graphics_value_antialiasing':'ORIGINAL','pc_graphics_value_filter':'ORIGINAL','pc_graphics_value_cap':'60 FPS','pc_graphics_value_family':'AUTO'}
VALUE_IDS={'WindowMode':'pc_graphics_value_window_mode','Resolution':'pc_graphics_value_resolution','Antialiasing':'pc_graphics_value_antialiasing','SpriteFilter':'pc_graphics_value_filter','FrameLimit':'pc_graphics_value_cap','ControllerPrompts':'pc_graphics_value_family'}
PROXIES={'WindowMode':(260,261),'Resolution':(235,236),'Antialiasing':(237,238),'SpriteFilter':(239,240),'FrameLimit':(241,242),'ControllerPrompts':(254,255)}
def find(node,name):
 if node.get('Name')==name:return node
 for child in node.get('Children',{}).values():
  result=find(child,name)
  if result is not None:return result
 return None
def animation_records(raw):
 """Strict shipped v2 clip table; preserves all unknown fields/bytes."""
 at=4;count=struct.unpack_from('<I',raw)[0];records=[]
 if count>128:raise ValueError('Unbounded clips')
 for _ in range(count):
  start=at;duration,nodes=struct.unpack_from('<fI',raw,at);at+=8
  if nodes>16:raise ValueError('Unbounded animated nodes')
  names=[]
  for __ in range(nodes):
   unknown,name,unknown2,channels=struct.unpack_from('<4I',raw,at);names.append((at+4-start,name));at+=16
   if unknown or unknown2 or channels>16:raise ValueError('Unsupported track fields')
   for ___ in range(channels):
    kind,keys=struct.unpack_from('<2I',raw,at);at+=8
    if kind>10 or keys>64:raise ValueError('Unsupported track keys')
    at+=keys*8
    if at>len(raw):raise ValueError('Truncated animation')
  records.append((raw[start:at],names))
 if at!=len(raw):raise ValueError('Animation trailing bytes')
 return records
def clone_page_tracks(animation):
 records=animation_records(animation['ClipsKeyframeData'].encode('latin1'))
 if len(records)!=49 or animation['OrderedNames'][7.]!='optionsrules' or len(animation['OrderedNames'])!=10:raise ValueError('Original animation differs')
 new=[]
 for source,name in [(21,'graphicsrules_on'),(13,'graphicsrules_off')]:
  data,names=records[source]
  if len(names)!=1 or names[0][1]!=6 or len(data)!=184:raise ValueError('Original Options track differs')
  data=bytearray(data);struct.pack_into('<I',data,names[0][0],10);new.append(bytes(data))
  info=copy.deepcopy(next(v for v in animation['ClipsInfo'].values()if v['SourceClip']==source));info['Name']=name;info['SourceClip']=float(49+len(new)-1);animation['ClipsInfo'][float(max(animation['ClipsInfo'])+1)]=info
 animation['OrderedNames'][11.]='graphicsrules'
 animation['ClipsKeyframeData']=(struct.pack('<I',51)+b''.join(r[0]for r in records)+b''.join(new)).decode('latin1')
def _y(node):return struct.unpack_from('<f',node['LPM'].encode('latin1'),4)[0]
def _merge_row(base,instance):
 """Bake shipped MainMenu instance overrides into original Options row."""
 row=copy.deepcopy(base);name=row['Name'];prefix='Menu|option_list|'+name
 row.update(copy.deepcopy(instance.get(prefix,{})))
 child=next(iter(row['Children'].values()));old=child['Name'].split(':')[0]
 for path,attrs in instance.items():
  if path.startswith(prefix+'|'+old+':'):
   relative=path[len(prefix)+1:].replace(old+':','')
   child['ChangedAttributes'].setdefault(relative,{}).update(copy.deepcopy(attrs))
 return row
def patch(layout,events,options):
 original=decode(layout)['SceneData'];original_options=decode(options)['SceneData']
 out=patch_pc(layout,events,options);a,b,o=map(decode,out);m=a['SceneData'];bindings=b['Scenes']['newmain']['Controls'];controls=m['Controls'];actions=m['Actions'];menu=m['Graph']['Children'][2.]
 shipped_instance=original['Graph']['Children'][2.]['Children'][7.]['Children'][1.]['ChangedAttributes']
 group=copy.deepcopy(menu['Children'][7.]);group['Name']='graphicsrules'
 embedded=copy.deepcopy(original_options['Graph']['Children'][1.]);embedded.update(copy.deepcopy(shipped_instance.get('Menu',{})));embedded['Name']='pc_graphics_menu'
 listing=find(embedded,'option_list');listing.update(copy.deepcopy(shipped_instance.get('Menu|option_list',{})))
 original_listing=find(original_options['Graph'],'option_list');base=find(original_listing,'group_credits')
 top=_y(find(original_listing,'group_volume_music'));bottom=_y(base);step=(top-bottom)/len(ROWS)
 listing['Children']={}
 for i,(name,focus,value,text)in enumerate(ROWS):
  row=_merge_row(base,shipped_instance);row['Name']='group_pc_graphics_'+text;_lpm_move(row,top-i*step-_y(row))
  widget=next(iter(row['Children'].values()));widget['Name']='PcGraphicsSelected'+name+':Menu';attrs=widget['ChangedAttributes'];attrs['Menu']['ControlName']=row['Name']
  attrs['Menu|Interface|Color_pht_content|pht_content']['TextID']='pc_graphics_'+text
  attrs['Menu|Interface|Color_pht_setting|pht_setting']['TextID']=VALUE_IDS.get(name,'')
  listing['Children'][float(i+1)]=row
  controls[float(focus)]={'Name':'PcGraphics'+name,'NodeName':widget['Name']}
  controls[float(focus+1)]={'Name':row['Name'],'NodeName':widget['Name']}
  controls[float(focus+2)]={'Name':row['Name']+'|SelectedText','NodeName':widget['Name']}
  if value:controls[float(value)]={'Name':'PcGraphics'+name+'Value','NodeName':'PcGraphicsSelected'+name+':pht_setting'};bindings[float(value)]={}
  up=ROWS[(i-1)%len(ROWS)][0];down=ROWS[(i+1)%len(ROWS)][0]
  event={'Up':"$PcGraphicsSelection_%s('MoveUp')"%up,'Down':"$PcGraphicsSelection_%s('MoveDown')"%down,'BButton':'$PcGraphicsBack','Back':'$PcGraphicsBack'}
  if name in PROXIES:
   minus,plus=PROXIES[name]
   for direction,index in [('Minus',minus),('Plus',plus)]:
    proxy='PcGraphics'+name+direction;controls[float(index)]={'Name':proxy,'NodeName':widget['Name']};bindings[float(index)]={'BButton':'$PcGraphicsBack','Back':'$PcGraphicsBack'}
    actions[proxy]={1.:{1.:"SetActiveControl('%s')"%proxy}}
   event.update(Left='$PcGraphics'+name+'Minus',Right='$PcGraphics'+name+'Plus',AButton='$PcGraphics'+name+'Plus',Start='$PcGraphics'+name+'Plus')
  else:event.update(AButton='$PcGraphics'+name,Start='$PcGraphics'+name)
  bindings[float(focus)]=event;bindings[float(focus+1)]={};bindings[float(focus+2)]={}
  actions['PcGraphicsSelection_'+name+'(sound)']={1.:{1.:"PlaySound('@sound@')"},2.:{1.:'InputDelayOn()'},3.:{1.:"SetActiveControl('PcGraphics%s')"%name,2.:"this.HighlightSelected('%s', 'text')"%row['Name']}}
 # Original row templates/styles, with one passive status line and header.
 status=_merge_row(base,shipped_instance);status['Name']='group_pc_graphics_status';_lpm_move(status,top-len(ROWS)*step-_y(status));sw=next(iter(status['Children'].values()));sw['Name']='PcGraphicsStatusRow:Menu';sw['ChangedAttributes']['Menu']['ControlName']='group_pc_graphics_status';sw['ChangedAttributes']['Menu|Interface|Color_pht_content|pht_content']['TextID']='pc_graphics_status';sw['ChangedAttributes']['Menu|Interface|Color_pht_setting|pht_setting']['TextID']='';listing['Children'][float(len(ROWS)+1)]=status
 controls[249.]={'Name':'PcGraphicsStatus','NodeName':'PcGraphicsStatusRow:pht_content'};bindings[249.]={}
 group['Children']={1.:embedded};menu['Children'][float(max(menu['Children'])+1)]=group
 clone_page_tracks(m['AnimationData'])
 for action in ['Init','InitFromSubMenu','Init_FromLive','Init_FromPC']:
  for phase in actions[action].values():
   if 'optionsrules_off'in phase.values():phase[float(max(phase)+1)]='graphicsrules_off';break
 actions['Validation_PcGraphics']={1.:{1.:"PlaySound('Validate')"},2.:{1.:'$PcGraphicsEnter'}}
 actions['PcGraphicsEnter']={1.:{1.:'optionsrules_off',2.:'graphicsrules_on'},2.:{1.:"$PcGraphicsSelection_Resolution('NoSound')"}}
 actions['PcGraphicsBack']={1.:{1.:'graphicsrules_off',2.:'optionsrules_on'},2.:{1.:"$Selection_PcGraphics('NoSound')"}}
 actions['PcGraphicsSave']={1.:{1.:"PlaySound('Validate')"},2.:{1.:"SetActiveControl('PcGraphicsSaveRequest')"}}
 controls[243.]={'Name':'PcGraphicsSaveRequest','NodeName':'PcGraphicsSelectedSave:Menu'};bindings[243.]={'BButton':'$PcGraphicsBack','Back':'$PcGraphicsBack'}
 # Back244 stays available as an explicit host-request proxy; direct B uses
 # only shipped animation/selection actions and needs no host execution.
 controls[244.]={'Name':'PcGraphicsBackRequest','NodeName':'PcGraphicsSelectedBack:Menu'};bindings[244.]={'BButton':'$PcGraphicsBack','Back':'$PcGraphicsBack'}
 # Header is an ordinary original Text node in the embedded graph, not host UI.
 with_header=copy.deepcopy(next(v for v in original['Graph']['Children'][2.]['Children'][3.]['Children'].values()if v.get('Name')=='pht_options'))
 with_header['Name']='pht_pc_graphics_title';with_header['TextID']='pc_graphics_title'
 # Position above the original Options list, following its own compressed LPM.
 _lpm_move(with_header,top+step-_y(with_header));embedded['Children'][float(max(embedded['Children'])+1)]=with_header
 # Original NodeTypes is an exact population table used by the RUI loader.
 # Account for embedded nodes and the inherited v2 Quit entry as well.
 population={}
 def count(node):
  if 'Type'in node:
   flags=node.get('Flags',0.);kind=node.get('ClassType')or node['Type'];bucket=population.setdefault(flags,{})
   bucket[kind]=bucket.get(kind,0.)+1.
  for child in node.get('Children',{}).values():count(child)
 count(m['Graph']);m['NodeTypes']=population
 return encode(layout,a),encode(events,b),encode(options,o)
def contract():
 return dict(version=VERSION,author='djultra64',labels=LABELS,rows=[dict(name=n,focus=f,value=v,textid='pc_graphics_'+t,minus=PROXIES.get(n,(None,None))[0],plus=PROXIES.get(n,(None,None))[1])for n,f,v,t in ROWS],status=dict(control=249,name='PcGraphicsStatus',limit=39),save_proxy=243,back_proxy=244,resources=['newmain.sceneData','newmain.scene','main_optionrules.sceneData'])
def resource_header(chunks):
 """Reproduce compiled guards from the three exact generated chunks."""
 chunks=tuple(chunks)
 if len(chunks)!=3 or any(type(data)is not bytes or not data for data in chunks):raise ValueError('Expected three nonempty immutable resource byte strings')
 def fnv(data):
  value=14695981039346656037
  for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
  return value
 lines=['/* Authenticated private native RUI v4 resources. Own work: djultra64. */',
 '#ifndef TOPSPIN_MENU_GRAPHICS_RESOURCES_H','#define TOPSPIN_MENU_GRAPHICS_RESOURCES_H',
 'static const char*private_sha[]={'+','.join('"'+hashlib.sha256(data).hexdigest()+'"'for data in chunks)+'};',
 'static const uint32_t private_size[]={'+','.join(str(len(data))for data in chunks)+'};',
 'static const uint64_t private_hash[]={'+','.join('UINT64_C(0x%016x)'%fnv(data)for data in chunks)+'};','#endif','']
 return '\n'.join(lines).encode('ascii')
def prepare(package,directory):
 with zipfile.ZipFile(package)as z:raw=[z.read(p)for p in ['Scenes/newmain.sceneData','Scenes/newmain.scene','Templates/main_optionrules.sceneData']]
 output=patch(*raw);dst=Path(directory);dst.mkdir(mode=0o700,parents=False);created=[]
 try:
  for name,data in zip(contract()['resources'],output):
   p=dst/name;fd=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o600);created.append(p)
   with os.fdopen(fd,'wb')as f:f.write(data);f.flush();os.fsync(f.fileno())
   p.chmod(0o444)
  manifest=contract();manifest['sha256']={n:hashlib.sha256(d).hexdigest()for n,d in zip(manifest['resources'],output)}
  p=dst/'manifest.json';p.write_text(json.dumps(manifest,indent=2)+'\n');created.append(p);p.chmod(0o444);dst.chmod(0o555)
 except BaseException:
  dst.chmod(0o700)
  for p in created:
   if not p.is_symlink():p.chmod(0o600)
   p.unlink()
  dst.rmdir();raise
 return output
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--header',type=Path,help='optional NEW header outside the immutable cache; existing files reject');a=p.parse_args()
 if a.header and (a.output.resolve()==a.header.resolve() or a.output.resolve()in a.header.resolve().parents):p.error('--header must be outside the cache')
 if a.header and a.header.exists():p.error('--header output already exists')
 chunks=prepare(a.package,a.output)
 if a.header:
  fd=os.open(a.header,os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o644)
  with os.fdopen(fd,'wb')as stream:stream.write(resource_header(chunks))
