"""Biased CRT tail tables, synthetic code only. Own work djultra64."""
import struct
from tools.disasm.test_returning_body_switch import BASE, _engine

def fixture(bias):
    table=BASE+16;arms=(BASE+28,BASE+33,BASE+38)
    data=b'\xff\x24\x8d'+struct.pack('<I',table+bias*4)
    data+=b'\x90'*(16-len(data))+struct.pack('<III',*arms)
    data+=b'\x8b\x45\x08\xc9\xc3'*3
    return data+b'\xcc'*32,table,arms

def test_base_one_slot_before_real_table():
    data,table,arms=fixture(-1);engine,_=_engine(data)
    assert engine.jump_table_entries(table-4)==list(arms)
    assert engine.jump_tables.get(table)==table+12
    for address in arms:assert engine.get_instruction(address).mnemonic=='mov'

def test_base_one_slot_after_real_table():
    data,table,arms=fixture(3);engine,_=_engine(data)
    assert engine.jump_table_entries(table+12)==list(arms)
    assert engine.jump_tables.get(table)==table+12
    assert engine.get_instruction(arms[0]).mnemonic=='mov'

if __name__=='__main__':
    test_base_one_slot_before_real_table();test_base_one_slot_after_real_table()
    print('biased jump tables: before/after-base and genuine arm decode PASS')


def test_scalar_words_and_outside_section_pointers_are_not_tables():
    for values in ((0,1,2),(BASE+0x10000,BASE+0x10001,BASE+0x10002)):
        data,table,_=fixture(-1);data=bytearray(data)
        data[16:28]=struct.pack('<III',*values)
        engine,_=_engine(bytes(data))
        assert not engine.jump_tables

def test_data_section_is_not_destructively_resynchronized():
    data,table,_=fixture(-1);engine,image=_engine(data)
    image.section.executable=False
    engine.jump_tables.clear();engine._jt_base.clear()
    engine.resync_jump_tables()
    assert not engine.jump_tables

def test_valid_slot_zero_stays_in_the_table():
    data,table,arms=fixture(0);engine,_=_engine(data)
    assert engine.jump_table_entries(table)==list(arms)
    assert engine.jump_tables.get(table)==table+12


def test_neighboring_function_table_is_not_owned_by_first_function():
    from tools.disasm.functions import FunctionDetector
    from tools.disasm.labels import LabelManager
    from tools.disasm.xrefs import XRefTracker
    data,table,arms=fixture(-1)
    second=BASE+len(data)
    # A separate ordinary function with a visible prologue and its own table.
    table2=second+16;arms2=(second+28,second+33,second+38)
    following=b'\x55\x8b\xec\xff\x24\x8d'+struct.pack('<I',table2)
    following+=b'\x90'*(16-len(following))+struct.pack('<III',*arms2)
    following+=b'\x8b\x45\x08\xc9\xc3'*3+b'\xcc'*16
    engine,image=_engine(data+following)
    detector=FunctionDetector(engine,image,XRefTracker(),LabelManager())
    detector.detect_all([image.section])
    assert detector.functions[BASE].end<=second
    assert detector.functions[second].end>=arms2[-1]+5
    assert engine.jump_table_entries(table-4)==list(arms)
    assert engine.jump_table_entries(table2)==list(arms2)
