"""Preserve custom cutout hair coverage at distance. Own work: djultra64.

Only lower DXT5 alpha blocks of the selected head attachment can change.
Original native assets, level zero, RGB blocks and model/material data stay exact.
This is an explicit package quality operation, independent of renderer state.
"""
import struct

from PIL import Image

from character_formats import (pack_zlb, parse_model, parse_texpak, payloads,
                               rebuild_pam, sha, texture_size, unpack_zlb)


def _palette(a, b):
    if a > b:
        return [a, b]+[((7-i)*a+i*b)//7 for i in range(1, 7)]
    return [a, b]+[((5-i)*a+i*b)//5 for i in range(1, 5)]+[0, 255]


def _decode_alpha(raw, width, height):
    values = [0]*(width*height)
    at = 0
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            palette = _palette(raw[at], raw[at+1])
            indices = int.from_bytes(raw[at+2:at+8], 'little')
            for row in range(min(4, height-y)):
                for column in range(min(4, width-x)):
                    values[(y+row)*width+x+column] = palette[(indices >> (3*(row*4+column))) & 7]
            at += 16
    return values


def _encode_alpha(values, width, height, scale):
    result = bytearray()
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            block = [min(255, round(values[min(y+row, height-1)*width+min(x+column, width-1)]*scale))
                     for row in range(4) for column in range(4)]
            a, b = max(block), min(block)
            palette = _palette(a, b)
            indices = sum(min(range(8), key=lambda i: abs(palette[i]-value)) << (3*k)
                          for k, value in enumerate(block))
            result.extend(bytes((a, b))+indices.to_bytes(6, 'little')+bytes(8))
    return bytes(result)


def _correct_payload(raw, width, height, mips):
    if (not 1 <= width <= 4096 or not 1 <= height <= 4096
            or not 1 <= mips <= max(width, height).bit_length()
            or len(raw) != texture_size(width, height, 8, mips)):
        raise ValueError('Invalid bounded native hair DXT5 mip chain')
    source = _decode_alpha(raw, width, height)
    coverage = sum(value >= 128 for value in source)/len(source)
    result, levels, at = bytearray(raw), [], 0
    image = Image.frombytes('L', (width, height), bytes(source))
    for level in range(mips):
        w, h = max(1, width >> level), max(1, height >> level)
        size = ((w+3)//4)*((h+3)//4)*16
        original = raw[at:at+size]
        before = sum(a >= 128 for a in _decode_alpha(original, w, h))
        target = max(1, round(coverage*w*h)) if coverage else 0
        changed = False
        # Keep healthy authored alpha; never create coverage in an invisible map.
        if level and coverage and (not before or target-before > max(1, w*h*.05)):
            alpha = list(image.resize((w, h), Image.Resampling.LANCZOS).getdata())
            best, low, high = None, 0., 8.
            for scale in [1., 0., 8.]+[None]*20:
                if scale is None:
                    scale = (low+high)/2
                candidate = _encode_alpha(alpha, w, h, scale)
                count = sum(a >= 128 for a in _decode_alpha(candidate, w, h))
                score = (abs(count-target), abs(scale-1))
                if best is None or score < best[0]:
                    best = (score, candidate)
                if count < target:
                    low = scale
                else:
                    high = scale
            candidate = best[1]
            for block in range(0, size, 16):
                result[at+block:at+block+8] = candidate[block:block+8]
            changed = bytes(result[at:at+size]) != original
        after = sum(a >= 128 for a in _decode_alpha(result[at:at+size], w, h))
        levels.append(dict(level=level, width=w, height=h, covered_before=before,
                           covered_after=after, pixels=w*h, changed=changed))
        at += size
    output = bytes(result)
    return output, dict(author='djultra64', source_sha256=sha(raw), sha256=sha(output),
                        threshold=128, base_coverage=coverage, levels=levels,
                        changed_levels=sum(level['changed'] for level in levels),
                        base_and_rgb_blocks_byte_identical=True)


def correct_dds(raw):
    """Return an explicit DXT5 hair candidate and CPU coverage evidence."""
    if (len(raw) < 128 or raw[:4] != b'DDS ' or struct.unpack_from('<I', raw, 4)[0] != 124
            or raw[84:88] != b'DXT5'):
        raise ValueError('Hair coverage correction requires a native DXT5 DDS')
    height, width = struct.unpack_from('<II', raw, 12)
    output, proof = _correct_payload(raw[128:], width, height,
                                    max(1, struct.unpack_from('<I', raw, 28)[0]))
    return raw[:128]+output, proof


def correct_texture_pack(models, source):
    """Apply the explicit custom hair quality policy before package validation."""
    selected = set()
    for data in models.values():
        for node in parse_model(data)['nodes']:
            if node['id'] in ('13d59455', '75ed28ac') and node['name'] == 'B_tête':
                selected.update(identity for identity in node['resources']
                                if identity[:8] in ('10000001', '10000003'))
    pack, proofs, changed = bytearray(source), [], []
    for entry in parse_texpak(source):
        if entry['id'] not in selected or entry['format'] != 8:
            continue
        at, size = entry['offset'], entry['bytes']
        candidate, proof = _correct_payload(source[at:at+size], entry['width'], entry['height'], entry['mips'])
        proofs.append(dict(proof, texture_id=entry['id']))
        if candidate != source[at:at+size]:
            pack[at:at+size] = candidate
            changed.append(entry['id'])
    return bytes(pack), dict(author='djultra64', source_sha256=sha(source), sha256=sha(pack),
                        changed_texture_ids=changed, textures=proofs,
                        policy='Selected custom DXT5 hair only; preserve base alpha-test coverage in degraded lower mips',
                        threshold=128, base_and_rgb_blocks_byte_identical=True,
                        models_and_materials_byte_identical=True)


def correct_archive(raw):
    """Correct selected custom hair diffuse maps; retain every other WAD member."""
    members = payloads(raw)
    models = {name:unpack_zlb(data) for name,data in members.items()
              if name.endswith(('/playerlod00.ooo', '/playerlod01.ooo'))}
    packs = [name for name in members if name.endswith('/textures/tex.pak')]
    if len(packs) != 1 or not models:
        raise ValueError('Custom hair correction requires one native pack and complete models')
    name = packs[0]
    pack, proof = correct_texture_pack(models, unpack_zlb(members[name]))
    output = rebuild_pam(raw, {name:pack_zlb(pack, members[name])}) if proof['changed_texture_ids'] else raw
    return output, dict(proof, source_sha256=sha(raw), sha256=sha(output),
                        geometry_materials_and_other_members_byte_identical=True)
