"""Approved native character assembly contracts. Own work: djultra64."""
from pathlib import Path
import importlib.util
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))
import character_formats as formats


def bump_model():
    spec = importlib.util.spec_from_file_location('approved_format_fixtures', ROOT / 'native-linux/tests/test_character_formats.py')
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    raw = fixture.fixture_model()
    at = raw.index(struct.pack('<I', 0x123456a1))
    changed = bytearray(raw[:at + 12])
    struct.pack_into('<I', changed, at + 8, 65536)
    for row in range(3):
        changed.extend(raw[at + 12 + row * 32:at + 12 + (row + 1) * 32])
        changed.extend(struct.pack('<9f', *range(9)))
    changed.extend(raw[at + 12 + 3 * 32:])
    # Select the tangent stream in the independently built draw program.
    draw = changed.index(struct.pack('<II', 11, 0))
    struct.pack_into('<I', changed, draw + 4, 65536)
    return bytes(changed)


class OriginalXboxBumpStreams(unittest.TestCase):
    def test_rigid_65536_is_68_byte_geometry_not_skin_weights(self):
        raw = bump_model()
        parsed = formats.parse_model(raw)
        stream = parsed['nodes'][0]['streams'][65536]
        self.assertEqual((stream['count'], stream['stride']), (3, 68))
        self.assertEqual(parsed['vertex_bytes'], 204)
        converted, proof = formats.convert_model(raw)
        self.assertEqual(converted, raw)
        self.assertTrue(proof['geometry_uv_skinning_materials_indices_preserved'])

    def test_bump_tangents_must_be_finite_and_records_complete(self):
        raw = bump_model()
        start = raw.index(struct.pack('<I', 0x123456a1)) + 12
        bad = bytearray(raw)
        struct.pack_into('<f', bad, start + 64, float('nan'))
        with self.assertRaisesRegex(ValueError, 'Nonfinite'):
            formats.parse_model(bytes(bad))
        with self.assertRaises(ValueError):
            formats.parse_model(raw[:start + 67] + raw[start + 68:])


@unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Save-Snapshot').is_dir()
                     and (ROOT / 'assets/Media/Wads/GamePart.wad').is_file(), 'private frozen native evidence unavailable')
class AcceptedNativeAssembly(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import approved_character_models
        import json
        import numpy as np
        cls.a = approved_character_models
        cls.np = np
        cls.legacy = ROOT.parent / 'character-research/legacy'
        cls.snapshot = cls.legacy / 'build/Federer-Creator-Save-Snapshot'
        cls.sources = {(o, l): formats.unpack_zlb((cls.snapshot / ('Player%02dLod%02d.ooo' % (o, l))).read_bytes())
                       for o in range(2) for l in range(2)}
        cls.figure = formats.unpack_zlb(formats.payloads((ROOT / 'assets/Media/Wads/GamePart.wad').read_bytes())['media/gamepart/default.fig'])
        cls.hair = formats.payloads((cls.legacy / 'build/Federer-Creator-Test/Media/Wads/PlayerCreatorMaleHairCut.wad').read_bytes())
        cls.bindings = json.loads((ROOT / 'face-builder/build/nadal/sources/native-bindings.json').read_text())['vertices']
        cls.native_source = (ROOT / 'face-builder/build/nadal/sources/native-source.ooo').read_bytes()
        cls.source_nodes = formats.parse_model(cls.native_source)['nodes']
        cls.assembled = {name: cls.a.assemble_models(name, ROOT / 'assets', cls.legacy, ROOT)
                         for name in ('nadal', 'djokovic', 'agassi', 'becker', 'mcenroe', 'pioline', 'haas')}

    def test_remaining_four_assemble_frozen_faces_and_selected_native_hair(self):
        expected = {
            'becker': (12, '0000', 'd3774872b6214ea12bccf51d5f308bd691acf01307b44573ceeb859a7fcc13d7',
                       '8bc900c62513d2282890ce638bbcf9857380fa7fc7e09db3937c4f27d8fc84f3'),
            'mcenroe': (4, '0010', '47403386e27783c5d9057d099cdc4d92831b1ae9bd28c59d3b52be105b65ab68',
                        '7570fc411afd27c06d90a40eb68591a00fe8b4c8487562549e820c35f8f40144'),
            'pioline': (9, '0010', '809b6f2d6013f58dbc2d1ec05b3d04247a25b88c6ed3fe92e7e2583c459b4fb7',
                        'd269fab427693193ca54ef53d118fcaee22fd02726571c38d6795f4c24b14535'),
            'haas': (14, '0010', '0655060ee8d55b96313d7a09554bbedfabcfa00b5eb82d6a313953eecc5a4fb8',
                     '64b6702f4648173befadd0f7da1531b298ac42c19d378bf4a3b9a51f29d0a3d1'),
        }
        for character, (hair, variant, obj_hash, albedo_hash) in expected.items():
            with self.subTest(character=character):
                try:
                    models, packs, proof = self.assembled.get(character) or self.a.assemble_models(
                        character, ROOT / 'assets', self.legacy, ROOT)
                except ValueError as error:
                    self.fail(f'{character} must assemble its frozen native model: {error}')
                self.assertEqual(set(models), {(0, 0), (0, 1), (1, 0), (1, 1)})
                self.assertEqual(proof['accepted']['obj_sha256'], obj_hash)
                self.assertEqual(proof['accepted']['albedo_sha256'], albedo_hash)
                self.assertEqual(proof['accepted']['high_native_rows'], 4459)
                for model in proof['models']:
                    self.assertEqual(model['hair']['display_number'], hair)
                    self.assertEqual(model['hair']['color_variant'], variant)
                for outfit in (0, 1):
                    formats.validate_models({str(lod): models[outfit, lod] for lod in (0, 1)},
                                            packs[outfit], figure=self.figure)

    def test_accepted_high_positions_normals_and_uv_match_every_bound_native_row(self):
        # Read the accepted OBJ independently; do not use the assembler's mapping.
        for character, (models, packs, proof) in self.assembled.items():
            path = ROOT / 'face-builder/build' / self.a.APPROVED[character]['obj']
            arrays = {tag: [] for tag in ('v', 'vn', 'vt')}
            for line in path.read_text().splitlines():
                parts = line.split()
                if parts and parts[0] in arrays:
                    arrays[parts[0]].append(list(map(float, parts[1:])))
            for outfit in (0, 1):
                model = models[outfit, 0]
                nodes = {n['id']: n for n in formats.parse_model(model)['nodes']}
                matched = 0
                for row, binding in enumerate(self.bindings):
                    source = next(n for n in self.source_nodes if n['start'] <= binding['position_offset'] < n['end'])
                    target = nodes[source['id']]
                    # Source last node is obsolete hair; only the appended actual
                    # selected hairstyle is retained by the native assembler.
                    if source is self.source_nodes[-1]:
                        continue
                    stream = target['streams'][binding['stream_format']]
                    at = stream['offset'] + binding['stream_vertex'] * stream['stride']
                    expected = arrays['v'][row] + arrays['vn'][row] + [arrays['vt'][row][0], 1 - arrays['vt'][row][1]]
                    self.assertEqual(model[at:at + 32], struct.pack('<8f', *expected), (character, outfit, row))
                    matched += 1
                self.assertEqual(matched, 4459)

    @staticmethod
    def _scrub(node, raw):
        data = bytearray(raw[node['start']:node['end']])
        for at in node['resource_offsets']:
            data[at - node['start']:at - node['start'] + 8] = bytes(8)
        if node['userdata']:
            a, b = node['userdata']
            del data[a - node['start']:b - node['start']]
        return bytes(data)

    def test_native_body_materials_indices_palettes_and_skinning_survive_all_outfits(self):
        for character, (models, packs, proof) in self.assembled.items():
            for key, result in models.items():
                original = self.sources[key]
                before, after = formats.parse_model(original), formats.parse_model(result)
                nodes = {n['id']: n for n in after['nodes']}
                changed = set(next(p for p in proof['models'] if (p['outfit'], p['lod']) == key)['head']['changed_head_nodes'])
                for node in before['nodes']:
                    if '1000000120213d0a' in node['resources']:
                        continue
                    new = nodes[node['id']]
                    self.assertIsNone(new['userdata'])
                    self.assertEqual(node['palette'], new['palette'])
                    self.assertEqual(node['draw_ranges'], new['draw_ranges'])
                    if node['id'] not in changed:
                        self.assertEqual(self._scrub(node, original), self._scrub(new, result), (character, key, node['id']))
                    for fmt, stream in node['streams'].items():
                        dest = new['streams'][fmt]
                        self.assertEqual((stream['count'], stream['stride']), (dest['count'], dest['stride']))
                        for i in range(stream['count']):
                            a = stream['offset'] + i * stream['stride']
                            b = dest['offset'] + i * dest['stride']
                            self.assertEqual(original[a + 32:a + stream['stride']], result[b + 32:b + dest['stride']])

    def test_correct_native_hair_records_preserved_including_nadal_bump_tangents(self):
        for character, (models, packs, proof) in self.assembled.items():
            hair_id = self.a.APPROVED[character]['hair']
            for key, result in models.items():
                raw = formats.unpack_zlb(self.hair['media/playercreatorpart/male/haircut/haircut_%02d/haircut_lod%02d.ooo' % (hair_id, key[1] + 1)])
                # Give the standalone original typed placeholder references only
                # so the independent strict parser can expose original streams.
                typed = bytearray(raw)
                at = raw.index(struct.pack('<I', 0x1234568b))
                count = struct.unpack_from('<I', raw, at + 4)[0]
                for row in range(count):
                    offset = at + 12 + row * 8
                    high = struct.unpack_from('<I', raw, offset)[0]
                    struct.pack_into('<I', typed, offset, 0x10000000 + (high if high in (1, 3) else 6))
                native = formats.parse_model(bytes(typed))['nodes'][0]
                parsed = formats.parse_model(result)
                hairs = [n for n in parsed['nodes'] if n['id'] == ('13d59455' if key[1] == 0 else '75ed28ac')]
                self.assertEqual(len(hairs), 1)
                far = [n for n in parsed['nodes'] if n['id'] == '02ea183a']
                self.assertEqual(len(far), 1 if key[1] == 1 else 0)
                if far:
                    from hair_lod_variants import same_selected_leaf
                    self.assertTrue(same_selected_leaf(result, hairs[0], far[0]))
                self.assertEqual(native['streams'].keys(), hairs[0]['streams'].keys())
                for fmt, stream in native['streams'].items():
                    self.assertEqual(stream['sha256'], hairs[0]['streams'][fmt]['sha256'], (character, key, fmt))
                if character == 'nadal' and key[1] == 0:
                    self.assertEqual(hairs[0]['streams'][65536]['count'], 390)

    def test_every_lod_has_meaningful_identity_and_closed_native_dependencies(self):
        for character, (models, packs, proof) in self.assembled.items():
            self.assertEqual(set(models), {(0, 0), (0, 1), (1, 0), (1, 1)})
            for outfit in (0, 1):
                report = formats.validate_models({str(l): models[outfit, l] for l in (0, 1)}, packs[outfit], figure=self.figure)
                self.assertTrue(report['figure_bones'])
                source = self.sources[outfit, 1]
                face = next(n for n in formats.parse_model(models[outfit, 1])['nodes'] if n['name'].startswith('M_MRF_VSG'))
                original = next(n for n in formats.parse_model(source)['nodes'] if n['id'] == face['id'])
                original_at = original['streams'][14]['offset']
                result_at = face['streams'][14]['offset']
                self.assertNotEqual(source[original_at:original_at + 32], models[outfit, 1][result_at:result_at + 32])
            self.assertTrue(proof['texture_dependencies_closed'])
            self.assertFalse(proof['source_files_modified'])

    def test_federer_cannot_be_accidentally_reassembled(self):
        with self.assertRaisesRegex(ValueError, 'Only approved'):
            self.a.assemble_models('federer', ROOT / 'assets', self.legacy, ROOT)

    def test_existing_native_wardrobe_is_applied_to_each_raw_outfit_before_namespace(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        calls = []
        def apply(models, pack, assets, character, outfit):
            self.assertEqual(character, 'nadal')
            self.assertEqual(assets, ROOT / 'assets')
            self.assertEqual(set(models), {'playerlod00.ooo', 'playerlod01.ooo'})
            for model in models.values():
                self.assertIn(self.a.FACE, formats.parse_model(model)['texture_ids'])
            self.assertIn(self.a.FACE, {row['id'] for row in formats.parse_texpak(pack)})
            calls.append(outfit)
            return models, pack, {'test_existing_native_wardrobe': True}
        with patch.dict(sys.modules, {'approved_roster_wardrobe': SimpleNamespace(apply=apply)}):
            models, packs, proof = self.a.assemble_models('nadal', ROOT / 'assets', self.legacy, ROOT)
        self.assertEqual(calls, [0, 1])
        self.assertEqual([row['outfit'] for row in proof['wardrobe']], [0, 1])
        self.assertTrue(all(row['test_existing_native_wardrobe'] for row in proof['wardrobe']))

    def test_native_wardrobe_changes_only_existing_cloth_pixels_and_keeps_head_exact(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        def preserve(models, pack, assets, character, outfit):
            return models, pack, {'changes': []}
        for character, (models, packs, proof) in self.assembled.items():
            with patch.dict(sys.modules, {'approved_roster_wardrobe': SimpleNamespace(apply=preserve)}):
                baseline_models, baseline_packs, _ = self.a.assemble_models(character, ROOT / 'assets', self.legacy, ROOT)
            for key, model in models.items():
                baseline = baseline_models[key]
                before, after = bytearray(baseline), bytearray(model)
                self.assertEqual(len(before), len(after))
                for raw, scrubbed in ((baseline, before), (model, after)):
                    for node in formats.parse_model(raw)['nodes']:
                        for at in node['resource_offsets']:
                            scrubbed[at:at + 8] = bytes(8)
                self.assertEqual(before, after, (character, key, 'geometry/material program changed'))
            for outfit in (0, 1):
                baseline, result = baseline_packs[outfit], packs[outfit]
                rows_before, rows_after = formats.parse_texpak(baseline), formats.parse_texpak(result)
                self.assertEqual(len(rows_before), len(rows_after))
                cloth_refs = {identity for node in formats.parse_model(baseline_models[outfit, 0])['nodes']
                    if node['name'].startswith(('M_MRF_VET_', 'M_VET_CHS_', 'M_VET_CHT_'))
                    for identity in node['resources'] if identity.startswith('10000001')}
                changes = []
                for before, after in zip(rows_before, rows_after):
                    if before['payload_sha256'] != after['payload_sha256']:
                        self.assertIn(before['id'], cloth_refs, (character, outfit, before['id']))
                        changes.append(before['id'])
                    else:
                        self.assertEqual(baseline[before['offset']:before['offset'] + before['bytes']],
                                         result[after['offset']:after['offset'] + after['bytes']])
                self.assertGreaterEqual(len(changes), 2, (character, outfit))
                wardrobe = next(row for row in proof['wardrobe'] if row['outfit'] == outfit)
                self.assertTrue(wardrobe['changes'])

    def test_wardrobe_cannot_modify_mesh_or_face_pixels(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        for mode, expected in (('mesh', 'native model bytes'), ('face', 'non-cloth texture')):
            def tamper(models, pack, assets, character, outfit):
                if mode == 'mesh':
                    changed = dict(models)
                    changed['playerlod00.ooo'] += b'changed'
                    return changed, pack, {}
                row = next(row for row in formats.parse_texpak(pack) if row['id'] == self.a.FACE)
                changed = bytearray(pack)
                changed[row['offset']] ^= 1
                return models, bytes(changed), {}
            with self.subTest(mode=mode), patch.dict(sys.modules, {'approved_roster_wardrobe': SimpleNamespace(apply=tamper)}):
                with self.assertRaisesRegex(ValueError, expected):
                    self.a.assemble_models('nadal', ROOT / 'assets', self.legacy, ROOT)

    def test_nadal_cap_is_green_in_both_material_slots_without_repainting_hair(self):
        import io
        from PIL import Image
        models, packs, proof = self.assembled['nadal']
        self.assertIn('nadal-selected-hair15-green-cap', proof['accepted']['obj'])
        for model in proof['models']:
            textures = {entry['slot']: entry for entry in model['hair']['textures']}
            crown, visor = textures['Diff_01'], textures['Diff_02']
            self.assertEqual(crown['source'], visor['source'])
            self.assertEqual(crown['sha256'], visor['sha256'])
            self.assertIn('green-cap', crown['source'])
            self.assertEqual(textures['Diff_00']['sha256'], '54e5982dacd7840727a786408609b1d3464d614b9dd515b7c3252323e26aacd3')
            pixels = self.np.asarray(Image.open(io.BytesIO(Path(crown['source']).read_bytes())).convert('RGB'), dtype=float)
            green = (pixels[:, :, 1] > pixels[:, :, 0] * 1.5) & (pixels[:, :, 1] > pixels[:, :, 2] * 1.3)
            self.assertGreater(float(green.mean()), .99)
            raw = Path(crown['source']).read_bytes()
            self.assertEqual(raw[84:88], b'DXT5')
            payload_hash = formats.sha(raw[128:])
            # Both exact native typed cap references share the encoded pixels,
            # even though their texture identities remain distinct slot IDs.
            packed = formats.parse_texpak(packs[model['outfit']])
            self.assertEqual(sum(entry['payload_sha256'] == payload_hash for entry in packed), 2)

    def test_actual_indexed_face_uv_and_low_geometry_triangles_keep_orientation(self):
        def uv_area(points):
            a, b, c = points
            d, e = b - a, c - a
            return d[0] * e[1] - d[1] * e[0]
        for character, (models, packs, proof) in self.assembled.items():
            for key, model in models.items():
                original = self.sources[key]
                old_nodes = {n['id']: n for n in formats.parse_model(original)['nodes']}
                for node in formats.parse_model(model)['nodes']:
                    if not node['name'].startswith('M_MRF_VSG'):
                        continue
                    old = old_nodes[node['id']]
                    for draw in node['draw_ranges']:
                        stream, base = node['streams'][draw['format']], old['streams'][draw['format']]
                        positions = self.np.array([struct.unpack_from('<3f', model, stream['offset'] + i * stream['stride']) for i in range(stream['count'])])
                        original_positions = self.np.array([struct.unpack_from('<3f', original, base['offset'] + i * base['stride']) for i in range(base['count'])])
                        uv = self.np.array([struct.unpack_from('<2f', model, stream['offset'] + i * stream['stride'] + 24) for i in range(stream['count'])])
                        original_uv = self.np.array([struct.unpack_from('<2f', original, base['offset'] + i * base['stride'] + 24) for i in range(base['count'])])
                        for index in range(len(draw['indices']) - 2):
                            ids = draw['indices'][index:index + 3]
                            before = uv_area(original_uv[ids])
                            if abs(before) > 1e-12:
                                self.assertGreater(uv_area(uv[ids]) / before, .01, (character, key, ids))
                            if key[1] == 1:
                                a, b, c = original_positions[ids]
                                d, e, f = positions[ids]
                                source_normal = self.np.cross(b - a, c - a)
                                if self.np.linalg.norm(source_normal) > 1e-6:
                                    self.assertGreater(self.np.dot(source_normal, self.np.cross(e - d, f - d)), 0, (character, key, ids))


if __name__ == '__main__':
    unittest.main()
