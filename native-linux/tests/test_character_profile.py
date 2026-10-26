"""Native profile and portrait preparation contracts. Own work: djultra64."""
import importlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))


class CharacterProfileTests(unittest.TestCase):
    def module(self):
        self.assertTrue((TOOLS / 'character_profile.py').is_file(), 'native profile validation is missing')
        return importlib.import_module('character_profile')

    def catalog(self):
        return {'version': 1, 'countries': [{'id': 196, 'name': 'Switzerland'}],
                'skills': [{'key': 'Forehand'}, {'key': 'AccelSpeed'}]}

    def test_only_native_known_finite_values_are_accepted(self):
        profile = {'displayName': 'R. Federer', 'firstName': 'Roger', 'lastName': 'Federer',
                   'shortName': 'Fed', 'countryId': 196, 'age': 25, 'heightCm': 186,
                   'weightKg': 85, 'leftHanded': False, 'backhandStyle': 0,
                   'playStyle': 2, 'skills': {'Forehand': 9, 'AccelSpeed': 7}}
        self.assertEqual(self.module().validate_profile(profile, self.catalog()), profile)
        for change in ({'age': float('nan')}, {'age': True}, {'age': 14},
                       {'countryId': 999}, {'leftHanded': 1}, {'backhandStyle': 108},
                       {'playStyle': 3}, {'NaturalTalent': 2}, {'displayName': 'bad\nname'},
                       {'displayName': 'unsupported\u2603'}, {'shortName': 'way too long' * 2},
                       {'skills': {'Forehand': 11}}, {'skills': {'Endurance': 3}},
                       {'skills': {'Forehand': float('nan')}}, {'skills': {'ServeMastery': 200}}):
            with self.subTest(change=change):
                with self.assertRaises(ValueError):
                    self.module().validate_profile(change, self.catalog())

    def test_empty_profile_is_an_optional_noop(self):
        self.assertEqual(self.module().validate_profile({}, self.catalog()), {})

    def test_actual_catalog_uses_xbox_country_and_skill_schema_and_preserved_defaults(self):
        module = self.module()
        if not (module.ROOT / 'assets/Media/Wads/Db.wad').is_file():
            self.skipTest('private original assets are not installed')
        catalog = module.profile_catalog()
        self.assertEqual(catalog['defaults']['federer']['shortName'], 'Fed')
        self.assertFalse(catalog['defaults']['federer']['leftHanded'])
        self.assertTrue(catalog['defaults']['nadal']['leftHanded'])
        self.assertIn({'id': 196, 'name': 'Switzerland', 'countryToken': 'PAM_518079633', 'zoneId': 1}, catalog['countries'])
        self.assertIn('ForehandPower', {row['key'] for row in catalog['skills']})
        self.assertNotIn('Endurance', {row['key'] for row in catalog['skills']})
        self.assertNotIn('ServeMastery', {row['key'] for row in catalog['skills']})
        for profile in catalog['defaults'].values():
            module.validate_profile(profile, catalog)

    def test_profile_composes_consistent_native_rows_and_keeps_other_player_untouched(self):
        import prepare_characters as prep
        from character_formats import Pml, payloads, unpack_zlb
        module = self.module()
        if not (module.ROOT / 'assets/Media/Wads/Db.wad').is_file():
            self.skipTest('private original assets are not installed')
        original = (module.ROOT / 'assets/Media/Wads/Db.wad').read_bytes()
        source = (prep.DEFAULT_RESEARCH / 'build/Federer-Creator-Test/Media/Wads/Db.wad').read_bytes()
        catalog = module.profile_catalog(original_db=original, source_db=source)
        override = {'displayName': 'T. Tester', 'firstName': 'Test', 'lastName': 'Tester',
                    'shortName': 'Tst', 'age': 30, 'countryId': 36, 'leftHanded': True,
                    'backhandStyle': 1, 'playStyle': 2, 'skills': {'Forehand': 3, 'AccelSpeed': 10}}
        rows = prep._database_updates(original, source, prep.CHARACTERS['federer'], profile=override, catalog=catalog)
        trees = {}
        original_members = payloads(original)
        for row in rows:
            self.assertNotIn('Id=16', row['node'])
            if row['member'] not in trees:
                trees[row['member']] = Pml(unpack_zlb(original_members[row['member']]))
            tree = trees[row['member']]
            tree.set(row['node'], row['property'], row['value'])
        for member in ('media/db/players(m).pml', 'media/db/profils(m).pml'):
            tree = trees[member]
            for prop, value in [('Name', 'Test Tester'), ('Age', '30'), ('BirthplaceID', '36'),
                                ('LeftHanded', '1'), ('Country', 'PAM_1297479313'), ('ZoneID', '2')]:
                self.assertEqual(tree.get('TRPlayer/Id=15', prop), value)
            self.assertEqual(tree.get('TRPlayer/Id=15/Name=Style', 'Value'), '1')
            self.assertEqual(tree.get('TRPlayer/Id=15/Name=PlayStyle', 'Value'), '2')
            self.assertEqual(tree.get('TRPlayer/Id=15/Name=Forehand', 'Value'), '3')
            self.assertEqual(tree.get('TRPlayer/Id=15/Name=AccelSpeed', 'Value'), '10')
        self.assertEqual(trees['media/db/profils(m).pml'].get('TPlayers/Id=15', 'Age'), '30')
        for outfit in (0, 1):
            path = 'TPLayerInfo/DirName=TukurXX%02d' % outfit
            self.assertEqual(trees['media/db/playerlist.pml'].get(path, 'Name'), 'T. Tester')
            self.assertEqual(trees['media/db/playerlist.pml'].get(path, 'RightHanded'), '0')
        self.assertEqual(prep._database_updates(original, source, prep.CHARACTERS['federer']),
                         prep._database_updates(original, source, prep.CHARACTERS['federer'], profile={}))

    def test_portrait_png_encodes_both_native_sizes_with_transparency_and_exact_headers(self):
        import prepare_characters as prep
        from PIL import Image, ImageDraw
        from character_formats import unpack_zlb
        image = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
        ImageDraw.Draw(image).rectangle((128, 128, 383, 383), fill=(255, 0, 0, 255))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'portrait.png'
            image.save(path)
            loaded, proof = prep.read_portrait(path)
            self.assertEqual(proof['dimensions'], [512, 512])
            for size in (256, 64):
                stream = io.BytesIO()
                Image.new('RGBA', (size, size)).save(stream, format='DDS', pixel_format='DXT5')
                original = stream.getvalue()
                converted = unpack_zlb(prep.portrait_dds(original, loaded))
                self.assertEqual(converted[:128], original[:128])
                self.assertEqual(len(converted), len(original))
                decoded = Image.open(io.BytesIO(converted)).convert('RGBA')
                self.assertEqual(decoded.getpixel((0, 0))[3], 0)
                self.assertGreater(decoded.getpixel((size // 2, size // 2))[3], 240)

    def test_portrait_rejects_wrong_format_oversized_or_empty_image(self):
        import prepare_characters as prep
        from PIL import Image
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            jpeg = root / 'portrait.jpg'
            Image.new('RGB', (512, 512)).save(jpeg)
            empty = root / 'empty.png'
            Image.new('RGBA', (512, 512)).save(empty)
            large = root / 'large.png'
            Image.new('1', (2049, 2049), 1).save(large)
            for path in (jpeg, empty, large):
                with self.subTest(path=path):
                    with self.assertRaises(ValueError):
                        prep.read_portrait(path)

    def test_preparer_wardrobe_hook_preserves_models_and_changes_only_selected_native_diffuse(self):
        import prepare_characters as prep
        import character_wardrobe
        from character_formats import payloads, unpack_zlb, convert_model, parse_texpak
        if not (prep.ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file():
            self.skipTest('private original Xbox wardrobe assets are not installed')
        source = payloads((prep.DEFAULT_RESEARCH / 'build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').read_bytes())
        prefix = 'media/gamepart/players/tukurxx00/'
        models = {name: convert_model(unpack_zlb(source[prefix + name]))[0] for name in ('playerlod00.ooo', 'playerlod01.ooo')}
        pack = unpack_zlb(source[prefix + 'textures/tex.pak'])
        catalog = character_wardrobe.build_catalog(prep.ROOT / 'assets', include_previews=False)
        choice = next(c['id'] for row in catalog['categories'] if row['id'] == 'shirt' for c in row['choices'] if c['id'] != 'preserved' and c['enabled'])
        revised, changed, proof = prep.apply_character_wardrobe(models, pack, prep.ROOT / 'assets', {'shirt': choice})
        self.assertEqual(models, revised)
        before = {t['id']: t['payload_sha256'] for t in parse_texpak(pack)}
        after = {t['id']: t['payload_sha256'] for t in parse_texpak(changed)}
        self.assertEqual({key for key in before if before[key] != after[key]}, {'10000001c282a38d'})
        self.assertIn('Media/Wads/PlayerCreatorMaleCloth.wad', proof['source_files'])

    def test_preparation_applies_explicit_uv_alignment_to_every_outfit_lod(self):
        import prepare_characters as prep
        import character_uv
        if not (prep.ROOT / 'assets/Media/Wads/GamePart.wad').is_file():
            self.skipTest('private native sources are not installed')
        specification = importlib.util.spec_from_file_location('profile_test_face_compiler', prep.ROOT / 'face-builder/compile.py')
        compiler = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(compiler)
        source = compiler.trusted_source('federer')
        triangles = character_uv.trusted_face('federer')
        index = next(i for i, triangle in enumerate(triangles) if .49 < triangle[0]['uv'][0] < .51 and .49 < triangle[0]['uv'][1] < .55)
        uv = triangles[index][0]['uv']
        recipe = {'schema': 'djultra64-face-builder', 'version': 1, 'character': 'federer', 'units': 'centimeters',
            'sourceMeshSha256': source['meshSha256'], 'sourceTextureSha256': source['textureSha256'],
            'landmarks': {}, 'values': {}, 'uvAlignment': {'nose': {'triangle': index,
                'weights': [1, 0, 0], 'targetUV': [uv[0] + .002, uv[1] + .002]}}}
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'candidate'
            prep.prepare(prep.ROOT / 'assets', prep.DEFAULT_RESEARCH, output, characters=('federer',), recipes={'federer': recipe})
            proof = json.loads((output / '9/compatibility-report.json').read_text())
            for outfit in proof['outfits']:
                self.assertEqual(len(outfit['uv_alignment']), 2)
                for report in outfit['uv_alignment'].values():
                    self.assertTrue(report['applied'])
                    self.assertGreater(report['changed_bytes'], 0)
                    self.assertTrue(report['geometry_normals_skinning_materials_indices_preserved'])

    def test_wardrobe_changes_primary_outfit_and_preserves_second_outfit_pixels(self):
        import prepare_characters as prep
        import character_wardrobe
        from character_formats import payloads, unpack_zlb, parse_texpak
        if not (prep.ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file():
            self.skipTest('private original Xbox wardrobe assets are not installed')
        specification = importlib.util.spec_from_file_location('wardrobe_test_face_compiler', prep.ROOT / 'face-builder/compile.py')
        compiler = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(compiler)
        source = compiler.trusted_source('federer')
        catalog = character_wardrobe.build_catalog(prep.ROOT / 'assets', include_previews=False)
        shirt = next(c['id'] for row in catalog['categories'] if row['id'] == 'shirt' for c in row['choices'] if c['id'] != 'preserved' and c['enabled'])
        recipe = {'schema': 'djultra64-face-builder', 'version': 1, 'character': 'federer', 'units': 'centimeters',
            'sourceMeshSha256': source['meshSha256'], 'sourceTextureSha256': source['textureSha256'],
            'landmarks': {}, 'values': {}, 'wardrobe': {'shirt': shirt}}
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'candidate'
            baseline = Path(folder) / 'same-face-and-hair'
            baseline_recipe = dict(recipe)
            baseline_recipe.pop('wardrobe')
            # Compare the same complete face/hair preparation with and without
            # the shirt choice; raw research textures predate those stages.
            prep.prepare(prep.ROOT / 'assets', prep.DEFAULT_RESEARCH, baseline,
                         characters=('federer',), recipes={'federer': baseline_recipe})
            prep.prepare(prep.ROOT / 'assets', prep.DEFAULT_RESEARCH, output, characters=('federer',), recipes={'federer': recipe})
            proof = json.loads((output / '9/compatibility-report.json').read_text())
            self.assertEqual(proof['outfits'][0]['wardrobe']['scope'], 'primary_outfit_only')
            self.assertEqual(len(proof['outfits'][0]['wardrobe']['changes']), 1)
            self.assertEqual(proof['outfits'][1]['wardrobe']['scope'], 'second_outfit_preserved')
            for outfit in (0, 1):
                archive = 'TukurXX%02d.wad' % outfit
                prefix = 'media/gamepart/players/tukurxx%02d/' % outfit
                original = payloads((baseline / '9/native' / archive).read_bytes())
                candidate = payloads((output / '9/native' / archive).read_bytes())
                before = [t['payload_sha256'] for t in parse_texpak(unpack_zlb(original[prefix + 'textures/tex.pak']))]
                after = [t['payload_sha256'] for t in parse_texpak(unpack_zlb(candidate[prefix + 'textures/tex.pak']))]
                self.assertEqual(sum(a != b for a, b in zip(before, after)), 1 if outfit == 0 else 0)


if __name__ == '__main__':
    unittest.main()
