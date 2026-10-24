#!/usr/bin/env python3
"""Prepare private original-resource inputs for own Menu glyph projection.
Requires the user's original assets. Never edits them; outputs are private
build cache, not redistributable resource replacements. djultra64.
"""
from pathlib import Path
import argparse,os,zipfile,json,hashlib,struct,zlib

def prepare(assets,out):
 out.mkdir(parents=True,exist_ok=True);out.chmod(0o700);rows=[]
 with zipfile.ZipFile(assets/'Media/MenuPart/Rui/Base.rui')as abc,zipfile.ZipFile(assets/'Media/MenuPart/Rui/Base.ruirr')as xpr:
  # First Menu prototype: footer and ordinary help/dialog font, plus the
  # original independent face-button sprites. Other fonts fail to match and
  # retain their original resources; no arbitrary texture is retagged.
  for font in ['Help_NovaBlackSSK','NovaBlackSSK_14']:
   for extension,archive in [('abc',abc),('xpr',xpr)]:
    path=out/(font+'.'+extension);data=archive.read('Fonts/'+path.name)
    with os.fdopen(os.open(path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600),'wb')as f:f.write(data)
   rows.append('F '+font+'.abc '+font+'.xpr')
  for index,name in enumerate(['AButton','BButton','XButton','YButton']):
   path=out/(name+'.xpr')
   with os.fdopen(os.open(path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600),'wb')as f:f.write(xpr.read('Textures/'+path.name))
   rows.append(f'S {index} {path.name}')
 path=out/'manifest.txt'
 with os.fdopen(os.open(path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600),'w')as f:f.write('\n'.join(rows)+'\n')
 art=Path(__file__).resolve().parents[1]/'assets/controller-prompts-xelu/provenance.json'
 provenance=json.loads(art.read_text())
 metadata={'version':2,'art':'Xelu Free Controller Prompts','license':provenance['license'],'archive_sha256':provenance['archive_sha256'],'provenance_sha256':hashlib.sha256(art.read_bytes()).hexdigest()}
 (out/'glyph-art.json').write_text(json.dumps(metadata,indent=2)+'\n');(out/'glyph-art.json').chmod(0o600)
 print(f'Prepared {len(rows)} private Menu resource inputs; originals unchanged')

# Exact original payload identities, not a general ignore-CRC archive mode.
KNOWN_CRC_PAYLOADS={
 ('Media/Rui/Base.ruirr','Fonts/NovaBlackSSK_24_Buttons.xpr'):'38ae721e7aeebad66ffa0feb898a93eba424c6fbdf6f4868d18895a7a5f5249f',
 ('Media/PlayerCreatorMenuPart/Rui/Base.ruirr','Fonts/Help_NovaBlackSSK.xpr'):'338164289af50e693defb3ca0d74f32806fae20aa3ed675a293a7aea658c05a5',
 ('Media/PlayerCreatorMenuPart/Rui/Base.ruirr','Fonts/NovaBlackSSK_14.xpr'):'1f3f7e04310c14704d1e8b7b85080d384a5d1281469f6c474956c9222a7d19b6',
 ('Media/Rui/Base.ruirr','Fonts/Help_NovaBlackSSK.xpr'):'338164289af50e693defb3ca0d74f32806fae20aa3ed675a293a7aea658c05a5',
 ('Media/Rui/Base.ruirr','Fonts/NovaBlackSSK_14.xpr'):'e7cb801db3d3d4b8111ece6811e3215ca3111680a1f59f162cdad4bc12a82b35',
}
IMAGES=('menu','game','training','career','playercreator','shop','online')
def original_entry(assets,packet,name):
 with zipfile.ZipFile(assets/packet)as z:
  try:return z.read(name),True
  except zipfile.BadZipFile:
   expected=KNOWN_CRC_PAYLOADS.get((packet,name))
   if not expected:raise
   info=z.getinfo(name);source=(assets/packet).read_bytes();header=source[info.header_offset:info.header_offset+30]
   if len(header)!=30 or header[:4]!=b'PK\x03\x04' or info.flag_bits&1:raise ValueError('unsupported original entry')
   nl,el=struct.unpack_from('<HH',header,26);start=info.header_offset+30+nl+el;encoded=source[start:start+info.compress_size]
   data=encoded if info.compress_type==0 else zlib.decompress(encoded,-15)if info.compress_type==8 else None
   if data is None or len(data)!=info.file_size or hashlib.sha256(data).hexdigest()!=expected:raise ValueError('original payload identity mismatch')
   return data,False

def prepare_image(assets,out,image):
 if image not in IMAGES:raise ValueError('unsupported prompt image')
 out.mkdir(parents=True,exist_ok=True);out.chmod(0o700);rows=[];sources=[];seen=set()
 # Exact RUI search roots are present in each original XBE. Creator has
 # both its own and Menu roots; identical templates are deduplicated.
 # Matching still requires original class+header+all payload bytes.
 parts={'menu':('MenuPart/Rui',),'shop':('MenuPart/Rui',),'playercreator':('PlayerCreatorMenuPart/Rui','MenuPart/Rui'),'career':('CareerPart/Rui',),'game':('Rui',),'training':('Rui',),'online':('Rui',)}
 for part in parts[image]:
  prefix='Media/'+part+'/Base';tag=part.split('/')[0]
  for font in ('Help_NovaBlackSSK','NovaBlackSSK_14','NovaBlackSSK_24_Buttons'):
   try:
    abc,abc_crc=original_entry(assets,prefix+'.rui','Fonts/'+font+'.abc')
    xpr,xpr_crc=original_entry(assets,prefix+'.ruirr','Fonts/'+font+'.xpr')
   except KeyError:continue
   key=('F',hashlib.sha256(abc).hexdigest(),hashlib.sha256(xpr).hexdigest())
   if key in seen:continue
   seen.add(key);a=tag+'_'+font+'.abc';b=tag+'_'+font+'.xpr'
   for name,data in ((a,abc),(b,xpr)):
    with os.fdopen(os.open(out/name,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600),'wb')as f:f.write(data)
   rows.append('F '+a+' '+b);sources.append({'kind':'font','part':prefix,'font':font,'abc_sha256':key[1],'xpr_sha256':key[2],'zip_crc_matches':[abc_crc,xpr_crc]})
  for index,button in enumerate('ABXY'):
   try:xpr,crc=original_entry(assets,prefix+'.ruirr','Textures/'+button+'Button.xpr')
   except KeyError:continue
   sha=hashlib.sha256(xpr).hexdigest();key=('S',index,sha)
   if key in seen:continue
   seen.add(key);name=tag+'_'+button+'Button.xpr'
   with os.fdopen(os.open(out/name,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600),'wb')as f:f.write(xpr)
   rows.append('S '+str(index)+' '+name);sources.append({'kind':'sprite','part':prefix,'slot':index,'xpr_sha256':sha,'zip_crc_matches':[crc]})
 if not rows or len(rows)>16:raise ValueError('bounded template count exceeded')
 art=Path(__file__).resolve().parents[1]/'assets/controller-prompts-xelu/provenance.json'
 metadata={'version':3,'image':image,'author':'djultra64','sources':sources,'guest_pin_budget':8388608 if image in ('game','training','online') else 4194304,'art_provenance_sha256':hashlib.sha256(art.read_bytes()).hexdigest()}
 for name,data in [('manifest.txt','\n'.join(rows)+'\n'),('glyph-art.json',json.dumps(metadata,indent=2)+'\n')]:
  with os.fdopen(os.open(out/name,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600),'w')as f:f.write(data)
 print(f'Prepared {image}: {len(rows)} immutable original templates; cache private, bounded runtime pinning')

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--image',choices=IMAGES);a=p.parse_args()
 if a.image:prepare_image(a.assets,a.output,a.image)
 else:prepare(a.assets,a.output)
