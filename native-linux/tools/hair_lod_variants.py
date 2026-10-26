"""Preserve selected hair in the native02/03 detail selector. djultra64.

The low OOO supplies both logical model detail levels. Original13A5B0 hides
hair02 at the far detail and hair03 at the middle detail. Both identities must
therefore contain the selected style, rather than deleting the far identity.
"""
import struct
from character_formats import parse_model, pack_zlb, payloads, rebuild_pam, sha, unpack_zlb

PAIRS = {'75ed28ac': '02ea183a', 'f5d6a3ec': '82d1937a'}


def same_selected_leaf(raw, first, second):
    a, b = raw[first['start']:first['end']], raw[second['start']:second['end']]
    return a[:4] + a[8:] == b[:4] + b[8:]


def ensure_model_variants(raw):
    """Return bytes/evidence, changing only a missing or stale far hair leaf."""
    parsed = parse_model(raw)
    selected = [n for n in parsed['nodes'] if n['id'] in PAIRS]
    if not selected:
        if any(n['id'] in PAIRS.values() for n in parsed['nodes']):
            raise ValueError('Native far hair03 is missing its selected middle hair02')
        return raw, dict(added_far_variant=False, replaced_far_variant=False)
    if len(selected) != 1:
        raise ValueError('Ambiguous native low hair identities')
    selected = selected[0]
    if selected['kind'] != 0x79 or selected['name'] != 'B_tête':
        raise ValueError('Native low hair must be a rigid head attachment')
    identity = PAIRS[selected['id']]
    far = [n for n in parsed['nodes'] if n['id'] == identity]
    if len(far) > 1 or any(n['kind'] != 0x79 or n['name'] != 'B_tête' for n in far):
        raise ValueError('Invalid native far hair attachment')
    if far and same_selected_leaf(raw, selected, far[0]):
        return raw, dict(added_far_variant=False, replaced_far_variant=False)
    leaf = bytearray(raw[selected['start']:selected['end']])
    struct.pack_into('<I', leaf, 4, int(identity, 16))
    nodes = [(n['kind'], bytes(leaf) if n['id'] == identity else raw[n['start']:n['end']])
             for n in parsed['nodes']]
    if not far:
        nodes.append((0x79, bytes(leaf)))
    types_at = parsed['nodes'][0]['start'] - parsed['objects'] * 4
    header = bytearray(raw[:types_at])
    struct.pack_into('<I', header, 4, len(nodes))
    if raw[8:21] == b'VertexBufSize':
        added = sum(s['count'] * s['stride'] for s in selected['streams'].values())
        removed = sum(s['count'] * s['stride'] for n in far for s in n['streams'].values())
        struct.pack_into('<I', header, 21, parsed['vertex_bytes'] + added - removed)
    result = (bytes(header) + b''.join(struct.pack('<I', 0x12345600 + kind) for kind, _ in nodes)
              + b''.join(data for _, data in nodes) + raw[parsed['nodes'][-1]['end']:])
    revised = parse_model(result)
    after = {n['id']: n for n in revised['nodes']}
    if not same_selected_leaf(result, after[selected['id']], after[identity]):
        raise ValueError('Far detail differs from the selected hairstyle')
    for node in parsed['nodes']:
        if node['id'] == identity:
            continue
        target = after[node['id']]
        if raw[node['start']:node['end']] != result[target['start']:target['end']]:
            raise ValueError('Hair detail repair changed an unrelated native node')
    return result, dict(added_far_variant=not far, replaced_far_variant=bool(far),
                        selected_node=selected['id'], far_node=identity,
                        selected_style_identical=True, retained_nodes_byte_identical=True)


def correct_archive(raw):
    members = payloads(raw)
    replacements, proofs = {}, []
    for name, packed in members.items():
        if not name.endswith('/playerlod01.ooo'):
            continue
        source = unpack_zlb(packed)
        revised, proof = ensure_model_variants(source)
        proofs.append(dict(proof, member=name))
        if revised != source:
            replacements[name] = pack_zlb(revised, packed)
    result = rebuild_pam(raw, replacements)
    after = payloads(result)
    if any(after[name] != data for name, data in members.items() if name not in replacements):
        raise ValueError('Hair detail repair changed an unrelated archive member')
    return result, dict(author='djultra64', source_sha256=sha(raw), sha256=sha(result),
                        models=proofs, changed_members=sorted(replacements),
                        all_other_members_byte_identical=True, texture_pack_byte_identical=True)


def validate_model_variants(raw):
    """Reject an integrated creator-style02 hair lacking its selected03."""
    nodes = parse_model(raw)['nodes']
    if not any(n['id'] in PAIRS or n['id'] in PAIRS.values() for n in nodes):
        return
    if ensure_model_variants(raw)[0] != raw:
        raise ValueError('Native hair02/03 detail variants are missing or use different hairstyles')
