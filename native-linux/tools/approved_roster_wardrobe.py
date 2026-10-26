"""Photograph-guided selection of existing Xbox cloth pixels. djultra64.

No image synthesis, recoloring, garment meshes, face or UV edits. Primary polo
and alternate T-shirt bodies have distinct native geometry/texture families.
"""
from pathlib import Path
import re

from character_formats import Pml,parse_model,parse_texpak,payloads,sha,unpack_zlb
from character_wardrobe import (ARCHIVE,DATABASE,FAMILIES,_read,_props,_dds,
                                build_catalog,validate_selection,apply_wardrobe)

_CHOICES={
 'federer':((768,372,731,603),(98,383,731,603)),
 'nadal':((95,374,731,603),(51,112,731,603)),
 'djokovic':((396,365,731,603),(43,385,731,603)),
 'agassi':((89,452,593,603),(50,463,731,603)),
 'becker':((404,372,731,603),(43,385,731,603)),
 'mcenroe':((468,372,731,603),(50,385,731,603)),
 'pioline':((94,372,731,603),(51,385,731,603)),
 'haas':((393,366,731,603),(51,386,731,603)),
}
_CATEGORIES=('shirt','shorts','shoes','socks')
_ALTERNATE={
 'shirt':{**FAMILIES['shirt'],'index':'1','family':'top/top_01',
          'object_prefixes':('M_MRF_VET___008_000_',),'texture_id':'100000010e058637'},
 'shorts':{**FAMILIES['shorts'],'index':'3','family':'short/short_03',
           'object_prefixes':('M_MRF_VET___001_000_',),'texture_id':'10000001c9b9089a',
           'height':256},
 'shoes':FAMILIES['shoes'],'socks':FAMILIES['socks'],
}


def selection_for(character,outfit):
    """Return four exact creator IDs, independent copies, for outfit 0 or 1."""
    if character not in _CHOICES or type(outfit) is not int or outfit not in (0,1):
        raise ValueError('Unknown athlete or native outfit')
    return dict(zip(_CATEGORIES,('creator:'+str(n) for n in _CHOICES[character][outfit])))


def catalog_for(assets,outfit):
    """Use only catalogued male choices for the exact current garment family."""
    if type(outfit) is not int or outfit not in (0,1):
        raise ValueError('Unknown native outfit')
    if outfit==0:return build_catalog(Path(assets),False)
    cloth,db=_read(assets,ARCHIVE),_read(assets,DATABASE)
    entries=payloads(cloth)
    tree=Pml(unpack_zlb(payloads(db)['media/db/profils(m).pml']))
    rows=[_props(tree,n) for n in tree.node('TPlayerCreatorItems').children]
    result={'schema':'maxspin-wardrobe-catalog-v1','author':'djultra64',
            'source_files':{ARCHIVE:sha(cloth),DATABASE:sha(db)},'categories':[]}
    for category,family in _ALTERNATE.items():
        prefix='media/playercreatorpart/male/cloth/'+family['family']+'/diff_00/'
        by_code={}
        for member in entries:
            if member.startswith(prefix) and member.endswith('.dds'):
                match=re.fullmatch(r'(\d+)_[^/]+\.dds',member[len(prefix):])
                if match:by_code.setdefault(int(match[1]),[]).append(member)
        choices=[]
        for row in rows:
            if row.get('TypeID')!=family['type'] or row.get('TypeIndex')!=family['index'] or not row.get('Description','').startswith('H_'):
                continue
            matches=by_code.get(int(row['TextureCode1']),[])
            if len(matches)!=1:continue
            raw=unpack_zlb(entries[matches[0]]);layout=_dds(raw)
            if any(layout[k]!=family[k] for k in ('width','height','format')):continue
            choices.append({'id':'creator:'+row['Id'],'item_id':int(row['Id']),
                            'label':row['Description'][2:],'enabled':True,'mode':'raw-diffuse',
                            'member':matches[0],'source_sha256':sha(raw),'layout':layout})
        result['categories'].append({'id':category,'choices':choices})
    return result


def apply(models,texture_pack,assets,character,outfit):
    """Return (identical models, changed existing cloth pixels, proof).

    Raw primary bodies use the established wardrobe reader. Alternate bodies,
    or an already namespaced Federer body, resolve the diffuse via typed cloth
    owners and exact layouts rather than guessing/replacing texture IDs.
    """
    selection=selection_for(character,outfit)
    catalog=catalog_for(assets,outfit)
    choices=validate_selection(selection,catalog)
    families=FAMILIES if outfit==0 else _ALTERNATE
    texture_rows={r['id']:r for r in parse_texpak(texture_pack)}
    if outfit==0 and all(families[c]['texture_id'] in texture_rows for c in _CATEGORIES):
        revised,output,proof=apply_wardrobe(models,texture_pack,Path(assets),selection)
    else:
        entries=payloads(_read(assets,ARCHIVE))
        reports={name:parse_model(raw) for name,raw in models.items()}
        if not reports:raise ValueError('Every native outfit LOD is required')
        output=bytearray(texture_pack)
        proof={'schema':'maxspin-wardrobe-proof-v1','author':'djultra64','changes':[],
               'model_sha256':{n:sha(raw) for n,raw in models.items()},
               'geometry_uv_skinning_unchanged':True,'face_textures_unchanged':True,
               'source_files':catalog['source_files']}
        for category,choice in choices.items():
            family=families[category];identities=[]
            for name,report in reports.items():
                owners=[n for n in report['nodes'] if n['name'].startswith(family['object_prefixes'])]
                if not owners or any(not any(n['name'].startswith(prefix) for n in owners) for prefix in family['object_prefixes']):
                    raise ValueError('Compatible paired native garment is missing: '+category+'/'+name)
                candidates={identity for n in owners for identity in n['resources'] if identity in texture_rows
                            and all(texture_rows[identity][k]==family[k] for k in ('width','height','format'))}
                if len(candidates)!=1:raise ValueError('Ambiguous garment diffuse: '+category+'/'+name)
                identity=candidates.pop()
                if any(identity in n['resources'] and n not in owners for n in report['nodes']):
                    raise ValueError('Garment pixels shared with non-cloth geometry')
                identities.append(identity)
            if len(set(identities))!=1:raise ValueError('Garment diffuse differs across native LODs')
            identity=identities[0];row=texture_rows[identity];raw=unpack_zlb(entries[choice['member']])
            if sha(raw)!=choice['source_sha256'] or _dds(raw)['mips']<row['mips']:
                raise ValueError('Existing garment source differs or lacks mips')
            pixels=raw[128:128+row['bytes']]
            if len(pixels)!=row['bytes']:raise ValueError('Existing garment mip bytes truncated')
            output[row['offset']:row['offset']+row['bytes']]=pixels
            proof['changes'].append({'category':category,'choice':choice['id'],'item_id':choice['item_id'],
                'member':choice['member'],'source_sha256':choice['source_sha256'],'texture_id':identity,
                'before_sha256':row['payload_sha256'],'after_sha256':sha(pixels),'mips':row['mips']})
        revised=models;output=bytes(output);parse_texpak(output)
        proof.update(texture_source_sha256=sha(texture_pack),texture_result_sha256=sha(output))
    proof.update(selection=selection,character=character,outfit=outfit,
                 source_kind='Original Xbox catalog diffuse bytes; no image synthesis or recoloring',
                 historical_accuracy='Nearest existing compatible garments; original logos and geometry retained')
    return revised,output,proof
