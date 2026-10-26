"""Keep accepted Federer hair in every native detail variant. djultra64.

Replace the old saved03 haircut with the selected02 geometry, preserving the
03 identity used by original far-detail selection. Accepted01/02 leaves and
all other native nodes, textures and wardrobe bytes remain unchanged.
"""
import struct

from character_formats import (pack_zlb, parse_model, payloads, rebuild_pam,
                               sha, unpack_zlb, validate_models)

SELECTED = {
    0: ('13d59455', 2248, '9035c6c7f5f830328426627d347d7ceeaf19978da85a4cac9c4adbbb0d3d40b9'),
    1: ('75ed28ac', 1662, '35189700e0b01fcc486539bc286c86229edf76ba92f576a488060f28f15faebd'),
}
OBSOLETE_ID = '02ea183a'
OBSOLETE_HASH = '75999d25958e489ff0e4807b511d574603590773300787782ae85b479404741c'


def _exact_stream(node, count, digest):
    return (node['name']=='B_tête' and set(node['streams'])=={0}
            and node['streams'][0]['count']==count
            and node['streams'][0]['sha256']==digest)


def correct_model(raw, lod):
    """Return corrected raw OOO and evidence; perform no filesystem changes.

    Pinned vertex hashes identify this accepted sculpt independently of resource
    namespaces. No other haircut or face is inferred from a node name alone.
    """
    if type(lod) is not int or lod not in SELECTED:
        raise ValueError('Federer hair correction requires native LOD 0 or 1')
    before = parse_model(raw)
    identity, count, digest = SELECTED[lod]
    selected = [node for node in before['nodes'] if node['id']==identity]
    if len(selected)!=1 or not _exact_stream(selected[0], count, digest):
        raise ValueError('Unrecognized approved Federer hair geometry')
    from hair_lod_variants import ensure_model_variants, same_selected_leaf
    obsolete = [node for node in before['nodes'] if node['id']==OBSOLETE_ID]
    stale = [node for node in obsolete if not same_selected_leaf(raw, selected[0], node)]
    if stale and (lod!=1 or len(stale)!=1 or not _exact_stream(stale[0], 1168, OBSOLETE_HASH)):
        raise ValueError('Unrecognized obsolete Federer hair geometry')
    result, variants = ensure_model_variants(raw)
    from approved_character_models import validate_hair_attachment
    validate_hair_attachment(result, selected[0])
    revised = {node['id']: node for node in parse_model(result)['nodes']}
    for node in before['nodes']:
        if node['id']==OBSOLETE_ID:
            continue
        target = revised[node['id']]
        if raw[node['start']:node['end']]!=result[target['start']:target['end']]:
            raise ValueError('Federer hair correction changed a retained native node')
    return result, dict(author='djultra64', source_sha256=sha(raw), sha256=sha(result),
        lod=lod, selected_native_haircut=10, selected_display_number=11,
        selected_hair_node=identity, selected_hair_rows=count,
        approved_band_rows=194, removed_objects=len(stale),
        removed_obsolete_node=OBSOLETE_ID if stale else None,
        removed_obsolete_rows=1168 if stale else 0,
        far_detail=variants, retained_nodes_byte_identical=True,
        face_body_skinning_materials_wardrobe_byte_identical=True)


def correct_archive(raw, *, figure=None):
    """Correct both native models in one Federer WAD, retaining all other members."""
    members = payloads(raw)
    replacements, models, proofs = {}, {}, []
    for lod in (0, 1):
        names = [name for name in members if name.endswith('/playerlod%02d.ooo'%lod)]
        if len(names)!=1:
            raise ValueError('Federer WAD requires exactly both native model LODs')
        name = names[0]
        if '/tukurxx00/' not in name and '/tukurxx01/' not in name:
            raise ValueError('Federer hair correction cannot change another athlete')
        source = unpack_zlb(members[name])
        corrected, proof = correct_model(source, lod)
        models[name] = corrected
        proofs.append(dict(proof, member=name))
        if corrected!=source:
            replacements[name] = pack_zlb(corrected, members[name])
    packs = [name for name in members if name.endswith('/textures/tex.pak')]
    if len(packs)!=1:
        raise ValueError('Federer WAD requires one original native texture pack')
    validate_models(models, unpack_zlb(members[packs[0]]), figure=figure)
    result = rebuild_pam(raw, replacements)
    after = payloads(result)
    if any(data!=after[name] for name,data in members.items() if name not in replacements):
        raise ValueError('Federer hair correction changed an unrelated archive member')
    return result, dict(author='djultra64', source_sha256=sha(raw), sha256=sha(result),
        models=proofs, changed_members=sorted(replacements),
        all_other_members_byte_identical=True, texture_pack_byte_identical=True,
        textures_created=False, high_model_byte_identical=True)
