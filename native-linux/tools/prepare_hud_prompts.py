#!/usr/bin/env python3
"""NEW private HUDv1 plus existing extended9 cache; originals untouched. djultra64."""
from pathlib import Path
import argparse,hashlib,json,os,struct,shutil

def prepare(assets,base,out):
 if out.exists()or out.is_symlink():raise ValueError('new private destination required')
 if base.is_symlink()or not base.is_dir():raise ValueError('plain existing prompt cache required')
 metadata=json.loads((base/'glyph-art.json').read_text())
 if metadata.get('version')!=4 or metadata.get('image')not in('game','training'):raise ValueError('extended9 Game/Training cache required')
 rows=(base/'manifest.txt').read_text().splitlines();records=[];payloads=[]
 for lang in ('Eng','Fre','Ger'):
  path=assets/f'Media/Xpr/Hud{lang}.xpr';b=path.read_bytes()
  if len(b)<16 or b[:4]!=b'XPR1' or struct.unpack_from('<I',b,4)[0]!=len(b):raise ValueError('original XPR1 bounds')
  header=struct.unpack_from('<I',b,8)[0];count=struct.unpack_from('<I',b,12)[0]
  if not 0<count<=256 or not 16+8*count<=header<=len(b):raise ValueError('original directory bounds')
  found=[]
  for i in range(count):
   no,ho=struct.unpack_from('<II',b,16+i*8);no+=12;ho+=12
   if not 16+8*count<=no<header or ho+20>header:raise ValueError('original entry bounds')
   end=b.find(b'\0',no,header)
   if end<0:raise ValueError('original name bounds')
   if b[no:end]!=b'd:\\Media\\HUDTextures\\Rule\\Attitude.dds':continue
   common,data,lock,fmt,size=struct.unpack_from('<5I',b,ho);levels=(fmt>>16)&15
   if common!=0x40001 or lock or size or fmt not in(0x06710f29,0x06780e29):raise ValueError('unknown original Attitude descriptor')
   n=sum(((max(1,128>>l)+3)//4)*((max(1,64>>l)+3)//4)*16 for l in range(levels))
   if data+n>len(b)-header:raise ValueError('original pixels bounds')
   original=b[header+data:header+data+n];xpr=struct.pack('<8I',0x30525058,32+n,32,common,0,0,fmt,0)+original;name=f'Hud{lang}_Attitude.xpr'
   found.append((name,xpr));records.append(dict(path=str(path.relative_to(assets)),resource='d:\\Media\\HUDTextures\\Rule\\Attitude.dds',source_sha256=hashlib.sha256(b).hexdigest(),entry=i,descriptor_offset=ho,payload_sha256=hashlib.sha256(original).hexdigest(),format=fmt,mip_levels=levels))
  if len(found)!=1:raise ValueError('one exact original HUD entry required')
  payloads+=found
 # Validate before creating anything; copy only manifest-referenced plain files.
 files={'manifest.txt','glyph-art.json'}
 for row in rows:
  fields=row.split()
  if len(fields)!=3 or fields[0]not in('F','S'):raise ValueError('base manifest')
  files.update(fields[1:]if fields[0]=='F'else fields[2:])
 for name in files:
  if Path(name).name!=name or (base/name).is_symlink()or not(base/name).is_file():raise ValueError('base plain files only')
 out.mkdir(parents=True,mode=0o700)
 for name in files:shutil.copyfile(base/name,out/name);(out/name).chmod(0o600)
 for name,data in payloads:(out/name).write_bytes(data);(out/name).chmod(0o600);rows.append('H 0 '+name)
 (out/'manifest.txt').write_text('\n'.join(rows)+'\n')
 metadata.update(hud_cache_version=1,hud_sources=records);(out/'glyph-art.json').write_text(json.dumps(metadata,indent=2)+'\n')
 return metadata
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();prepare(a.assets,a.base,a.output)
