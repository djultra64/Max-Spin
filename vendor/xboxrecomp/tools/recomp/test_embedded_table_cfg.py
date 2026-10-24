"""Embedded pointers are data, never phantom calls. Own work djultra64."""
import struct
from tools.recomp import config
from tools.recomp.translator import FunctionTranslator
BASE=0x10000

def translator(unknown=False):
 data=bytearray(b'\x90'*0x100)
 data[:7]=b'\xff\x24\x8d'+struct.pack('<I',BASE+0x20)
 arms=[BASE+0xe8,BASE+0xed,BASE+0xf2]
 data[0x20:0x2c]=struct.pack('<III',*arms)
 for arm in arms:data[arm-BASE:arm-BASE+5]=b'\x8b\x45\x08\xc9\xc3'
 if unknown:data[0xe8:0xea]=b'\xff\xe0' # unknown computed continuation
 config._install([config.Section('.text',BASE,len(data),0,len(data),True)],entry_point=BASE,kernel_thunk_addr=BASE,origin='embedded-table-cfg-test')
 db={BASE:{'_addr':BASE,'start':hex(BASE),'end':BASE+len(data),'size':len(data)}}
 return FunctionTranslator(bytes(data),db),db,arms

def test_pointer_words_do_not_generate_calls():
 t,db,arms=translator();code=t.translate_function(BASE,db[BASE])
 assert '/* call ' not in code
 for arm in arms:assert f'loc_{arm:08X}' in code
 assert 'ebp = g_ebp;' in code
 insns,_=t.decode_function(BASE,BASE+0x100)
 assert not any(BASE+0x20<=i.address<BASE+0x2c for i in insns)

def test_unknown_computed_edge_keeps_original_body():
 t,_,_=translator(True);insns,_=t.decode_function(BASE,BASE+0x100)
 # Unknown edges cannot justify pruning any possible continuation.
 assert any(BASE+0x20<=i.address<BASE+0x2c for i in insns)

if __name__=='__main__':
 test_pointer_words_do_not_generate_calls();test_unknown_computed_edge_keeps_original_body()
 print('embedded tables: no phantom calls, frame inheritance, unknown-edge guard PASS')
