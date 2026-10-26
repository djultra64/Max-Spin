"""Assemble accepted athlete art into private native Xbox models. djultra64.

This module reads frozen evidence and returns bytes. It never writes resources,
activates packages, regenerates accepted artwork, or changes Federer.
"""
from pathlib import Path
import configparser
import io
import json
import re
import struct
import zlib

import numpy as np
from PIL import Image

from character_formats import (convert_model, namespace_textures, parse_model,
    parse_texpak, payloads, sha, texture_size, unpack_zlb, validate_models)

APPROVED = {
    'nadal': {'obj': 'nadal-selected-hair15-green-cap/nadal-selected.obj',
              'obj_sha256': 'feab26dbb0d8521e69c5b40d59060f0eebec4d6af4fd33c3b82cca634c7231be',
              'albedo': 'nadal-reference-revision-v2/nadal-face-albedo-v2.png',
              'sha256': 'c17ee043844fa0f774c0076b5b6dbd7d450722a0d25390e39f4c21e80fdf3e13',
              'hair': 14, 'variant': '0010',
              'cap_dds': 'nadal-selected-hair15-green-cap/textures/green-cap-native.dds',
              'cap_dds_sha256': '84ac1fbb60c96b9e4c53977cd379c64b509a6b99b3d48dbee4a20eddc28909c2'},
    'djokovic': {'obj': 'automatic-djokovic/djokovic-face-v1.obj',
                 'obj_sha256': '7017e07b64cf63f67039900ce7aca50827851cac52155461c493bb3f1b88239e',
                 'albedo': 'automatic-djokovic/djokovic-face-albedo-v1.png',
                 'sha256': '48569ee4a01dd5284a7a1d979fac4ff2bda8296cfe507cc1e7171a47d7408258',
                 'hair': 5, 'variant': '0020'},
    'agassi': {'obj': 'automatic-agassi/agassi-face-v1.obj',
               'obj_sha256': '5b0a9e172a9d4710affb67925008ef67270aacb221743d0146f77799908987bf',
               'albedo': 'automatic-agassi/agassi-face-albedo-v1.png',
               'sha256': '0b160af5dd51e65af83933b01b68bd8f6ebf5f31d12a4d92a2166ded5fa45edc',
               'hair': 3, 'variant': '0000'},
    'becker': {'obj': 'automatic-becker/becker-face-v1.obj',
               'obj_sha256': 'd3774872b6214ea12bccf51d5f308bd691acf01307b44573ceeb859a7fcc13d7',
               'albedo': 'automatic-becker/becker-face-albedo-v1.png',
               'sha256': '8bc900c62513d2282890ce638bbcf9857380fa7fc7e09db3937c4f27d8fc84f3',
               'hair': 11, 'variant': '0000'},
    'mcenroe': {'obj': 'automatic-mcenroe/mcenroe-face-v1.obj',
                'obj_sha256': '47403386e27783c5d9057d099cdc4d92831b1ae9bd28c59d3b52be105b65ab68',
                'albedo': 'automatic-mcenroe/mcenroe-face-albedo-v2.png',
                'sha256': '7570fc411afd27c06d90a40eb68591a00fe8b4c8487562549e820c35f8f40144',
                'hair': 3, 'variant': '0010'},
    'pioline': {'obj': 'automatic-pioline/pioline-face-v1.obj',
                'obj_sha256': '809b6f2d6013f58dbc2d1ec05b3d04247a25b88c6ed3fe92e7e2583c459b4fb7',
                'albedo': 'automatic-pioline/textures/tex_1000000144b4dc6e.png',
                'sha256': 'd269fab427693193ca54ef53d118fcaee22fd02726571c38d6795f4c24b14535',
                'hair': 8, 'variant': '0010'},
    'haas': {'obj': 'automatic-haas/haas-face-v1.obj',
             'obj_sha256': '0655060ee8d55b96313d7a09554bbedfabcfa00b5eb82d6a313953eecc5a4fb8',
             'albedo': 'automatic-haas/haas-face-albedo-v1.png',
             'sha256': '64b6702f4648173befadd0f7da1531b298ac42c19d378bf4a3b9a51f29d0a3d1',
             'hair': 13, 'variant': '0010'},
}
FACE = '1000000144b4dc6e'
EYE = '10000001e5da8b09'
M = lambda suffix: struct.pack('<I', 0x12345600 + suffix)


def _obj(path):
    vertices, normals, uv, used = [], [], [], set()
    material, materials = None, {}
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v': vertices.append(list(map(float, fields[1:4])))
        elif fields[0] == 'vn': normals.append(list(map(float, fields[1:4])))
        elif fields[0] == 'vt': uv.append(list(map(float, fields[1:3])))
        elif fields[0] == 'usemtl': material = fields[1]
        elif fields[0] == 'f':
            for token in fields[1:]:
                row = tuple(int(i) - 1 for i in token.split('/'))
                if len(row) != 3 or row[0] != row[1] or row[0] != row[2]:
                    raise ValueError('Accepted OBJ must retain one native row for position/normal/UV')
                used.add(row[0])
    for line in path.with_suffix('.mtl').read_text().splitlines():
        if line.startswith('newmtl '): material = line[7:]
        elif line.startswith('map_Kd '):
            texture = Path(line[7:])
            materials[material] = texture if texture.is_absolute() else path.parent / texture
    result = np.column_stack([vertices, normals, uv])
    result[:, 7] = 1 - result[:, 7]  # OBJ uses upward V; Xbox uses downward V.
    if not np.isfinite(result).all() or min(used, default=-1) < 0 or max(used) >= len(result):
        raise ValueError('Invalid accepted OBJ data')
    return result, used, materials


def _dds_texture(identity, raw):
    if len(raw) < 128 or raw[:4] != b'DDS ' or struct.unpack_from('<I', raw, 4)[0] != 124:
        raise ValueError('Invalid native DDS template')
    height, width = struct.unpack_from('<II', raw, 12)
    mips = max(1, struct.unpack_from('<I', raw, 28)[0])
    fmt = {b'DXT1': 4, b'DXT5': 8}.get(raw[84:88])
    if fmt is None or len(raw) != 128 + texture_size(width, height, fmt, mips):
        raise ValueError('Unsupported native DDS layout')
    return dict(id=identity, width=width, height=height, format=fmt, mips=mips,
                payload=raw[128:])


def _encode_texpak(textures):
    table, body = bytearray(), bytearray()
    offset = 8 + len(textures) * 36
    for entry in textures:
        data = entry['payload']
        table.extend(struct.pack('<9I', int(entry['id'][:8], 16), int(entry['id'][8:], 16),
            offset, entry['width'], entry['height'], entry['format'], entry['mips'], 0, 0))
        body.extend(data)
        offset += len(data)
    result = M(0x9f) + struct.pack('<I', len(textures)) + bytes(table) + bytes(body)
    parse_texpak(result)
    return result


def _paint_native(template, image_path):
    """Encode existing accepted pixels at the template's native DDS mip layout."""
    descriptor = _dds_texture('1000000100000000', template)
    fourcc = template[84:88].decode('ascii')
    with Image.open(image_path) as source:
        image = source.convert('RGBA')
    if image.width * descriptor['height'] != image.height * descriptor['width']:
        raise ValueError('Accepted atlas aspect differs from the native template')
    body = bytearray()
    for level in range(descriptor['mips']):
        size = (max(1, descriptor['width'] >> level), max(1, descriptor['height'] >> level))
        output = io.BytesIO()
        image.resize(size, Image.Resampling.LANCZOS).save(output, format='DDS', pixel_format=fourcc)
        encoded = output.getvalue()
        if encoded[84:88] != template[84:88]:
            raise ValueError('DDS encoder changed native block format')
        body.extend(encoded[128:])
    result = template[:128] + bytes(body)
    _dds_texture('1000000100000000', result)
    return result


def _hair_node(node):
    return node['name'] == 'B_tête' and '1000000120213d0a' in node['resources']


def _records(raw, node):
    return [(fmt, i, stream['offset'] + i * stream['stride'], stream['stride'])
            for fmt, stream in node['streams'].items() for i in range(stream['count'])]


def _accepted_rows(repo, character, baseline):
    build = repo / 'face-builder/build'
    info = APPROVED[character]
    if sha((build / info['obj']).read_bytes()) != info['obj_sha256']:
        raise ValueError('Accepted native geometry differs from the approved asset')
    accepted, used, materials = _obj(build / info['obj'])
    albedo = build / info['albedo']
    if sha(albedo.read_bytes()) != info['sha256']:
        raise ValueError('Accepted face albedo differs from the approved asset')
    if 'tex_' + FACE not in materials or sha(materials['tex_' + FACE].read_bytes()) != info['sha256']:
        raise ValueError('Accepted OBJ material does not use the approved face albedo')
    bindings = json.loads((build / 'nadal/sources/native-bindings.json').read_text())
    source = (build / 'nadal/sources/native-source.ooo').read_bytes()
    if sha(source) != bindings['source_native_model_sha256']:
        raise ValueError('Native correspondence source changed')
    source_nodes = parse_model(source)['nodes']
    target_nodes = {n['id']: n for n in parse_model(baseline)['nodes']}
    bound, by_node = {}, {}
    for index, binding in enumerate(bindings['vertices']):
        offset = binding['position_offset']
        nodes = [n for n in source_nodes if n['start'] <= offset < n['end']]
        if len(nodes) != 1:
            raise ValueError('Ambiguous accepted native row')
        source_node = nodes[0]
        if _hair_node(source_node):
            continue
        # The source Houston cache has hair10, whereas the immutable creator
        # snapshot has hair03. Both are excluded by their native hair object ID.
        target = target_nodes.get(source_node['id'])
        if target is not None and _hair_node(target):
            continue
        if target is None:
            raise ValueError('Accepted head object is absent from the native baseline')
        fmt, row = binding['stream_format'], binding['stream_vertex']
        stream = target['streams'].get(fmt)
        if stream is None or row >= stream['count']:
            raise ValueError('Accepted native topology differs from frozen full body')
        target_at = stream['offset'] + row * stream['stride']
        original_uv = struct.unpack_from('<2f', source, offset + 24)
        baseline_uv = struct.unpack_from('<2f', baseline, target_at + 24)
        if not np.allclose(original_uv, baseline_uv, atol=1e-6, rtol=0):
            raise ValueError('Accepted native UV correspondence is inconsistent')
        if index >= len(accepted):
            raise ValueError('Accepted OBJ lost a source-bound head row')
        # Native streams can include undrawn duplicate rows. They still have
        # accepted coordinates and must retain their source-bound row order.
        bound[(target['id'], fmt, row)] = accepted[index].copy()
        by_node.setdefault(target['id'], []).append((target_at, accepted[index].copy()))
    if not any(target_nodes[key]['name'].startswith('M_MRF_VSG') for key in by_node):
        raise ValueError('Accepted model has no source-bound native face')
    return bound, by_node, materials, dict(obj=str(build / info['obj']),
        obj_sha256=sha((build / info['obj']).read_bytes()), albedo_sha256=info['sha256'],
        native_bindings_sha256=sha((build / 'nadal/sources/native-bindings.json').read_bytes()),
        high_native_rows=len(bound))


def _transfer(low, high, revised):
    """Transfer an accepted displacement field with spatial/UV seam constraints.

    Native low LODs have independent skinning and topology. Four neighboring
    original high rows interpolate position, normal and UV *deltas*, so the low
    topology, palette, weights and original seam ownership remain native.
    """
    output = low.copy()
    for first in range(0, len(low), 128):
        query = low[first:first + 128]
        distance = np.sum((query[:, None, :3] - high[None, :, :3]) ** 2, axis=2)
        distance += 100 * np.sum((query[:, None, 6:8] - high[None, :, 6:8]) ** 2, axis=2)
        closest = np.argpartition(distance, min(3, len(high) - 1), axis=1)[:, :min(4, len(high))]
        selected = np.take_along_axis(distance, closest, axis=1)
        weights = 1 / np.maximum(selected, 1e-12) ** 2
        weights /= weights.sum(axis=1, keepdims=True)
        output[first:first + len(query)] += np.sum(weights[..., None] * (revised - high)[closest], axis=1)
    output[:, 3:6] /= np.maximum(np.linalg.norm(output[:, 3:6], axis=1, keepdims=True), 1e-12)
    return output


def _nadal_uv_mapper(repo):
    """Reconstruct the approved continuous UV field, including closed lip rims.

    Interpolating discrete high rows can reverse tiny low mouth triangles where
    the accepted field has a steep slope. Evaluate its original monotone anchors
    on low native UVs instead; no artwork or accepted high row is changed.
    """
    build = repo / 'face-builder/build'
    revision = json.loads((build / 'nadal-reference-revision-v2/revision-proof.json').read_text())
    mouth = json.loads((build / 'nadal-selected-hair3-brown/mouth-proof.json').read_text())
    if revision['albedo_sha256'] != APPROVED['nadal']['sha256'] or mouth['face_albedo_unchanged_sha256'] != APPROVED['nadal']['sha256']:
        raise ValueError('Nadal UV field does not describe the accepted albedo')
    observations = revision['face_observations']

    def smooth(value):
        value = np.clip(value, 0, 1)
        return value * value * (3 - 2 * value)

    def apply(records):
        u, v = records[:, 6], records[:, 7]
        x, y, z = records[:, :3].T
        eyes = (observations['leftEye']['v_down'] + observations['rightEye']['v_down']) * .5
        gap, lip = observations['main_gap'], observations['mouth']['v_down']
        source = [0, .28, .325, .368, .42, .459158242, .484412755, .569027305, .586, .69, .765130222, .86, 1]
        target = [0, .28, .325, eyes, .422, .459158242 + gap[1] - .484412755,
                  gap[1], lip - .017, lip, .70, .765130222, .86, 1]
        central = smooth((u - .34) / .085) * smooth((.67 - u) / .085)
        newv = v + central * (np.interp(v, source, target) - v)
        eye_delta = (observations['leftEye']['v_down'] - observations['rightEye']['v_down']) * .5
        newv += central * np.exp(-.5 * ((v - .368) / .018) ** 2) * np.clip((.505 - u) / .068, -1, 1) * eye_delta
        active = smooth((v - .42) / .025) * smooth((.75 - v) / .055)
        newu = u + central * active * (gap[0] - .504915595)
        nasal = (abs(x) < 2.4) & (y > 165.5) & (y < 169.2) & (z < -6.8) & (u < .09) & (v > .28) & (v < .44)
        newv[nasal] = np.interp(v[nasal], [0, .294, .3092, .34, .385, .40, .43, 1],
            [0, .31, .323, .343, observations['nostril_island_center_v'], .398, .43, 1])
        gate = smooth((v - .532) / .016) * smooth((.78 - v) / .02)
        mapped = v + central * (np.interp(v, mouth['uv_source_anchors'], mouth['uv_target_anchors']) - v)
        newv += gate * (mapped - newv)
        return np.column_stack([newu, newv])

    return apply


def _preserve_low_uv_orientation(before, after):
    """Constrain the coarse native lip triangles to retain their signed area.

    A monotone continuous warp can still reverse a coarse triangle's straight
    edges across several lip anchors. Project only violating triangles toward
    the closest valid UVs; weld duplicate UV rows so native seams remain closed.
    """
    source, revised, offsets, lookup, triangles = [], [], [], {}, []
    for node in parse_model(before)['nodes']:
        if not node['name'].startswith('M_MRF_VSG'):
            continue
        row_keys = {}
        for fmt, i, at, stride in _records(before, node):
            uv = struct.unpack_from('<2f', before, at + 24)
            key = tuple(round(v, 7) for v in uv)
            if key not in lookup:
                lookup[key] = len(source)
                source.append(uv)
                revised.append(struct.unpack_from('<2f', after, at + 24))
                offsets.append([])
            index = lookup[key]
            offsets[index].append(at + 24)
            row_keys[fmt, i] = index
        for draw in node['draw_ranges']:
            for i in range(len(draw['indices']) - 2):
                ids = [row_keys[draw['format'], row] for row in draw['indices'][i:i + 3]]
                if len(set(ids)) == 3:
                    triangles.append(ids)
    source, revised = np.asarray(source), np.asarray(revised)
    initial = revised.copy()
    def area(points):
        a, b, c = points
        d, e = b - a, c - a
        return d[0] * e[1] - d[1] * e[0]
    constraints = [(ids, area(source[ids])) for ids in triangles if abs(area(source[ids])) > 1e-12]
    for _ in range(128):
        corrected = False
        for ids, original in constraints:
            a, b, c = revised[ids]
            sign = 1 if original > 0 else -1
            current = sign * area(revised[ids])
            target = abs(original) * .05
            if current >= target:
                continue
            gradients = sign * np.array([[b[1] - c[1], c[0] - b[0]],
                                         [c[1] - a[1], a[0] - c[0]],
                                         [a[1] - b[1], b[0] - a[0]]])
            revised[ids] += gradients * ((target - current) * 1.01 / np.maximum(np.sum(gradients ** 2), 1e-20))
            corrected = True
        if not corrected:
            break
    else:
        raise ValueError('Cannot preserve native low-LOD mouth UV orientation')
    output = bytearray(after)
    changed = np.max(abs(revised - initial), axis=1) > 1e-10
    for index in np.flatnonzero(changed):
        for at in offsets[index]:
            struct.pack_into('<2f', output, at, *revised[index])
    return bytes(output), dict(constrained_uv_rows=sum(len(offsets[i]) for i in np.flatnonzero(changed)),
        maximum_uv_adjustment=float(np.max(abs(revised - initial))), minimum_area_ratio=.05)


def _deform_model(raw, baseline_high, bound, lod, uv_mapper=None):
    output = bytearray(raw)
    before = parse_model(raw)
    high_nodes = {n['id']: n for n in parse_model(baseline_high)['nodes']}
    changed, changed_rows = set(), 0
    for node in before['nodes']:
        if _hair_node(node):
            continue
        records = _records(raw, node)
        if lod == 0:
            revised = [(at, bound[(node['id'], fmt, i)]) for fmt, i, at, stride in records
                       if (node['id'], fmt, i) in bound]
        else:
            category = ('face' if node['name'].startswith('M_MRF_VSG') else
                        'lashes' if 'M_MRF_CIL' in node['name'] else None)
            if category is None:
                continue
            sources, targets = [], []
            for identity, high_node in high_nodes.items():
                eligible = (high_node['name'].startswith('M_MRF_VSG') if category == 'face'
                            else 'M_MRF_CIL' in high_node['name'])
                if not eligible:
                    continue
                for fmt, i, at, stride in _records(baseline_high, high_node):
                    key = (identity, fmt, i)
                    if key in bound:
                        sources.append(struct.unpack_from('<8f', baseline_high, at))
                        targets.append(bound[key])
            original = np.array([struct.unpack_from('<8f', raw, at) for _, _, at, _ in records])
            transferred = _transfer(original, np.asarray(sources), np.asarray(targets))
            if category == 'face' and uv_mapper is not None:
                transferred[:, 6:8] = uv_mapper(original)
            revised = [(record[2], value) for record, value in zip(records, transferred)]
        for at, values in revised:
            struct.pack_into('<8f', output, at, *values)
        if revised:
            changed.add(node['id'])
            changed_rows += len(revised)
            if node['bounds'] is not None:
                # Rigid head pieces retain native serialization and attachments;
                # update their geometric bounds to the newly accepted positions.
                positions = np.array([struct.unpack_from('<3f', output, at) for _, _, at, _ in records])
                end = min(s['offset'] for s in node['streams'].values())
                bounds_at = raw.rfind(M(0x95), node['start'], end)
                if bounds_at < 0:
                    raise ValueError('Native rigid head bounds are missing')
                struct.pack_into('<6f', output, bounds_at + 8, *positions.min(0), *positions.max(0))
    for node in before['nodes']:
        if node['id'] not in changed and output[node['start']:node['end']] != raw[node['start']:node['end']]:
            raise ValueError('Assembly changed an unrelated native body node')
        # Every suffix after UV contains native skin weights/indices or tangents.
        for _, _, at, stride in _records(raw, node):
            if output[at + 32:at + stride] != raw[at + 32:at + stride]:
                raise ValueError('Assembly changed a native skinning record')
    uv_constraints = None
    if lod == 1:
        output, uv_constraints = _preserve_low_uv_orientation(raw, bytes(output))
    output = bytes(output)
    parse_model(output)
    from character_uv import validate_native_uv
    validate_native_uv(raw, output)
    return output, dict(changed_head_nodes=sorted(changed), adjusted_rows=changed_rows,
        non_head_nodes_byte_identical=True, skin_weights_palettes_indices_preserved=True,
        native_uv_triangle_orientation_preserved=True,
        low_uv_constraints=uv_constraints,
        transfer='exact source-bound accepted rows' if lod == 0 else 'four-neighbor spatial/UV displacement field from approved high LOD')


def _resolve_hair(entries, legacy, character, lod, repo=None):
    info = APPROVED[character]
    prefix = 'media/playercreatorpart/male/haircut/haircut_%02d/' % info['hair']
    source_name = prefix + 'haircut_lod%02d.ooo' % (lod + 1)
    original = unpack_zlb(entries[source_name])
    main = configparser.ConfigParser(interpolation=None)
    main.read_string(unpack_zlb(entries[prefix + 'haircut_%02d.ini' % info['hair']]).decode('cp1252'))
    main = main['Main']
    resources, textures, proof = {}, {}, []
    for kind in ('Diff', 'Refl', 'Spec', 'Bump'):
        for number in range(int(main.get('Num' + kind, '0'))):
            field = '%s_%02d' % (kind, number)
            low = main[field].lower()
            if low == '-1':
                continue
            accessory = field + '_SubSel' in main
            folder = prefix + ('subsel/subsel_%02d/' % int(main[field + '_SubSel']) if accessory else '') + field.lower() + '/'
            choices = sorted(k for k in entries if k.startswith(folder) and k.endswith('.dds'))
            if not choices:
                raise ValueError('Native hairstyle texture is missing: ' + folder)
            selected = next((k for k in choices if k.rsplit('/', 1)[-1].startswith(info['variant'] + '_')), choices[0])
            if accessory and kind == 'Diff':
                if character == 'nadal':
                    repo = Path(repo) if repo else Path(__file__).resolve().parents[2]
                    selected = str((repo / 'face-builder/build' / info['cap_dds']).resolve())
                    dds = Path(selected).read_bytes()
                    if sha(dds) != info['cap_dds_sha256']:
                        raise ValueError('Nadal green cap differs from the selected material')
                else:
                    selected = 'build/Nadal-2010-Research/headband-black-candidate/1000000120213d0a.dds'
                    dds = (legacy / selected).read_bytes()
            else:
                dds = unpack_zlb(entries[selected])
            # Original creator Diff IDs carry the native shader texture class.
            # Reflection/specular/bump records are all native generic maps.
            old_hi = {'Diff': None, 'Refl': 6, 'Spec': 7, 'Bump': 5}[kind]
            identity_low = zlib.crc32(('djultra64/approved-hair/' + character + '/' + field + '/' + sha(dds)).encode())
            resources[low] = (kind, old_hi, identity_low, dds)
            proof.append(dict(slot=field, source=selected, sha256=sha(dds)))
    changed = bytearray(original)
    at = original.index(M(0x8b))
    count = struct.unpack_from('<I', original, at + 4)[0]
    if original[at + 8 + count * 8:at + 12 + count * 8] != M(0x8c):
        raise ValueError('Invalid standalone native hair resource table')
    for index in range(count):
        position = at + 8 + index * 8
        low, high = struct.unpack_from('<II', original, position)
        kind, expected_high, new_low, dds = resources.get('%08x' % low, (None, None, None, None))
        if kind is None or (expected_high is not None and high != expected_high) or (kind == 'Diff' and high not in (1, 3)):
            raise ValueError('Native hair reference does not match its original INI')
        typed_high = 0x10000000 + (high if kind == 'Diff' else 6)
        identity = '%08x%08x' % (typed_high, new_low)
        struct.pack_into('<II', changed, position, new_low, typed_high)
        textures[identity] = _dds_texture(identity, dds)
    report = parse_model(bytes(changed))
    if len(report['nodes']) != 1 or report['nodes'][0]['name'] != 'B_tête':
        raise ValueError('Selected native hairstyle is not one rigid head leaf')
    node = report['nodes'][0]
    leaf = bytes(changed[node['start']:node['end']])
    # The native saved full-body compiler writes MA3; extend MA2 descriptors
    # without touching their existing descriptor fields or any geometry bytes.
    marker = leaf.index(M(0x8c))
    count = struct.unpack_from('<I', leaf, marker + 4)[0]
    cursor, material = marker + 8, bytearray()
    for _ in range(count):
        version = leaf[cursor + 84:cursor + 92]
        stride = 108 if version == b'PAM-MA2\0' else 112
        descriptor = leaf[cursor:cursor + stride]
        if version == b'PAM-MA2\0':
            descriptor = descriptor[:90] + b'3' + descriptor[91:] + bytes(4)
        elif version != b'PAM-MA3\0':
            raise ValueError('Invalid native hairstyle material')
        material.extend(descriptor)
        cursor += stride
    leaf = leaf[:marker + 8] + bytes(material) + leaf[cursor:]
    return leaf, textures, dict(native_id=info['hair'], display_number=info['hair'] + 1,
        color_variant=info['variant'], source=source_name, source_sha256=sha(original),
        textures=proof, native_vertex_records_preserved=True)


def validate_hair_attachment(raw, expected):
    """Require the selected style in each native logical detail variant.

    The approved Federer band may be part of its selected leaf; body/eye rigid
    head objects are independent. Validate identity and geometry rather than
    treating every head attachment or accessory as another hairstyle.
    """
    nodes = parse_model(raw)['nodes']
    selected = [node for node in nodes if node['id'] == expected['id']]
    if len(selected) != 1 or selected[0]['name'] != 'B_tête':
        raise ValueError('Model is missing its selected native hair attachment')
    actual = selected[0]['streams']
    wanted = expected['streams']
    if actual.keys() != wanted.keys() or any(
            any(actual[fmt][field] != stream[field] for field in ('count', 'stride', 'sha256'))
            for fmt, stream in wanted.items()):
        raise ValueError('Model changed the selected native hair geometry')
    from hair_lod_variants import PAIRS, same_selected_leaf
    far_identity = PAIRS.get(expected['id'])
    far = [node for node in nodes if node['id'] in PAIRS.values()]
    if far_identity:
        if len(far) != 1 or far[0]['id'] != far_identity or not same_selected_leaf(raw, selected[0], far[0]):
            raise ValueError('Model retains obsolete native hair or lacks its selected far detail')
    elif far:
        raise ValueError('High model retains an obsolete native hair attachment')
    return dict(selected_node=expected['id'],
        selected_rows=sum(stream['count'] for stream in actual.values()),
        selected_native_streams_exact=True, obsolete_saved_hair_absent=True,
        far_variant_selected_style_exact=bool(far_identity))


def _graft(raw, leaf):
    parsed = parse_model(raw)
    old_hair = [n for n in parsed['nodes'] if _hair_node(n)]
    if not old_hair:
        raise ValueError('Native full-body hair attachment is missing')
    # Keep the saved object's ID and the authentic selected attachment header.
    leaf = leaf[:4] + raw[old_hair[0]['start'] + 4:old_hair[0]['start'] + 8] + leaf[8:]
    nodes, inserted = [], False
    for node in parsed['nodes']:
        if _hair_node(node):
            if not inserted:
                nodes.append((0x79, leaf)); inserted = True
        else:
            nodes.append((node['kind'], raw[node['start']:node['end']]))
    result = M(0x7c) + struct.pack('<I', len(nodes))
    if raw[8:21] == b'VertexBufSize':
        # The final typed parser recomputes/validates this declaration below.
        stream_report = parse_model(M(0x7c) + struct.pack('<II', 1, 0x12345679) + leaf + M(0x94) + bytes(4))
        removed = sum(s['count'] * s['stride'] for n in old_hair for s in n['streams'].values())
        result += b'VertexBufSize' + struct.pack('<I', parsed['vertex_bytes'] - removed + stream_report['vertex_bytes'])
    result += b''.join(M(kind) for kind, _ in nodes) + b''.join(data for _, data in nodes) + M(0x94) + bytes(4)
    converted, conversion = convert_model(result)
    from hair_lod_variants import ensure_model_variants
    converted, detail_variants = ensure_model_variants(converted)
    selected = parse_model(M(0x7c) + struct.pack('<II', 1, 0x12345679) + leaf + M(0x94) + bytes(4))['nodes'][0]
    hair_consistency = validate_hair_attachment(converted, selected)
    return converted, dict(removed_old_hair_objects=len(old_hair), native_objects=parse_model(converted)['objects'],
        hair_consistency=hair_consistency, hair_detail_variants=detail_variants, **conversion)


def assemble_models(character, assets, research, repo=None):
    """Return ({(outfit,lod): raw OOO}, {outfit: raw Tex.pak}, evidence).

    Both outfits retain their authentic frozen body, garment geometry and
    skinning, with selected existing Xbox garment diffuse pixels. High faces
    reproduce the accepted OBJ exactly; low faces sample its displacement field
    on the independent native low topology. Federer is outside this API.
    """
    if character not in APPROVED:
        raise ValueError('Only approved native athlete assembly is supported')
    repo = Path(repo).resolve() if repo else Path(__file__).resolve().parents[2]
    research = Path(research).resolve()
    legacy = research if research.name == 'legacy' else research / 'legacy'
    assets = Path(assets).resolve()
    snapshot = legacy / 'build/Federer-Creator-Save-Snapshot'
    originals = {(o, l): unpack_zlb((snapshot / ('Player%02dLod%02d.ooo' % (o, l))).read_bytes())
                 for o in range(2) for l in range(2)}
    inventory = json.loads((snapshot / 'private-save-inventory.json').read_text())
    for (outfit, lod), raw in originals.items():
        name = 'Player%02dLod%02d.ooo' % (outfit, lod)
        if sha((snapshot / name).read_bytes()) != inventory[name]:
            raise ValueError('Frozen full-body source changed: ' + name)
    bound, _, materials, accepted_proof = _accepted_rows(repo, character, originals[0, 0])
    uv_mapper = _nadal_uv_mapper(repo) if character == 'nadal' else None
    creator = payloads((legacy / 'build/Federer-Creator-Test/Media/Wads/PlayerCreatorPart.wad').read_bytes())
    textures = {}
    for path in sorted(snapshot.glob('1*.dds')):
        if re.fullmatch('[0-9a-f]{16}', path.stem):
            source = path.read_bytes()
            if sha(source) != inventory.get(path.name):
                raise ValueError('Frozen native texture source changed: ' + path.name)
            textures[path.stem] = _dds_texture(path.stem, unpack_zlb(source))
    required = {identity for raw in originals.values() for identity in parse_model(raw)['texture_ids']}
    for identity in sorted(required - textures.keys()):
        key = 'media/playercreatorpart/commontextures/' + identity + '.dds'
        textures[identity] = _dds_texture(identity, unpack_zlb(creator[key]))
    for identity in (FACE, EYE):
        path = materials.get('tex_' + identity)
        if path is None or not path.is_file():
            raise ValueError('Accepted head texture is missing: ' + identity)
        template = unpack_zlb((snapshot / (identity + '.dds')).read_bytes())
        textures[identity] = _dds_texture(identity, _paint_native(template, path))
    accepted_proof['material_sources'] = {identity: dict(path=str(materials['tex_' + identity]),
        sha256=sha(materials['tex_' + identity].read_bytes())) for identity in (FACE, EYE)}
    hair_entries = payloads((legacy / 'build/Federer-Creator-Test/Media/Wads/PlayerCreatorMaleHairCut.wad').read_bytes())
    figure = unpack_zlb(payloads((assets / 'Media/Wads/GamePart.wad').read_bytes())['media/gamepart/default.fig'])
    models, packs, proofs, wardrobe_proofs = {}, {}, [], []
    for outfit in range(2):
        outfit_models, combined = {}, dict(textures)
        for lod in range(2):
            source = originals[outfit, lod]
            deformed, head = _deform_model(source, originals[0, 0], bound, lod, uv_mapper)
            leaf, hair_textures, hair = _resolve_hair(hair_entries, legacy, character, lod, repo)
            combined.update(hair_textures)
            model, conversion = _graft(deformed, leaf)
            name = 'playerlod%02d.ooo' % lod
            outfit_models[name] = model
            proofs.append(dict(outfit=outfit, lod=lod, source_sha256=sha(source), head=head, hair=hair, conversion=conversion))
        pack = _encode_texpak(list(combined.values()))
        # Apply genuine native garment textures while the original typed IDs are
        # available. Enforce the material-only contract independently of the
        # wardrobe selector: all model bytes and every non-cloth payload stay put.
        from approved_roster_wardrobe import apply as apply_wardrobe
        frozen_models = dict(outfit_models)
        texture_before = {row['id']: row for row in parse_texpak(pack)}
        cloth_diffuse = {identity for raw in outfit_models.values()
            for node in parse_model(raw)['nodes']
            if node['name'].startswith(('M_MRF_VET_', 'M_VET_CHS_', 'M_VET_CHT_'))
            for identity in node['resources'] if identity.startswith('10000001')}
        outfit_models, pack, wardrobe_proof = apply_wardrobe(outfit_models, pack, assets, character, outfit)
        if outfit_models != frozen_models:
            raise ValueError('Approved wardrobe changed native model bytes')
        texture_after = {row['id']: row for row in parse_texpak(pack)}
        if texture_before.keys() != texture_after.keys():
            raise ValueError('Approved wardrobe changed native texture identities')
        for identity, before in texture_before.items():
            after = texture_after[identity]
            if any(before[field] != after[field] for field in before if field != 'payload_sha256'):
                raise ValueError('Approved wardrobe changed native texture layout')
            if identity not in cloth_diffuse and before['payload_sha256'] != after['payload_sha256']:
                raise ValueError('Approved wardrobe changed a non-cloth texture')
        wardrobe_proofs.append(dict(wardrobe_proof, outfit=outfit))
        outfit_models, pack, mapping = namespace_textures(outfit_models, pack, 'approved-' + character)
        validate_models(outfit_models, pack, figure=figure)
        for lod in range(2):
            models[outfit, lod] = outfit_models['playerlod%02d.ooo' % lod]
        packs[outfit] = pack
    return models, packs, dict(author='djultra64', schema='maxspin-approved-native-models-v1',
        character=character, accepted=accepted_proof, models=proofs, wardrobe=wardrobe_proofs,
        source_fullbody='frozen native creator snapshot', figure_sha256=sha(figure),
        texture_dependencies_closed=True, source_files_modified=False,
        limitations='Structural native validation complete; in-game appearance, animation and runtime rendering require actual title verification.')
