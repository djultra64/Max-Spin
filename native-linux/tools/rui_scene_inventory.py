#!/usr/bin/env python3
"""Decode original Rapid RUI declarative Lua chunks without running Lua/game code.
Own decoder: djultra64. Only table/literal opcodes are accepted.
"""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path

class Reader:
    def __init__(self,data):
        if len(data)>16*1024*1024 or data[:14]!=bytes.fromhex('1b4c756150010404040608090908'):
            raise ValueError('Unsupported original Lua/Rapid chunk header')
        self.data=data;self.at=22
    def take(self,n):
        if n<0 or self.at+n>len(self.data):raise ValueError('Truncated chunk')
        b=self.data[self.at:self.at+n];self.at+=n;return b
    def integer(self):return struct.unpack('<I',self.take(4))[0]
    def count(self):
        n=self.integer()
        if n>200000:raise ValueError('Unbounded chunk population')
        return n
    def string(self):
        n=self.count()
        if not n:return None
        b=self.take(n)
        if not b.endswith(b'\0'):raise ValueError('String lacks original terminator')
        return b[:-1].decode('latin1')
    def prototype(self):
        source=self.string();line=self.integer();nups,nparams,vararg,stack=self.take(4)
        # Rapid's shipped dialect serializes locals, lines, constants and
        # code in this order. Do not silently treat it as a different Lua ABI.
        locals_=[(self.string(),self.integer(),self.integer())for _ in range(self.count())]
        lines=[self.integer()for _ in range(self.count())]
        constants=[]
        for _ in range(self.count()):
            kind=self.take(1)[0]
            if kind==0:value=None
            elif kind==3:value=struct.unpack('<d',self.take(8))[0]
            elif kind==4:value=self.string()
            else:raise ValueError('Unsupported constant type')
            constants.append(value)
        if self.integer()!=0:raise ValueError('Nested function code is not declarative data')
        code=[self.integer()for _ in range(self.count())]
        if self.at!=len(self.data) or len(lines)not in(0,len(code)):
            raise ValueError('Chunk boundary/debug-line population mismatch')
        return dict(source=source,line=line,nups=nups,nparams=nparams,vararg=vararg,
                    stack=stack,locals=locals_,constants=constants,code=code)


def decode(data):
    p=Reader(data).prototype();register={};global_tables={'Scenes':{}};k=p['constants']
    def rk(value):return k[value-250]if value>=250 else register[value]
    for pc,word in enumerate(p['code']):
        op=word&63;a=word>>24;b=word>>15&511;c=word>>6&511;bx=word>>6&262143
        if a>=250:raise ValueError('Invalid register')
        if op==10:register[a]={}
        elif op==1:register[a]=k[bx]
        elif op==2:
            if c:raise ValueError('Control-flow opcode in declarative data')
            register[a]=bool(b)
        elif op==5:
            if k[bx]!='Scenes':raise ValueError('Unknown external global')
            register[a]=global_tables['Scenes']
        elif op==7:
            if k[bx]!='SceneData':raise ValueError('Unexpected global assignment')
            global_tables[k[bx]]=register[a]
        elif op==9:register[a][rk(b)]=rk(c)
        elif op==31:
            for i in range(1,bx%32+2):register[a][bx-bx%32+i]=register[a+i]
        elif op==27:
            if pc!=len(p['code'])-1:raise ValueError('Early return in data chunk')
        else:raise ValueError(f'Non-declarative opcode {op} at instruction {pc}')
    return global_tables


def inventory(package,scene):
    with zipfile.ZipFile(package)as z:
        layout=z.read(f'Scenes/{scene}.sceneData');events=z.read(f'Scenes/{scene}.scene')
    model=decode(layout)['SceneData'];bindings=decode(events)['Scenes'][scene]
    controls=[]
    for index,control in model['Controls'].items():
        controls.append(dict(index=index,name=control.get('Name'),node=control.get('NodeName'),
                             events=bindings['Controls'].get(index,{})))
    return dict(author='djultra64',scope='Original table construction decoded; no Lua CALL, game execution or runtime state claims',
                package=str(package),scene=scene,
                original_sha256=dict(layout=hashlib.sha256(layout).hexdigest(),events=hashlib.sha256(events).hexdigest()),
                controls=controls,actions=model['Actions'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);p.add_argument('--scene',default='newmain');p.add_argument('--output',type=Path);a=p.parse_args()
    text=json.dumps(inventory(a.package,a.scene),indent=2)+'\n'
    if a.output:a.output.write_text(text)
    else:print(text,end='')
