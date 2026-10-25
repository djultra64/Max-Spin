"""SDK source and patches have one UTF-8 encoding on every host. djultra64."""
import importlib.util,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sdk_prepare',ROOT/'windows/native/prepare_sdk.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
real_read=Path.read_text;real_write=Path.write_text
# Reproduce native Windows Python's cp1252 default on a UTF-8 Linux host.
def read(path,*args,**kwargs):
 kwargs.setdefault('encoding','cp1252');return real_read(path,*args,**kwargs)
def write(path,*args,**kwargs):
 kwargs.setdefault('encoding','cp1252');return real_write(path,*args,**kwargs)
with tempfile.TemporaryDirectory()as directory:
 root=Path(directory);source=root/'source.c';patch=root/'fix.patch'
 source.write_bytes('/* \u2190 original comment */\nold\n'.encode('utf-8'))
 patch.write_bytes(b'--- a/source.c\n+++ b/source.c\n@@ -2,1 +2,1 @@\n-old\n+new\n')
 Path.read_text=read;Path.write_text=write
 try:module.apply_patch(root,patch)
 finally:Path.read_text=real_read;Path.write_text=real_write
 assert source.read_bytes()=='/* \u2190 original comment */\nnew\n'.encode('utf-8')
print('PASS SDK UTF-8 comments survive cp1252 host defaults with exact patch contexts')
