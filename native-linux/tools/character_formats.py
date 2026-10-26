"""Bounded private native character resource readers. Own work: djultra64.

PAM/PML/ZLB contracts retain the earlier djultra64 character research readers;
this module has no dependency on that private research or its installation.
"""
from dataclasses import dataclass
import configparser
import hashlib
import math
import re
import struct
import zlib

MAX_RESOURCE = 64 * 1024 * 1024


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _archive(raw):
    if not isinstance(raw, bytes) or not 36 <= len(raw) <= 256 * 1024 * 1024 or raw[:8] != b'PAM_PAK\0':
        raise ValueError('Invalid PAM archive header')
    count, strings, pool_size, base, c1, c2, c3, c4 = struct.unpack_from('<HH6I', raw, 8)
    end = 36 + count * 32
    if not count or end + pool_size > base or base > len(raw):
        raise ValueError('Invalid PAM archive tables')
    arrays = raw[36:36 + count * 8]
    lookup = raw[36 + count * 8:end]
    pool = raw[end:end + pool_size]
    if (pool.count(b'\0'), zlib.crc32(arrays), zlib.crc32(lookup),
        zlib.crc32(pool), zlib.crc32(raw[:32])) != (strings, c1, c2, c3, c4):
        raise ValueError('PAM archive checksum mismatch')
    rows = {}
    indices = set()
    for i in range(count):
        key, index_flags, name_at, flags, parent, padding = struct.unpack_from('<6I', lookup, i * 24)
        index = index_flags & 0xffff
        if key in rows or index in indices or index >= count or name_at >= pool_size:
            raise ValueError('Invalid PAM archive lookup')
        name_end = pool.find(b'\0', name_at)
        if name_end < 0:
            raise ValueError('Unterminated PAM archive name')
        name = pool[name_at:name_end].decode('cp1252')
        if not name or name in ('.', '..') or any(c in name for c in '/\\:\0'):
            raise ValueError('Unsafe PAM archive name')
        size, offset = struct.unpack_from('<II', arrays, index * 8)
        if offset != 0xffffffff and (size > MAX_RESOURCE or offset < base or offset + size > len(raw)):
            raise ValueError('Invalid PAM archive payload bounds')
        rows[key] = {'key': key, 'index': index, 'name': name, 'parent': parent,
                     'size': size, 'offset': offset}
        indices.add(index)
    def path(key, seen=()):
        if key not in rows or key in seen or len(seen) > 64:
            raise ValueError('Invalid PAM archive parent hierarchy')
        row = rows[key]
        return (path(row['parent'], seen + (key,)) + '/' if row['parent'] else '') + row['name']
    entries = {}
    for key, row in rows.items():
        name = path(key).lower()
        if zlib.crc32(name.replace('/', '\\').encode('cp1252')) != key:
            raise ValueError('PAM archive path checksum mismatch')
        if row['offset'] != 0xffffffff:
            if name in entries:
                raise ValueError('Duplicate PAM archive member')
            entries[name] = row
    return entries


def payloads(raw):
    return {name: raw[row['offset']:row['offset'] + row['size']]
            for name, row in _archive(raw).items()}


def rebuild_pam(raw, replacements):
    entries = _archive(raw)
    replacements = {name.replace('\\', '/').lower(): data for name, data in replacements.items()}
    if not set(replacements) <= entries.keys() or any(not isinstance(v, bytes) or len(v) > MAX_RESOURCE for v in replacements.values()):
        raise ValueError('Unknown or oversized PAM archive replacement')
    if all(raw[entries[k]['offset']:entries[k]['offset'] + entries[k]['size']] == v for k, v in replacements.items()):
        return raw
    count = struct.unpack_from('<H', raw, 8)[0]
    base = struct.unpack_from('<I', raw, 16)[0]
    table = bytearray(raw[36:36 + count * 8])
    body = bytearray()
    by_index = {row['index']: (name, row) for name, row in entries.items()}
    for i in range(count):
        if i not in by_index:
            continue
        name, row = by_index[i]
        value = replacements.get(name, raw[row['offset']:row['offset'] + row['size']])
        body.extend(bytes((-len(body)) % 4))
        struct.pack_into('<II', table, i * 8, len(value), base + len(body))
        body.extend(value)
    result = bytearray(raw[:base])
    result[36:36 + count * 8] = table
    struct.pack_into('<I', result, 20, zlib.crc32(table))
    struct.pack_into('<I', result, 32, zlib.crc32(result[:32]))
    result.extend(body)
    result = bytes(result)
    now = payloads(result)
    for name, row in entries.items():
        expected = replacements.get(name, raw[row['offset']:row['offset'] + row['size']])
        if now[name] != expected:
            raise ValueError('PAM archive preservation failed')
    return result


def unpack_zlb(raw):
    if len(raw) > MAX_RESOURCE:
        raise ValueError('Resource exceeds supported size')
    if not raw.startswith(b'PAM-ZLB\0'):
        return raw
    if len(raw) < 44:
        raise ValueError('Truncated PAM-ZLB header')
    count, chunk, last, total, c1, c2, c3 = struct.unpack_from('<7I', raw, 8)
    if not 1 <= count <= 4096 or not 0 < last <= chunk <= 16 * 1024 * 1024 or total != (count - 1) * chunk + last or total > MAX_RESOURCE:
        raise ValueError('Invalid PAM-ZLB dimensions')
    end = 36 + count * 8
    offsets_data, crcs_data = raw[36:36 + count * 4], raw[36 + count * 4:end]
    if len(crcs_data) != count * 4 or (zlib.crc32(offsets_data), zlib.crc32(crcs_data), zlib.crc32(raw[8:32])) != (c1, c2, c3):
        raise ValueError('PAM-ZLB checksum mismatch')
    offsets = list(struct.unpack('<' + 'I' * count, offsets_data)) + [len(raw)]
    crcs = struct.unpack('<' + 'I' * count, crcs_data)
    if offsets[0] != end or any(a >= b for a, b in zip(offsets, offsets[1:])):
        raise ValueError('Invalid PAM-ZLB block offsets')
    output = []
    for i, (a, b) in enumerate(zip(offsets, offsets[1:])):
        stored = raw[a:b]
        expected = last if i == count - 1 else chunk
        if zlib.crc32(stored) != crcs[i]:
            raise ValueError('PAM-ZLB block checksum mismatch')
        if len(stored) == expected:
            decoded = stored
        else:
            decoder = zlib.decompressobj()
            try:
                decoded = decoder.decompress(stored, expected + 1)
            except zlib.error as error:
                raise ValueError('Invalid PAM-ZLB compressed stream') from error
            if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                raise ValueError('Incomplete PAM-ZLB compressed stream')
        if len(decoded) != expected:
            raise ValueError('PAM-ZLB decoded length mismatch')
        output.append(decoded)
    return b''.join(output)


def pack_zlb(raw, template=None):
    if template is not None and not template.startswith(b'PAM-ZLB\0'):
        return raw
    if not raw or len(raw) > MAX_RESOURCE:
        raise ValueError('Invalid PAM-ZLB resource size')
    chunk = min(len(raw), 524288)
    blocks = []
    for at in range(0, len(raw), chunk):
        data = raw[at:at + chunk]
        compressed = zlib.compress(data, 9)
        blocks.append(compressed if len(compressed) < len(data) else data)
    offset = 36 + len(blocks) * 8
    offsets = []
    for block in blocks:
        offsets.append(offset)
        offset += len(block)
    offsets_data = struct.pack('<' + 'I' * len(offsets), *offsets)
    crcs_data = struct.pack('<' + 'I' * len(blocks), *(zlib.crc32(b) for b in blocks))
    header = b'PAM-ZLB\0' + struct.pack('<6I', len(blocks), chunk,
        len(raw) - (len(blocks) - 1) * chunk, len(raw), zlib.crc32(offsets_data), zlib.crc32(crcs_data))
    return header + struct.pack('<I', zlib.crc32(header[8:32])) + offsets_data + crcs_data + b''.join(blocks)


@dataclass
class Node:
    key: int
    children: list
    properties: list


class Pml:
    def __init__(self, data):
        if len(data) < 24 or len(data) > MAX_RESOURCE:
            raise ValueError('Invalid PML size')
        magic, end, count = struct.unpack_from('<III', data)
        if magic or not 24 <= end <= len(data) or count > 65536:
            raise ValueError('Invalid PML header')
        self.dictionary, self.name_count = data[end:], count
        names = self.dictionary.rstrip(b'\0').split(b'\0')
        if len(names) != count:
            raise ValueError('Invalid PML dictionary')
        self.names = {zlib.crc32(n.lower()): n.decode('cp1252') for n in names}
        if len(self.names) != count:
            raise ValueError('Duplicate PML dictionary name')
        seen = set()
        def read(at, depth=0):
            if depth > 64 or at in seen or not 12 <= at <= end - 12:
                raise ValueError('Invalid PML node offset')
            seen.add(at)
            nc, np, key = struct.unpack_from('<III', data, at)
            if nc + np > (end - at - 12) // 4:
                raise ValueError('Invalid PML node table')
            offsets = struct.unpack_from('<' + 'I' * (nc + np), data, at + 12)
            props = []
            prop_keys = set()
            for p in offsets[:np]:
                if p < 12 or p + 4 >= end:
                    raise ValueError('Invalid PML property offset')
                name = struct.unpack_from('<I', data, p)[0]
                value_end = data.find(b'\0', p + 4, end)
                if value_end < 0 or name in prop_keys:
                    raise ValueError('Invalid PML property')
                prop_keys.add(name)
                props.append((name, data[p + 4:value_end].decode('cp1252')))
            return Node(key, [read(p, depth + 1) for p in offsets[np:]], props)
        self.root = read(12)
        self._original = data
        self._before = self._fingerprint()

    @staticmethod
    def key(name):
        return zlib.crc32(name.lower().encode('cp1252'))

    def _fingerprint(self):
        def walk(n):
            return n.key, tuple(n.properties), tuple(walk(c) for c in n.children)
        return walk(self.root)

    def node(self, path):
        node = self.root
        for part in path.split('/') if path else []:
            if '=' in part:
                field, value = part.split('=', 1)
                matches = [c for c in node.children if (self.key(field), value) in c.properties]
            else:
                matches = [c for c in node.children if c.key == self.key(part)]
            if len(matches) != 1:
                raise ValueError('PML selector must match exactly one node: ' + path)
            node = matches[0]
        return node

    def get(self, path, name):
        props = dict(self.node(path).properties)
        if self.key(name) not in props:
            raise ValueError('Missing PML property: ' + name)
        return props[self.key(name)]

    def set(self, path, name, value):
        node = self.node(path)
        for i, (key, old) in enumerate(node.properties):
            if key == self.key(name):
                value = str(value)
                if '\0' in value:
                    raise ValueError('Invalid PML property value')
                node.properties[i] = key, value
                return
        raise ValueError('Missing PML property: ' + name)

    def encode(self):
        if self._fingerprint() == self._before:
            return self._original
        out = bytearray(12)
        def write(node):
            start = len(out)
            nc, np = len(node.children), len(node.properties)
            out.extend(struct.pack('<III', nc, np, node.key))
            table = len(out)
            out.extend(bytes(4 * (nc + np) + (4 if not nc else 0)))
            offsets = []
            for key, value in node.properties:
                offsets.append(len(out))
                out.extend(struct.pack('<I', key) + value.encode('cp1252') + b'\0')
                out.extend(bytes((-len(out)) % 4))
            offsets.extend(write(c) for c in node.children)
            struct.pack_into('<' + 'I' * len(offsets), out, table, *offsets)
            return start
        write(self.root)
        struct.pack_into('<III', out, 0, 0, len(out), self.name_count)
        out.extend(self.dictionary)
        return bytes(out)


def texture_size(width, height, fmt, mips):
    if not 1 <= width <= 4096 or not 1 <= height <= 4096 or width & (width - 1) or height & (height - 1) or fmt not in (0, 4, 8) or not 1 <= mips <= max(width, height).bit_length():
        raise ValueError('Unsupported native texture dimensions or format')
    return sum(max(1, width >> l) * max(1, height >> l) * 4 if fmt == 0 else
        max(1, (max(1, width >> l) + 3) // 4) * max(1, (max(1, height >> l) + 3) // 4) * (8 if fmt == 4 else 16)
        for l in range(mips))


def parse_texpak(raw, *, original_source=False):
    """Parse bounded texture rows; retain stricter namespaces for replacements.

    Frozen Rios/Emory originals also contain namespaces 6 and 0x10000007.
    Only the hash-bound original comparison opts in; candidate packs continue
    to use the original replacement namespace allowlist.
    """
    if len(raw) < 8 or len(raw) > MAX_RESOURCE or struct.unpack_from('<I', raw)[0] != 0x1234569f:
        raise ValueError('Invalid native texture pack')
    count = struct.unpack_from('<I', raw, 4)[0]
    if not 1 <= count <= 2048 or 8 + 36 * count > len(raw):
        raise ValueError('Invalid native texture count')
    at = 8 + count * 36
    entries, ids = [], set()
    namespaces = {0x10000001, 0x10000003, 0x10000006}
    if original_source:
        namespaces.update((0x00000006, 0x10000007))
    for i in range(count):
        hi, lo, offset, w, h, fmt, mips, f0, f1 = struct.unpack_from('<9I', raw, 8 + i * 36)
        name = '%08x%08x' % (hi, lo)
        size = texture_size(w, h, fmt, mips)
        if hi not in namespaces or name in ids or f0 or f1 or offset != at or offset + size > len(raw):
            raise ValueError('Invalid native texture row')
        ids.add(name)
        entries.append({'id': name, 'width': w, 'height': h, 'format': fmt, 'mips': mips,
                        'payload_sha256': sha(raw[offset:offset + size]), 'offset': offset, 'bytes': size})
        at += size
    if at != len(raw):
        raise ValueError('Unconsumed native texture data')
    return entries


class _Cursor:
    def __init__(self, raw):
        self.raw, self.at = raw, 0

    def take(self, size):
        if size < 0 or self.at + size > len(self.raw):
            raise ValueError('Truncated native model at ' + str(self.at))
        raw = self.raw[self.at:self.at + size]
        self.at += size
        return raw

    def values(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))

    def u32(self):
        return self.values('<I')[0]

    def marker(self, suffix):
        at = self.at
        if self.u32() != 0x12345600 + suffix:
            raise ValueError('Unexpected native model marker at %d (expected %02x)' % (at, suffix))

    def peek(self, value):
        return self.raw[self.at:self.at + len(value)] == value


def _finite(values):
    if not all(math.isfinite(v) for v in values):
        raise ValueError('Nonfinite native model value')


def parse_model(raw):
    """Consume the Xbox root, rigid/skin records and every typed geometry byte."""
    if not 16 <= len(raw) <= MAX_RESOURCE:
        raise ValueError('Invalid native model size')
    c = _Cursor(raw)
    c.marker(0x7c)
    count = c.u32()
    if not 1 <= count <= 256:
        raise ValueError('Invalid native model object count')
    declared_bytes = None
    if c.peek(b'VertexBufSize'):
        c.take(13)
        declared_bytes = c.u32()
    types = [c.u32() for _ in range(count)]
    if any(t not in (0x12345679, 0x1234569b) for t in types):
        raise ValueError('Unsupported native model object type')
    nodes, refs, global_refs, vertices, vertex_bytes = [], set(), set(), 0, 0
    for kind in types:
        start = c.at
        c.marker(kind & 255)
        object_id = c.u32()
        userdata = None
        if kind == 0x12345679 and c.peek(b'PAM-OBJ\0'):
            userdata = [c.at, c.at + 136]
            c.take(136)
        header = c.take(20)
        name = ''
        if c.peek(b'OBJECT_NAME'):
            c.take(11)
            length = c.u32()
            if not 1 <= length <= 256:
                raise ValueError('Invalid native object name')
            name = c.take(length).decode('cp1252')
            if '\0' in name:
                raise ValueError('Invalid native object name')
        c.marker(0x8b)
        nr = c.u32()
        if nr > 64:
            raise ValueError('Invalid model texture reference count')
        resources = ['%08x%08x' % (hi, lo) for lo, hi in (c.values('<II') for _ in range(nr))]
        resource_offsets = [c.at - nr * 8 + i * 8 for i in range(nr)]
        if any(int(t[:8], 16) not in (0x10000001, 0x10000003, 0x10000006) and t != '00000001fcd68a40' for t in resources):
            raise ValueError('Unsupported native texture reference type')
        # Exact built-in eye resource also occurs in the Xbox creator originals.
        # It is a global material resource, not a missing Tex.pak texture.
        refs.update(t for t in resources if t != '00000001fcd68a40')
        global_refs.update(t for t in resources if t == '00000001fcd68a40')
        c.marker(0x8c)
        nm = c.u32()
        if nm > 64:
            raise ValueError('Invalid native material count')
        for i in range(nm):
            first = c.take(108)
            _finite(struct.unpack_from('<16f', first))
            if first[84:92] == b'PAM-MA3\0':
                c.take(4)
            elif first[84:92] != b'PAM-MA2\0':
                raise ValueError('Unsupported native material version')
        c.marker(0x8e)
        nops, nbytes = c.values('<II')
        if nops > 8192 or nbytes > 131072 or nbytes % 4:
            raise ValueError('Invalid native draw program size')
        program = list(struct.unpack('<' + 'I' * (nbytes // 4), c.take(nbytes)))
        c.marker(0x84)
        ni = c.u32()
        if ni > 1000000:
            raise ValueError('Invalid native index count')
        indices = c.values('<' + 'H' * ni)
        index_flags = c.u32()
        if index_flags not in (0, 1, 8, 24):
            raise ValueError('Unsupported native index metadata %08x for %s at %d' % (index_flags, name, c.at - 4))
        bounds = None
        if kind == 0x12345679:
            c.marker(0x95)
            if c.u32() != 1:
                raise ValueError('Unsupported native rigid bounds')
            bounds = c.values('<6f')
            _finite(bounds)
            if any(bounds[i] > bounds[i + 3] for i in range(3)):
                raise ValueError('Invalid native rigid bounds')
        node_declared = None
        if c.peek(b'VertexBufSize'):
            c.take(13)
            node_declared = c.u32()
        c.marker(0x97)
        ns = c.u32()
        if ns > 4:
            raise ValueError('Unsupported native stream count')
        streams = {}
        record_bytes = 0
        for _ in range(ns):
            c.marker(0xa1)
            nv, fmt = c.values('<II')
            # Xbox rigid bump-mapped geometry appends three tangent vectors.
            # This is a real 68-byte format, not an encoded skin-weight count.
            stride = {0: 32, 65536: 68, 14: 40, 15: 48, 16: 52}.get(fmt)
            if stride is None or fmt in streams or not 0 < nv <= 65535:
                raise ValueError('Unsupported native vertex stream')
            at = c.at
            data = c.take(nv * stride)
            for v in range(nv):
                _finite(struct.unpack_from('<8f', data, v * stride))
                if fmt == 65536:
                    _finite(struct.unpack_from('<9f', data, v * stride + 32))
            streams[fmt] = {'count': nv, 'offset': at, 'stride': stride,
                            'sha256': sha(data), 'skin_uv_sha256': sha(b''.join(data[v * stride + 24:(v + 1) * stride] for v in range(nv)))}
            vertices += nv
            record_bytes += nv * stride
        if node_declared is not None and node_declared != record_bytes:
            raise ValueError('Native vertex byte count mismatch')
        vertex_bytes += record_bytes
        palette = []
        if kind == 0x1234569b:
            c.marker(0x7e)
            nb = c.u32()
            if nb > 256:
                raise ValueError('Invalid native bone palette count')
            palette = [c.values('<5I') for _ in range(nb)]
        for fmt, stream in streams.items():
            if fmt in (0, 65536):
                continue
            if kind != 0x1234569b or not palette:
                raise ValueError('Skinned vertex stream lacks native bone palette')
            weights = fmt - 13
            for i in range(stream['count']):
                at = stream['offset'] + i * stream['stride'] + 32
                values = struct.unpack_from('<' + 'f' * weights, raw, at)
                _finite(values)
                if any(v < -0.00001 or v > 1.00001 for v in values) or sum(values) > 1.00001:
                    raise ValueError('Invalid native skin weights')
                indices_used = struct.unpack_from('<' + 'H' * (weights + 1), raw, at + weights * 4)
                if any(index >= len(palette) for index in indices_used):
                    raise ValueError('Native skin palette index outside table')
        c.marker(0x94)
        if c.u32():
            raise ValueError('Unsupported native object ending')
        wi, ops, cursor, fmt = 0, 0, 0, 0
        draw_ranges = []
        while wi < len(program):
            op = program[wi]
            size = {3: 2, 4: 2, 21: 2, 22: 2, 26: 4, 11: 2, 17: 3}.get(op)
            if size is None or wi + size > len(program):
                raise ValueError('Unsupported native draw opcode')
            if op == 3 and program[wi + 1] >= nm:
                raise ValueError('Native draw material outside table')
            if op == 26:
                slots = struct.unpack('<4H', struct.pack('<2I', *program[wi + 2:wi + 4]))
                if any(s != 65535 and s >= nr for s in slots):
                    raise ValueError('Native draw texture outside reference table')
            if op == 11:
                fmt = program[wi + 1]
            if op == 17:
                n = program[wi + 1]
                packed = program[wi + 2]
                first, span = packed & 65535, packed >> 16
                selected = indices[cursor:cursor + n]
                if fmt not in streams or not n or len(selected) != n or not span or min(selected) < first or max(selected) >= first + span or first + span > streams[fmt]['count']:
                    raise ValueError('Native draw index range outside vertex stream')
                draw_ranges.append({'format': fmt, 'indices': list(selected)})
                cursor += n
            wi += size
            ops += 1
        if cursor != ni or ops != nops:
            raise ValueError('Native draw program count mismatch')
        nodes.append({'name': name, 'kind': kind & 255, 'id': '%08x' % object_id,
                      'start': start, 'end': c.at, 'userdata': userdata,
                      'header_sha256': sha(header),
                      'attachment': '%08x' % struct.unpack_from('<I', header)[0],
                      'resources': resources, 'resource_offsets': resource_offsets,
                      'streams': streams, 'palette': [list(p) for p in palette],
                      'bounds': bounds, 'draws': ops, 'indices': ni, 'draw_ranges': draw_ranges})
    c.marker(0x94)
    if c.u32() or c.at != len(raw):
        raise ValueError('Unconsumed native model bytes')
    if declared_bytes is not None and declared_bytes != vertex_bytes:
        raise ValueError('Native root vertex byte count mismatch')
    return {'sha256': sha(raw), 'objects': count, 'vertices': vertices,
            'vertex_bytes': vertex_bytes, 'texture_ids': sorted(refs),
            'global_ids': sorted(global_refs), 'nodes': nodes,
            'bone_ids': sorted({'%08x' % b for n in nodes for p in n['palette'] for b in p[:4]})}


def parse_figure(raw):
    """Xbox FIG: ignored saved pointer, count, 44-byte transforms, name pool."""
    if not 52 <= len(raw) <= MAX_RESOURCE:
        raise ValueError('Invalid native figure size')
    count = struct.unpack_from('<I', raw, 4)[0]
    end = 8 + count * 44
    if not 1 <= count <= 4096 or end >= len(raw):
        raise ValueError('Invalid native figure node table')
    names = raw[end:]
    if not names.endswith(b'\0'):
        raise ValueError('Invalid native figure name pool')
    result = {}
    for i in range(count):
        at = 8 + i * 44
        _finite(struct.unpack_from('<10f', raw, at))
        offset = struct.unpack_from('<I', raw, at + 40)[0]
        name_end = names.find(b'\0', offset)
        if offset >= len(names) or name_end < 0 or name_end == offset or name_end - offset > 256 or (offset and names[offset - 1]):
            raise ValueError('Invalid native figure name offset')
        name = names[offset:name_end]
        key = '%08x' % zlib.crc32(name)
        decoded = name.decode('cp1252')
        if key in result and result[key] != decoded:
            raise ValueError('Native figure name hash collision')
        result[key] = decoded
    return result


def convert_model(raw):
    """Remove only parsed creator runtime userdata; retain all typed payloads."""
    before = parse_model(raw)
    spans = [n['userdata'] for n in before['nodes'] if n['userdata']]
    result, cursor = bytearray(), 0
    for a, b in spans:
        result.extend(raw[cursor:a])
        cursor = b
    result.extend(raw[cursor:])
    result = bytes(result)
    after = parse_model(result)
    if before['objects'] != after['objects'] or before['vertices'] != after['vertices']:
        raise ValueError('Native conversion changed model structure')
    for old, new in zip(before['nodes'], after['nodes']):
        for field in ('name', 'id', 'kind', 'header_sha256', 'resources', 'palette', 'attachment'):
            if old[field] != new[field]:
                raise ValueError('Native conversion changed model binding')
        for fmt, stream in old['streams'].items():
            if stream['sha256'] != new['streams'][fmt]['sha256']:
                raise ValueError('Native conversion changed vertex/skin data')
    return result, {'source_sha256': sha(raw), 'converted_sha256': sha(result),
                    'removed_userdata_records': len(spans), 'removed_bytes': sum(b - a for a, b in spans),
                    'geometry_uv_skinning_materials_indices_preserved': True}


def validate_models(models, texture_pack, *, original_models=None, figure=None):
    textures = parse_texpak(texture_pack)
    ids = {t['id'] for t in textures}
    reports = {name: parse_model(unpack_zlb(raw)) for name, raw in models.items()}
    for name, report in reports.items():
        if not set(report['texture_ids']) <= ids:
            raise ValueError('Native model has unresolved texture dependencies: ' + name)
    allowed_bones = set(parse_figure(figure)) if figure is not None else set()
    if original_models and not allowed_bones:
        for raw in original_models.values():
            original = parse_model(unpack_zlb(raw))
            allowed_bones.update(original['bone_ids'])
            allowed_bones.update(n['attachment'] for n in original['nodes'])
    if allowed_bones:
        allowed_bones.add('00000000')
        for name, report in reports.items():
            required = set(report['bone_ids'])
            required.update(n['attachment'] for n in report['nodes'] if n['kind'] == 0x79)
            if not required <= allowed_bones:
                raise ValueError('Native model has unresolved figure bone bindings: ' + name + ' ' + ','.join(sorted(required - allowed_bones)))
    return {'models': reports, 'textures': textures, 'figure_bones': len(allowed_bones)}


def namespace_textures(models, texture_pack, namespace):
    """Relabel complete pack IDs and exact typed refs; retain all pixel bytes."""
    entries = parse_texpak(texture_pack)
    if not re.fullmatch('[a-z][a-z0-9-]{0,63}', namespace):
        raise ValueError('Invalid character texture namespace')
    mapping = {}
    output = bytearray(texture_pack)
    seen = set()
    for i, texture in enumerate(entries):
        original = texture['id']
        identity = '%d/%d/%d/%d/%s' % (texture['width'], texture['height'], texture['format'], texture['mips'], texture['payload_sha256'])
        low = zlib.crc32(('djultra64/max-spin/character/' + namespace + '/' + original + '/' + identity).encode('ascii'))
        name = original[:8] + '%08x' % low
        if name in seen:
            raise ValueError('Namespaced character texture collision')
        seen.add(name)
        mapping[original] = name
        struct.pack_into('<I', output, 8 + i * 36 + 4, low)
    revised = {}
    for name, raw in models.items():
        report = parse_model(raw)
        model = bytearray(raw)
        for node in report['nodes']:
            for original, at in zip(node['resources'], node['resource_offsets']):
                if original == '00000001fcd68a40':
                    continue
                if original not in mapping:
                    raise ValueError('Cannot namespace unresolved native texture')
                value = mapping[original]
                struct.pack_into('<II', model, at, int(value[8:], 16), int(value[:8], 16))
        revised[name] = bytes(model)
    result = bytes(output)
    validate_models(revised, result)
    return revised, result, mapping


def validate_native_package(manifest, base_files, files):
    """Validate coherent two-outfit Xbox WADs against supplied original resources."""
    target = manifest['target']
    figure = None
    if 'Media/Wads/GamePart.wad' in base_files:
        figure_entries = payloads(base_files['Media/Wads/GamePart.wad'])
        figure = unpack_zlb(figure_entries['media/gamepart/default.fig'])
    reports = {}
    changed_texture_ids = {}
    texture_identities = {}
    for outfit in range(2):
        relative = 'Media/Wads/Players/%s%02d.wad' % (target, outfit)
        descriptor = manifest.get('files', {}).get(relative)
        if not descriptor or relative not in base_files or descriptor['path'] not in files:
            raise ValueError('Native character requires both complete outfit archives')
        original = payloads(base_files[relative])
        candidate = payloads(files[descriptor['path']])
        if original.keys() != candidate.keys():
            raise ValueError('Native character archive member inventory changed')
        prefix = 'media/gamepart/players/%s%02d/' % (target.lower(), outfit)
        allowed = {prefix + name for name in ('playerlod00.ooo', 'playerlod01.ooo',
            'textures/tex.pak', 'player.ini', 'scorename.dds', 'soundscore.pml', 'soundevent.pml')}
        if any(data != candidate[name] for name, data in original.items() if name not in allowed):
            raise ValueError('Native character changed unrelated archive payloads')
        for leaf in ('player.ini', 'scorename.dds', 'soundscore.pml', 'soundevent.pml'):
            key = prefix + leaf
            before, after = unpack_zlb(original[key]), unpack_zlb(candidate[key])
            if before == after:
                continue
            if leaf == 'player.ini':
                def parse_ini(data):
                    ini = configparser.ConfigParser(interpolation=None)
                    ini.optionxform = str
                    try:
                        ini.read_string(data.decode('cp1252'))
                    except (UnicodeError, configparser.Error) as error:
                        raise ValueError('Invalid native character INI') from error
                    return {section: dict(ini[section]) for section in ini.sections()}
                old_ini, new_ini = parse_ini(before), parse_ini(after)
                if old_ini.keys() != new_ini.keys():
                    raise ValueError('Native character INI sections changed')
                for section, properties in old_ini.items():
                    if properties.keys() != new_ini[section].keys():
                        raise ValueError('Native character INI dependency fields changed')
                    for field, value in properties.items():
                        revised = new_ini[section][field]
                        if value == revised:
                            continue
                        if section == 'Accessories' and field == 'Hair' and revised == '':
                            continue
                        if section != 'General' or field not in ('Name', 'ShortName', 'LongName') or not revised.strip() or len(revised) > 128 or any(ord(ch) < 32 for ch in revised):
                            raise ValueError('Native character INI dependency changed')
            elif leaf == 'scorename.dds':
                if len(before) < 128 or before[:4] != b'DDS ' or before[:128] != after[:128] or len(before) != len(after):
                    raise ValueError('Native character score DDS layout changed')
            else:
                # Original audio PML contains resource paths, not just labels.
                # A whole-member replacement is outside this typed package scope.
                raise ValueError('Native character audio dependencies changed')
        models = {name: unpack_zlb(candidate[prefix + name]) for name in ('playerlod00.ooo', 'playerlod01.ooo')}
        originals = {name: unpack_zlb(original[prefix + name]) for name in models}
        from hair_lod_variants import validate_model_variants
        for model in models.values():
            validate_model_variants(unpack_zlb(model))
        reports[relative] = validate_models(models, unpack_zlb(candidate[prefix + 'textures/tex.pak']), original_models=originals, figure=figure)
        for model in reports[relative]['models'].values():
            if any(node['userdata'] for node in model['nodes']):
                raise ValueError('Native character retains captured runtime userdata')
        original_textures = {t['id']: t for t in parse_texpak(unpack_zlb(original[prefix + 'textures/tex.pak']), original_source=True)}
        for texture in reports[relative]['textures']:
            identity = sha(struct.pack('<4I', texture['width'], texture['height'], texture['format'], texture['mips']) + bytes.fromhex(texture['payload_sha256']))
            previous_identity = texture_identities.get(texture['id'])
            if previous_identity is not None and previous_identity != identity:
                raise ValueError('Native character texture ID conflicts between outfits')
            texture_identities[texture['id']] = identity
            if texture['id'] not in original_textures or texture['payload_sha256'] != original_textures[texture['id']]['payload_sha256']:
                previous = changed_texture_ids.get(texture['id'])
                if previous is not None and previous != texture['payload_sha256']:
                    raise ValueError('Native character texture ID conflicts between outfits')
                changed_texture_ids[texture['id']] = texture['payload_sha256']
    return {'archives': reports, 'texture_ids': changed_texture_ids,
            'texture_identities': texture_identities, 'figure_verified': figure is not None}
