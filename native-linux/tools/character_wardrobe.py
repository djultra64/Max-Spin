"""Source-bound Xbox wardrobe textures for the preserved baked bodies. djultra64.

Only existing diffuse texture pixels for matching geometry families are used.
This module never edits models, face textures, UVs, skinning or original assets.
"""
import base64
import json
from io import BytesIO
from pathlib import Path
import struct
import re

from character_formats import Pml, parse_model, parse_texpak, payloads, sha, texture_size, unpack_zlb

ARCHIVE = 'Media/Wads/PlayerCreatorMaleCloth.wad'
DATABASE = 'Media/Wads/Db.wad'
MAX_SOURCE = 128 * 1024 * 1024
FAMILIES = {
    'shirt': {'label':'Shirt', 'type':'13', 'index':'0', 'family':'top/top_00',
              'object_prefixes':('M_MRF_VET___002_000_',), 'texture_id':'10000001c282a38d',
              'width':512, 'height':512, 'format':8},
    'shorts': {'label':'Shorts', 'type':'14', 'index':'2', 'family':'short/short_02',
               'object_prefixes':('M_MRF_VET___003_000_',), 'texture_id':'10000001f1b2ff2a',
               'width':256, 'height':512, 'format':8},
    'shoes': {'label':'Shoes', 'type':'15', 'index':'1', 'family':'shoes/shoes_01',
              'object_prefixes':('M_VET_CHS_D_001_____', 'M_VET_CHS_G_001_____'),
              'texture_id':'10000001226c8843', 'width':256, 'height':256, 'format':4},
    'socks': {'label':'Socks', 'type':'17', 'index':'0', 'family':'socks/socks_00',
              'object_prefixes':('M_VET_CHT_D_000_____', 'M_VET_CHT_G_000_____'),
              'texture_id':'10000001b8bbaae6', 'width':128, 'height':128, 'format':4},
}


def _read(assets, relative):
    with (Path(assets) / relative).open('rb') as source:
        data = source.read(MAX_SOURCE + 1)
    if len(data) > MAX_SOURCE:
        raise ValueError('Native wardrobe source exceeds bound: ' + relative)
    return data


def _dds(raw):
    if len(raw) < 128 or raw[:4] != b'DDS ' or struct.unpack_from('<I', raw, 4)[0] != 124 or struct.unpack_from('<I', raw, 76)[0] != 32:
        raise ValueError('Invalid native wardrobe DDS header')
    height, width = struct.unpack_from('<II', raw, 12)
    mips = max(1, struct.unpack_from('<I', raw, 28)[0])
    fmt = {b'DXT1':4, b'DXT5':8}.get(raw[84:88])
    if fmt is None or len(raw) != 128 + texture_size(width, height, fmt, mips):
        raise ValueError('Unsupported native wardrobe DDS layout')
    return {'width':width, 'height':height, 'format':fmt, 'mips':mips}


def _preview(raw):
    from PIL import Image
    with Image.open(BytesIO(raw)) as image:
        image = image.convert('RGBA')
        image.thumbnail((96,96), Image.Resampling.LANCZOS)
        output = BytesIO()
        image.save(output, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(output.getvalue()).decode('ascii')


def _image_url(image):
    output = BytesIO()
    image.save(output, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(output.getvalue()).decode('ascii')


def _preview_model(character='federer'):
    """The preserved baked source has the exact enabled garment UV families."""
    root = Path(__file__).resolve().parents[2].parent / 'character-research'
    targets = {'federer':'TukurXX','nadal':'Houston'}
    if character not in targets:
        raise ValueError('Unknown wardrobe preview character')
    target = targets[character]
    relative = f'build/Federer-Creator-Test/Media/Wads/Players/{target}00.wad'
    inventory = json.loads((root / 'preservation-manifest.json').read_text())
    record = next((row for row in inventory['files'] if row['path'] == 'legacy/' + relative), None)
    original = _read(root / 'legacy', relative)
    if record is None or record.get('bytes') != len(original) or record.get('sha256') != sha(original):
        raise ValueError('Wardrobe preview source differs from verified preserved research')
    prefix = f'media/gamepart/players/{target.lower()}00/'
    member = prefix + 'playerlod00.ooo'
    model = unpack_zlb(payloads(original)[member])
    texture = unpack_zlb(payloads(original)[prefix+'textures/tex.pak'])
    return model, texture, {'character':character,'archive':'legacy/' + relative,'archive_sha256':sha(original),
                            'member':member,'model_sha256':sha(model)}


def _native_texture_dds(texture_pack, identity):
    """Wrap exact frozen native mip bytes in a DDS header for preview decoding."""
    row = next((row for row in parse_texpak(texture_pack) if row['id'] == identity),None)
    if row is None or row['format'] not in (4,8):
        raise ValueError('Preserved garment texture is missing or unsupported')
    header = bytearray(128)
    header[:4] = b'DDS '
    struct.pack_into('<7I',header,4,124,0xa1007,row['height'],row['width'],
                     texture_size(row['width'],row['height'],row['format'],1),0,row['mips'])
    struct.pack_into('<II',header,76,32,4)
    header[84:88] = b'DXT1' if row['format'] == 4 else b'DXT5'
    struct.pack_into('<I',header,108,0x401008 if row['mips'] > 1 else 0x1000)
    return bytes(header)+texture_pack[row['offset']:row['offset']+row['bytes']],row


def _garment_maps(model, prefixes, size=512, report=None):
    """Rasterize original indexed native strips into reusable visible UV maps."""
    import numpy as np
    if type(size) is not int or not 128 <= size <= 1024:
        raise ValueError('Invalid native garment preview size')
    report = report or parse_model(model)
    positions, normals, uvs, objects = [], [], [], []
    for node in report['nodes']:
        if not node['name'].startswith(prefixes):
            continue
        objects.append(node['name'])
        for draw in node['draw_ranges']:
            stream = node['streams'][draw['format']]
            rows = np.ndarray((stream['count'],8), dtype='<f4', buffer=model,
                              offset=stream['offset'],strides=(stream['stride'],4))
            indices = draw['indices']
            for at in range(len(indices)-2):
                triangle = indices[at:at+3]
                if len(set(triangle)) != 3:
                    continue
                vertices = rows[triangle]
                positions.append(vertices[:,:3])
                normals.append(vertices[:,3:6])
                # Native OOO V is top-origin, matching the decoded DDS rows.
                uvs.append(vertices[:,6:8])
    if not positions:
        raise ValueError('Compatible native garment mesh is missing')
    positions, normals, uvs = map(np.asarray,(positions,normals,uvs))
    lower, upper = positions.reshape(-1,3).min(0), positions.reshape(-1,3).max(0)
    span = upper-lower
    if min(span[:2]) <= 1e-6:
        raise ValueError('Native garment has no visible front/profile extent')
    scale = (size*.84)/max(span[:2])
    center = (lower+upper)*.5
    result = {}
    light = np.asarray([-.3,.45,.84]);light /= np.linalg.norm(light)
    for label, side in (('front',-1),('back',1)):
        projected = positions.copy()
        projected[:,:,0] = (positions[:,:,0]-center[0])*side*scale+size*.5
        projected[:,:,1] = -(positions[:,:,1]-center[1])*scale+size*.5
        projected[:,:,2] *= side
        depth = np.full((size,size),-np.inf,dtype=np.float32)
        uv_map = np.zeros((size,size,2),dtype=np.float32)
        shade = np.ones((size,size),dtype=np.float32)
        mask = np.zeros((size,size),dtype=bool)
        for triangle, normal, uv in zip(projected,normals,uvs):
            minimum = np.maximum(0,np.floor(triangle[:,:2].min(0)).astype(int))
            maximum = np.minimum(size-1,np.ceil(triangle[:,:2].max(0)).astype(int))
            if (maximum<minimum).any():
                continue
            x,y=np.meshgrid(np.arange(minimum[0],maximum[0]+1)+.5,
                            np.arange(minimum[1],maximum[1]+1)+.5)
            a,b,c=triangle
            denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denominator)<1e-7:
                continue
            wa=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/denominator
            wb=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/denominator
            wc=1-wa-wb
            z=wa*a[2]+wb*b[2]+wc*c[2]
            block=np.s_[minimum[1]:maximum[1]+1,minimum[0]:maximum[0]+1]
            visible=(wa>=-1e-5)&(wb>=-1e-5)&(wc>=-1e-5)&(z>depth[block])
            if not visible.any():
                continue
            weights=np.stack((wa,wb,wc),axis=-1)
            surface_uv=weights@uv
            surface_normals=weights@normal
            surface_normals*=np.asarray([side,1,side])
            surface_normals/=np.maximum(np.linalg.norm(surface_normals,axis=-1,keepdims=True),1e-6)
            illumination=.88+.12*np.maximum(0,surface_normals@light)
            depth[block][visible]=z[visible]
            uv_map[block][visible]=surface_uv[visible]
            shade[block][visible]=illumination[visible]
            mask[block][visible]=True
        if mask.sum()<size*size*.05:
            raise ValueError('Native garment preview rasterization is empty')
        result[label]={'uv':uv_map,'shade':shade,'mask':mask}
    return result, {'model_sha256':sha(model),'objects':objects,'triangles':len(positions),
                    'width':size,'height':size,'projection':'native indexed mesh, orthographic front/back; native top-origin UV'}


def _render_views(maps, raw_dds):
    import numpy as np
    from PIL import Image
    _dds(raw_dds)
    with Image.open(BytesIO(raw_dds)) as source:
        texture=np.asarray(source.convert('RGB'),dtype=np.float32)
    height,width=texture.shape[:2]
    result={}
    for label,surface in maps.items():
        uv=surface['uv']
        x=np.clip(uv[:,:,0]*width-.5,0,width-1)
        y=np.clip(uv[:,:,1]*height-.5,0,height-1)
        x0,y0=np.floor(x).astype(int),np.floor(y).astype(int)
        x1,y1=np.minimum(x0+1,width-1),np.minimum(y0+1,height-1)
        fx,fy=(x-x0)[:,:,None],(y-y0)[:,:,None]
        colors=(texture[y0,x0]*(1-fx)+texture[y0,x1]*fx)*(1-fy)+(texture[y1,x0]*(1-fx)+texture[y1,x1]*fx)*fy
        pixels=np.zeros((*surface['mask'].shape,4),dtype=np.uint8)
        pixels[:,:,:3]=np.clip(colors*surface['shade'][:,:,None],0,255).astype(np.uint8)
        # Garment alpha carries material information; geometry defines its outline.
        pixels[:,:,3]=surface['mask'].astype(np.uint8)*255
        pixels[~surface['mask'],:3]=0
        result[label]=_image_url(Image.fromarray(pixels))
    return result


def render_garment_views(model:bytes, object_prefixes, raw_dds:bytes, size=512):
    """Actual native geometry and UVs with original diffuse pixels, for review."""
    maps,proof=_garment_maps(model,tuple(object_prefixes),size)
    return _render_views(maps,raw_dds),proof


def _props(tree, node):
    return {tree.names.get(key, '%08x' % key):value for key,value in node.properties}


def _catalog(assets, include_previews, character=None):
    cloth, db = _read(assets, ARCHIVE), _read(assets, DATABASE)
    entries = payloads(cloth)
    tree = Pml(unpack_zlb(payloads(db)['media/db/profils(m).pml']))
    rows = [_props(tree,node) for node in tree.node('TPlayerCreatorItems').children]
    catalog = {'schema':'maxspin-wardrobe-catalog-v1', 'author':'djultra64',
               'source_files':{ARCHIVE:sha(cloth),DATABASE:sha(db)},
               'application':'Existing Xbox diffuse pixels; preserved baked geometry and reflection textures. Creator procedural color blending is not applied.',
               'preview_contract':{'shirt':'textured-native-garment','shorts':'textured-native-garment',
                                   'views':['front','back'],'size':[512,512],
                                   'preserved':'Exact frozen primary-outfit model and texture of the opened character',
                                   'lighting':'Neutral diffuse preview; native title materials/lighting can differ'},
               'categories':[]}
    preview_model, preview_texture, preview_source = _preview_model(character or 'federer') if include_previews else (None,None,None)
    preview_report = parse_model(preview_model) if include_previews else None
    for category, family in FAMILIES.items():
        garment_maps = garment_proof = None
        if include_previews and category in ('shirt','shorts'):
            garment_maps,garment_proof = _garment_maps(preview_model,family['object_prefixes'],report=preview_report)
        prefix = 'media/playercreatorpart/male/cloth/' + family['family'] + '/diff_00/'
        by_code = {}
        for member in sorted(entries):
            if member.startswith(prefix) and member.endswith('.dds'):
                match = re.fullmatch(r'(\d+)_[^/]+\.dds', member[len(prefix):])
                if match:
                    by_code.setdefault(int(match[1]),[]).append(member)
        choices = [{'id':'preserved','label':'Keep preserved outfit','enabled':True}]
        if garment_maps is not None:
            preserved_dds,preserved_row = _native_texture_dds(preview_texture,family['texture_id'])
            views = _render_views(garment_maps,preserved_dds)
            choices[0].update(previews=views,preview=views['front'],
                preview_kind='textured-native-garment',preview_labels={'front':'Front','back':'Back'},
                preview_source={**preview_source,**garment_proof,'texture_id':family['texture_id'],
                                'texture_sha256':preserved_row['payload_sha256']})
        for row in sorted(rows, key=lambda row:int(row['Id'])):
            if row.get('TypeID') != family['type'] or row.get('TypeIndex') != family['index'] or not row.get('Description','').startswith('H_'):
                continue
            code = int(row['TextureCode1'])
            sources = by_code.get(code, [])
            choice = {'id':'creator:' + row['Id'], 'item_id':int(row['Id']),
                      'label':row['Description'][2:], 'source_label':row['Description'],
                      'texture_code':code, 'enabled':False, 'mode':'raw-diffuse',
                      'creator_fields':{key:value for key,value in row.items() if key not in ('Id','Name','Description')}}
            if len(sources) != 1:
                choice['reason'] = 'Native diffuse source is missing or ambiguous for this existing catalog entry.'
            else:
                raw = unpack_zlb(entries[sources[0]])
                layout = _dds(raw)
                if any(layout[key] != family[key] for key in ('width','height','format')):
                    choice['reason'] = 'Native texture layout differs from the preserved baked garment.'
                else:
                    choice.update(enabled=True,member=sources[0],source_sha256=sha(raw),layout=layout)
                    if include_previews:
                        if garment_maps is not None:
                            choice['previews'] = _render_views(garment_maps,raw)
                            choice['preview'] = choice['previews']['front']
                            choice['preview_kind'] = 'textured-native-garment'
                            choice['preview_labels'] = {'front':'Front','back':'Back'}
                            choice['preview_source'] = {**preview_source,**garment_proof}
                            choice['atlas_preview'] = _preview(raw)
                        else:
                            choice['preview'] = _preview(raw)
                            choice['preview_kind'] = 'native-texture-atlas'
            choices.append(choice)
        catalog['categories'].append({'id':category,'label':family['label'],
            'geometry_family':family['family'].split('/')[-1], 'choices':choices})
    cap_choices = [{'id':'preserved','label':'Keep preserved headwear','enabled':True}]
    for row in rows:
        if row.get('TypeID') == '3' and row.get('Description','').startswith('H_') and 'cap' in row.get('Description','').lower():
            cap_choices.append({'id':'creator:'+row['Id'],'item_id':int(row['Id']),
                'label':row['Description'][2:],'source_label':row['Description'], 'enabled':False,
                'reason':'Native cap is a different creator haircut/accessory mesh. Baked hair, head fit, morphs and hide rules require separate validation.'})
    catalog['categories'].append({'id':'cap','label':'Cap / headwear','geometry_family':None,'choices':cap_choices})
    return catalog, entries


def build_catalog(assets: Path, include_previews=True, character=None):
    """Read exact male Xbox catalog IDs; optionally embed private native thumbnails."""
    return _catalog(assets, include_previews,character)[0]


def validate_selection(selection, catalog):
    categories = {category['id']:category for category in catalog['categories']}
    if not isinstance(selection, dict) or not set(selection) <= set(categories):
        raise ValueError('Unsupported native wardrobe category')
    result = {}
    for category, identity in selection.items():
        if not isinstance(identity, str):
            raise ValueError('Wardrobe choice must be an existing catalog ID')
        choices = {choice['id']:choice for choice in categories[category]['choices']}
        if identity not in choices or not choices[identity]['enabled']:
            raise ValueError('Unsupported native wardrobe choice: ' + category + '/' + identity)
        if identity != 'preserved':
            result[category] = choices[identity]
    return result


def apply_wardrobe(models, texture_pack: bytes, assets: Path, selection):
    """Return identical model bytes and private modified existing cloth textures.

    Call before per-athlete texture namespacing. Selection contains catalog IDs,
    never paths, colors, image bytes or arbitrary replacement resource IDs.
    """
    proof = {'schema':'maxspin-wardrobe-proof-v1','author':'djultra64', 'changes':[],
             'model_sha256':{name:sha(raw) for name,raw in models.items()},
             'geometry_uv_skinning_unchanged':True,'face_textures_unchanged':True}
    if selection == {}:
        return models, texture_pack, proof
    catalog, entries = _catalog(assets, False)
    choices = validate_selection(selection, catalog)
    proof['source_files'] = catalog['source_files']
    if not choices:
        return models, texture_pack, proof
    textures = {row['id']:row for row in parse_texpak(texture_pack)}
    parsed = {name:parse_model(unpack_zlb(raw)) for name,raw in models.items()}
    if not parsed:
        raise ValueError('Wardrobe requires every native outfit LOD model')
    output = bytearray(texture_pack)
    for category, choice in choices.items():
        family = FAMILIES[category]
        identity = family['texture_id']
        row = textures.get(identity)
        if row is None or any(row[key] != family[key] for key in ('width','height','format')):
            raise ValueError('Native baked wardrobe texture layout differs: ' + category)
        for name, model in parsed.items():
            owners = [node for node in model['nodes'] if identity in node['resources']]
            if not owners or any(not node['name'].startswith(family['object_prefixes']) for node in owners):
                raise ValueError('Wardrobe diffuse is absent or shared with non-cloth geometry: ' + category + '/' + name)
            if any(not any(node['name'].startswith(prefix) for node in owners) for prefix in family['object_prefixes']):
                raise ValueError('Native wardrobe is missing a paired garment object: ' + category)
        raw = unpack_zlb(entries[choice['member']])
        if sha(raw) != choice['source_sha256'] or _dds(raw)['mips'] < row['mips']:
            raise ValueError('Native wardrobe source binding or mip chain differs')
        pixels = raw[128:128+row['bytes']]
        if len(pixels) != row['bytes']:
            raise ValueError('Native wardrobe source mip bytes truncated')
        output[row['offset']:row['offset']+row['bytes']] = pixels
        proof['changes'].append({'category':category,'choice':choice['id'],'item_id':choice['item_id'],
            'member':choice['member'],'source_sha256':choice['source_sha256'],'texture_id':identity,
            'before_sha256':row['payload_sha256'],'after_sha256':sha(pixels),'mips':row['mips']})
    output = bytes(output)
    parse_texpak(output)
    proof.update(texture_source_sha256=sha(texture_pack),texture_result_sha256=sha(output))
    return models, output, proof
