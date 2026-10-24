#!/usr/bin/env python3
"""Build private, hash-scoped MainMenu PC RUI chunks; own work: djultra64.
Original assets are read-only. This does not execute Lua or launch the title.
"""
import argparse, copy, hashlib, math, os, struct, tempfile, zipfile
from pathlib import Path
from rui_scene_inventory import Reader, decode
HASHES = {'layout': '6b839099983d0a740fa72cf610684efac9635ab2817126fc6e20c70f5d1f9abe',
          'events': '1699673e360194572e600268176a283606fdc4e92bd834d8ff665e08ebdc7d92',
          'options': '6fb70803608f36957b5e48612683fc45a573e303abba5a10738d4015ee3f7720'}
LABELS = {'pc_graphics_options': 'Graphics Options', 'pc_quit_game': 'QUIT GAME'}

def string(value):
    if value is None:return struct.pack('<I',0)
    data=value.encode('latin1')+b'\0'
    return struct.pack('<I',len(data))+data

def encode(original, tables):
    """Rebuild only declarative tables using registers, never RK constants.

    Constant indices can exceed RK's261 range. Using LOADK for both operands
    avoids silently changing keys once the constant pool grows.
    """
    p=Reader(original).prototype();code=[];constants=[];indices={};high=0;seen=set()
    def constant(value):
        if value is None:key=(0,b'')
        elif isinstance(value,str):key=(4,value.encode('latin1'))
        elif isinstance(value,(int,float)) and not isinstance(value,bool):
            if not math.isfinite(value):raise ValueError('Nonfinite numeric literal')
            key=(3,struct.pack('<d',float(value)))
        else:raise ValueError('Unsupported literal')
        if key not in indices:
            if len(constants)>=262144:raise ValueError('Constant limit')
            indices[key]=len(constants);constants.append((key,value))
        return indices[key]
    def emit(op,a,b=0,c=0,bx=None):
        nonlocal high
        if not 0<=a<250:raise ValueError('Register limit')
        high=max(high,a+1)
        if len(code)>=200000:raise ValueError('Instruction limit')
        code.append(op|(a<<24)|((b<<15)|(c<<6) if bx is None else bx<<6))
    def value(v,r):
        if isinstance(v,dict):
            if id(v) in seen:raise ValueError('Shared/cyclic declarative tables require an explicit alias contract')
            seen.add(id(v))
            emit(10,r)
            for k,w in v.items():
                if not isinstance(k,(str,int,float)) or isinstance(k,bool):raise ValueError('Invalid key')
                value(k,r+1);value(w,r+2);emit(9,r,r+1,r+2)
        elif isinstance(v,bool):emit(2,r,int(v),0)
        else:emit(1,r,bx=constant(v))
    if set(tables)=={'Scenes','SceneData'} and not tables['Scenes']:
        value(tables['SceneData'],0);emit(7,0,bx=constant('SceneData'))
    elif set(tables)=={'Scenes'}:
        emit(5,0,bx=constant('Scenes'))
        for name,scene in tables['Scenes'].items():
            value(name,1);value(scene,2);emit(9,0,1,2)
    else:raise ValueError('Unexpected global schema')
    emit(27,0,1,0)
    out=bytearray(original[:22]);out+=string(p['source'])+struct.pack('<I',p['line'])
    out+=bytes((0,0,p['vararg'],max(2,high)))
    out+=struct.pack('<III',0,0,len(constants))
    for (kind,raw),v in constants:
        out.append(kind)
        if kind==3:out+=raw
        elif kind==4:out+=string(v)
    out+=struct.pack('<II',0,len(code))+struct.pack('<'+'I'*len(code),*code)
    if decode(out)!=tables:raise ValueError('Encoded declarative roundtrip differs')
    return bytes(out)

def _lpm_move(node,dy):
    b=node['LPM'].encode('latin1')
    if len(b) not in (12,48):raise ValueError('Unknown original compressed LPM')
    y=struct.unpack_from('<f',b,4)[0]
    node['LPM']=(b[:4]+struct.pack('<f',y+dy)+b[8:]).decode('latin1')

def patch(layout,events,options):
    if any(hashlib.sha256(raw).hexdigest()!=HASHES[key] for raw,key in zip((layout,events,options),('layout','events','options'))):
        raise ValueError('Unrecognized original MainMenu chunks')
    a=decode(layout);b=decode(events);o=decode(options);m=a['SceneData'];e=b['Scenes']['newmain']
    controls=m['Controls'];bindings=e['Controls'];actions=m['Actions']
    if set(controls)!=set(float(i) for i in range(1,209)):raise ValueError('Original control population')
    main=m['Graph']['Children'][2.0]['Children'][3.0]['Children'];byname={n['Name']:n for n in main.values()}
    roots=['pht_exhibition','pht_custom_tournament','pht_career_mode','pht_live','pht_tuto_tennisschool','pht_options']
    groups=['group_exhibition','group_custom_tournament','group_career_mode','group_live','group_tuto_tennisschool','group_options']
    top=struct.unpack_from('<f',byname[groups[0]]['LPM'].encode('latin1'),4)[0]
    bottom=struct.unpack_from('<f',byname[groups[-1]]['LPM'].encode('latin1'),4)[0];spacing=(top-bottom)/6
    for i,(root,group) in enumerate(zip(roots,groups)):
        old=struct.unpack_from('<f',byname[group]['LPM'].encode('latin1'),4)[0];delta=top-i*spacing-old
        _lpm_move(byname[group],delta);_lpm_move(byname[root],delta)
    root=copy.deepcopy(byname['pht_options']);root['Name']='pht_pc_quit';root['TextID']='pc_quit_game'
    group=copy.deepcopy(byname['group_options']);group['Name']='group_pc_quit'
    _lpm_move(root,-spacing);_lpm_move(group,-spacing)
    template=next(iter(group['Children'].values()));template['Name']='PcSelected_quit:Menu'
    template['ChangedAttributes']['Menu']['ControlName']=group['Name']
    template['ChangedAttributes']['Menu|Interface|Color_pht_content|pht_content']['TextID']='pc_quit_game'
    n=int(max(main))+1;main[float(n)]=root;main[float(n+1)]=group
    controls[213.]={'Name':'PcQuitGame','NodeName':root['Name']}
    controls[214.]={'Name':group['Name'],'NodeName':template['Name']}
    controls[215.]={'Name':group['Name']+'|SelectedText','NodeName':template['Name']}
    controls[216.]={'Name':'PcQuitRequest','NodeName':root['Name']}
    bindings[213.]={'Up':"$Selection_Options('MoveUp')",'Down':"$Selection_Exhibition('MoveDown')",'AButton':'$Validation_PcQuit','Start':'$Validation_PcQuit','BButton':'$BackToStartScreen','Back':'$BackToStartScreen'}
    bindings[214.]={};bindings[215.]={};bindings[216.]={'BButton':"$Selection_PcQuit('NoSound')",'Back':"$Selection_PcQuit('NoSound')"}
    bindings[159.]['Down']="$Selection_PcQuit('MoveDown')";bindings[148.]['Up']="$Selection_PcQuit('MoveUp')"
    actions['Selection_PcQuit(sound)']={1.:{1.:"PlaySound('@sound@')"},2.:{1.:"SetActiveControl('PcQuitGame')",2.:"this.HighlightSelected('group_pc_quit', 'text')"}}
    actions['Validation_PcQuit']={1.:{1.:"PlaySound('Validate')"},2.:{1.:"SetActiveControl('PcQuitRequest')"}}
    # Same Scene hosts Options. Keep its namespace/template boundary intact;
    # only the original main_optionrules template gets one additional row.
    def find(node,name):
        if node.get('Name')==name:return node
        for child in node.get('Children',{}).values():
            result=find(child,name)
            if result is not None:return result
        return None
    option_root=o['SceneData']['Graph'];listing=find(option_root,'option_list')
    rownames=['group_volume_music','group_volume_fx','group_volume_referee','group_volume_crowd','group_speed_unit','group_vibration','group_reset','group_credits']
    rows={name:find(listing,name)for name in rownames}
    if any(v is None for v in rows.values()):raise ValueError('Original Options rows differ')
    graphic=copy.deepcopy(rows['group_credits']);graphic['Name']='group_pc_graphics'
    gt=next(iter(graphic['Children'].values()));gt['Name']='PcGraphicsSelected:Menu'
    gt['ChangedAttributes']['Menu']['ControlName']='group_pc_graphics'
    gt['ChangedAttributes']['Menu|Interface|Color_pht_content|pht_content']['TextID']='pc_graphics_options'
    # No stale SHOW value beside the host panel entry.
    gt['ChangedAttributes']['Menu|Interface|Color_pht_setting|pht_setting']['TextID']=''
    listing['Children'][float(max(listing['Children'])+1)]=graphic
    first_y=struct.unpack_from('<f',rows[rownames[0]]['LPM'].encode('latin1'),4)[0]
    last_y=struct.unpack_from('<f',rows[rownames[-1]]['LPM'].encode('latin1'),4)[0];step=(first_y-last_y)/8
    instance=m['Graph']['Children'][2.]['Children'][7.]['Children'][1.]
    if instance.get('TemplateName')!='main_optionrules':raise ValueError('Original Options instance differs')
    for i,node in enumerate([rows[x]for x in rownames]+[graphic]):
        y=first_y-i*step;old=struct.unpack_from('<f',node['LPM'].encode('latin1'),4)[0]
        _lpm_move(node,y-old)
        override=instance['ChangedAttributes'].get('Menu|option_list|'+node['Name'])
        if override is not None:
            old=struct.unpack_from('<f',override['LPM'].encode('latin1'),4)[0];_lpm_move(override,y-old);override['translateY']=y
    node_name='main_optionrules:'+gt['Name']
    controls[209.]={'Name':'PcGraphicsOptions','NodeName':node_name}
    controls[210.]={'Name':'group_pc_graphics','NodeName':node_name}
    controls[211.]={'Name':'group_pc_graphics|SelectedText','NodeName':node_name}
    controls[212.]={'Name':'PcGraphicsPanel','NodeName':node_name}
    bindings[209.]={'Up':"$Options_Selection_Credits('MoveUp')",'Down':"$Options_Selection_VolumeMusic('MoveDown')",'AButton':'$Validation_PcGraphics','Start':'$Validation_PcGraphics','BButton':'$AskSaveOptions','Back':'$AskSaveOptions'}
    bindings[210.]={};bindings[211.]={};bindings[212.]={'BButton':"$Selection_PcGraphics('NoSound')",'Back':"$Selection_PcGraphics('NoSound')"}
    bindings[90.]['Down']="$Selection_PcGraphics('MoveDown')";bindings[194.]['Up']="$Selection_PcGraphics('MoveUp')"
    actions['Selection_PcGraphics(sound)']={1.:{1.:"PlaySound('@sound@')"},2.:{1.:'InputDelayOn()'},3.:{1.:"SetActiveControl('PcGraphicsOptions')",2.:"this.HighlightSelected('group_pc_graphics', 'text')"}}
    actions['Validation_PcGraphics']={1.:{1.:"PlaySound('Validate')"},2.:{1.:"SetActiveControl('PcGraphicsPanel')"}}
    return encode(layout,a),encode(events,b),encode(options,o)

def prepare(package,directory):
    with zipfile.ZipFile(package) as z:
        layout=z.read('Scenes/newmain.sceneData');events=z.read('Scenes/newmain.scene');options=z.read('Templates/main_optionrules.sceneData')
    out=patch(layout,events,options)
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    for name,data in zip(('newmain.sceneData','newmain.scene','main_optionrules.sceneData'),out):
        with tempfile.NamedTemporaryFile(dir=directory,prefix='.pc-menu-',delete=False) as f:
            tmp=f.name;f.write(data)
        os.replace(tmp,directory/name)
    return out

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=prepare(a.package,a.output)
    print('Private PC chunks prepared:',*(len(b) for b in out))
