"""Source-bound facial UV alignment; geometry and source pixels stay fixed.
Own work: djultra64. Apply before Xbox userdata/texture namespace conversion.
"""
from pathlib import Path
from functools import lru_cache
import hashlib,importlib.util,math,struct,sys
ROOT=Path(__file__).resolve().parents[2]
FEATURES={'nose','mouth','leftEye','rightEye','mouthLeft','mouthRight','noseLeft','noseRight','earLeft','earRight','chin'}
SIGMA=.055

def smooth(x):
 x=max(0.,min(1.,x));return x*x*(3-2*x)

def gate(p):
 return smooth((p[0]-.28)/.055)*smooth((.72-p[0])/.055)*smooth((p[1]-.20)/.06)*smooth((.82-p[1])/.06)

def kernel(a,b):return math.exp(-sum((x-y)**2 for x,y in zip(a,b))/(2*SIGMA*SIGMA))

def influence(p,anchor):
 if anchor['name'] in ('earLeft','earRight'):
  distance=math.dist(p,anchor['source'])
  return smooth((.07-distance)/.025)*math.exp(-distance*distance/(2*.02*.02))
 return gate(p)*kernel(p,anchor['source'])

def finite(x):return type(x) in (int,float) and math.isfinite(x)

def solve(matrix,b):
 a=[list(row)+[value] for row,value in zip(matrix,b)];n=len(b)
 for i in range(n):
  pivot=max(range(i,n),key=lambda r:abs(a[r][i]))
  if abs(a[pivot][i])<1e-9:raise ValueError('Landmarks are too close; choose separate features')
  a[i],a[pivot]=a[pivot],a[i];divisor=a[i][i]
  for c in range(i,n+1):a[i][c]/=divisor
  for r in range(n):
   if r!=i:
    value=a[r][i]
    for c in range(i,n+1):a[r][c]-=value*a[i][c]
 return [row[n] for row in a]

class UVWarp:
 def __init__(self,anchors):
  self.anchors=anchors
  matrix=[[sum(w*influence(p,b) for w,p in zip(a['weights'],a['vertices'])) for b in anchors] for a in anchors]
  self.cx=solve(matrix,[a['target'][0]-a['source'][0] for a in anchors]) if anchors else []
  self.cy=solve(matrix,[a['target'][1]-a['source'][1] for a in anchors]) if anchors else []
 def apply(self,p):
  if not self.anchors:return list(p)
  weights=[influence(p,a) for a in self.anchors]
  q=[p[0]+sum(c*w for c,w in zip(self.cx,weights)),p[1]+sum(c*w for c,w in zip(self.cy,weights))]
  if not all(finite(v) and 0<=v<=1 for v in q):raise ValueError('Alignment leaves the texture atlas; reduce the adjustment')
  return q

def compile_alignment(alignment,triangles):
 if not isinstance(alignment,dict) or any(not isinstance(k,str) for k in alignment):raise ValueError('Expected UV landmark pairs')
 anchors=[]
 for name,p in sorted(alignment.items()):
  if name not in FEATURES or not isinstance(p,dict):raise ValueError('Invalid texture correspondence')
  index=p.get('triangle');weights=p.get('weights');target=p.get('targetUV')
  if type(index)is not int or not 0<=index<len(triangles) or not isinstance(weights,list) or len(weights)!=3 or not all(finite(w) and -1e-7<=w<=1.0000001 for w in weights) or abs(sum(weights)-1)>1e-6 or not isinstance(target,list) or len(target)!=2 or not all(finite(v) and 0<=v<=1 for v in target):raise ValueError('Invalid '+name+' texture correspondence')
  source=[sum(triangles[index][i]['uv'][j]*weights[i] for i in range(3)) for j in range(2)];g=gate(source)
  ear=name in ('earLeft','earRight')
  if not ear and g<.05:raise ValueError('Choose a landmark inside the central face atlas')
  if math.hypot(target[0]-source[0],target[1]-source[1])>(.03 if ear else .08):raise ValueError('Texture landmark is too far from its mesh point')
  anchors.append(dict(name=name,source=source,target=list(target),gate=g,weights=list(weights),vertices=[list(v['uv']) for v in triangles[index]]))
 return UVWarp(anchors)

def check_triangles(triangles,warp):
 def area(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
 for triangle in triangles:
  a=[v['uv'] for v in triangle];b=[warp.apply(p) for p in a];before=area(*a)
  if abs(before)>1e-12 and area(*b)/before<=.0001:raise ValueError('Texture triangles fold; reduce the alignment')

def validate_native_uv(before,after):
 """Check actual indexed strips after float32 serialization, including low LODs."""
 from character_formats import parse_model
 if len(before)!=len(after):raise ValueError('Native UV alignment changed model size')
 report=parse_model(before);parse_model(after)
 def area(points):
  a,b,c=points
  return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
 for node in report['nodes']:
  if not node['name'].startswith('M_MRF_VSG'):continue
  for draw in node['draw_ranges']:
   stream=node['streams'][draw['format']];indices=draw['indices']
   for i in range(len(indices)-2):
    ids=indices[i:i+3]
    if len(set(ids))<3:continue
    offsets=[stream['offset']+j*stream['stride']+24 for j in ids]
    original=area([struct.unpack_from('<2f',before,at) for at in offsets])
    revised=area([struct.unpack_from('<2f',after,at) for at in offsets])
    if abs(original)>1e-12 and revised/original<=.0001:
     raise ValueError('Texture triangles fold in a native detail level; reduce the alignment')

@lru_cache(maxsize=2)
def trusted_face(character):
 if character not in ('federer','nadal'):raise ValueError('Unknown character')
 import tempfile
 spec=importlib.util.spec_from_file_location('trusted_uv_builder',ROOT/'face-builder/build.py');builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
 with tempfile.TemporaryDirectory(prefix='maxspin-uv-source-') as folder:data=builder.prepare(character,Path(folder),include_previews=False)
 return data['faceTriangles']

def apply_uv_alignment(character,raw,outfit,lod,alignment):
 if character not in ('federer','nadal') or type(outfit)is not int or outfit not in (0,1) or type(lod)is not int or lod not in (0,1):raise ValueError('Unsupported character/outfit/LOD')
 raw=bytes(raw)
 if alignment is None or alignment=={}:return raw,dict(author='djultra64',applied=False,changed_bytes=0)
 from character_formats import parse_model
 triangles=trusted_face(character);warp=compile_alignment(alignment,triangles);check_triangles(triangles,warp)
 report=parse_model(raw);output=bytearray(raw);allowed=set();vertices=0
 for node in report['nodes']:
  if not node['name'].startswith('M_MRF_VSG'):continue
  for stream in node['streams'].values():
   for i in range(stream['count']):
    at=stream['offset']+i*stream['stride']+24
    uv=struct.unpack_from('<2f',raw,at)
    # OBJ/WebGL V runs upwards; Xbox's stored image coordinate runs downwards.
    revised=warp.apply([uv[0],1-uv[1]])
    struct.pack_into('<2f',output,at,revised[0],1-revised[1]);allowed.update(range(at,at+8));vertices+=1
 if not vertices:raise ValueError('No typed native face UV records')
 differences={i for i,(a,b) in enumerate(zip(raw,output)) if a!=b}
 if not differences<=allowed:raise ValueError('UV alignment modified a non-UV byte')
 validate_native_uv(raw,bytes(output))
 return bytes(output),dict(author='djultra64',applied=True,character=character,outfit=outfit,lod=lod,uv_vertices=vertices,changed_bytes=len(differences),geometry_normals_skinning_materials_indices_preserved=True,texture_pixels_preserved=True,features=sorted(alignment),source_sha256=hashlib.sha256(raw).hexdigest(),sha256=hashlib.sha256(output).hexdigest())
