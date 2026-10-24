#!/usr/bin/env python3
"""Prepare a NEW private9glyph template cache. Originals immutable. djultra64.
Requires existing prepare_menu_prompts.py and local verified Xelu CC0 art.
Refuses any existing destination; no production cache can be silently changed.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os
EXTRA=['XboxSeriesX_Right_Stick','XboxSeriesX_View','XboxSeriesX_Menu','PS5_Right_Stick','PS5_Share','PS5_Options','Switch_Right_Stick','Switch_Minus','Switch_Plus']
def prepare(assets,out,image,original_tool,art):
 if out.exists() or out.is_symlink():raise ValueError('destination must be a new private directory')
 # Validate all optional artwork before creating any output.
 prov=json.loads((art/'provenance.json').read_text());known={x['file']:x for x in prov['files']};records=[]
 for name in EXTRA:
  file=name+'.png';data=(art/file).read_bytes();assert hashlib.sha256(data).hexdigest()==known[file]['sha256'];records.append(known[file])
 spec=importlib.util.spec_from_file_location('original_prompt_preparer',original_tool);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
 if image not in mod.IMAGES:raise ValueError('unknown prompt image')
 mod.prepare_image(assets,out,image)
 meta=out/'glyph-art.json';value=json.loads(meta.read_text());value.update(version=4,extra_art=records,extra_codepoints={'rotate':'212A','back':'212B','start':'212C'},rotate_consumer_verified=image in ('playercreator','shop'),applied_source_slots=9)
 # Only new files owned by this invocation are replaced; original source font
 # bytes remain exact and are still verified by the existing matcher.
 meta.write_text(json.dumps(value,indent=2)+'\n');meta.chmod(0o600)
 return value
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--image',required=True);p.add_argument('--original-tool',required=True,type=Path);p.add_argument('--art',required=True,type=Path);a=p.parse_args()
 prepare(a.assets,a.output,a.image,a.original_tool,a.art)
