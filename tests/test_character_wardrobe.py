"""Native wardrobe uses existing Xbox textures without editing geometry. djultra64."""
import importlib.util
import base64
from io import BytesIO
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))
from character_formats import payloads, parse_texpak, unpack_zlb


class NativeWardrobeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'native-linux/tools/character_wardrobe.py'
        cls.module = None
        if path.exists():
            spec = importlib.util.spec_from_file_location('character_wardrobe', path)
            cls.module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.module)
        cls.assets = ROOT / 'assets'
        cls.catalogs = {}

    def wardrobe(self):
        self.assertIsNotNone(self.module, 'native wardrobe module is missing')
        return self.module

    def preview_catalog(self, character='federer'):
        if character not in self.catalogs:
            self.catalogs[character] = self.wardrobe().build_catalog(self.assets,include_previews=True,character=character)
        return self.catalogs[character]

    def test_empty_selection_retains_exact_resources_without_needing_creator_assets(self):
        module = self.wardrobe()
        models = {'lod00':b'opaque immutable mesh'}
        same, texture, proof = module.apply_wardrobe(models, b'opaque texture', Path('/missing-assets'), {})
        self.assertEqual(same, models)
        self.assertEqual(texture, b'opaque texture')
        self.assertEqual(proof['changes'], [])

    @unittest.skipUnless((ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file(), 'private original Xbox assets unavailable')
    def test_catalog_links_genuine_male_ids_to_exact_native_geometry_family_and_thumbnails(self):
        catalog = self.preview_catalog()
        categories = {category['id']:category for category in catalog['categories']}
        for category, identity in [('shirt','creator:38'),('shorts','creator:104'),('shoes','creator:53'),('socks','creator:64')]:
            choice = next(choice for choice in categories[category]['choices'] if choice['id'] == identity)
            self.assertTrue(choice['enabled'])
            self.assertTrue(choice['preview'].startswith('data:image/png;base64,'))
            self.assertFalse(choice['label'].startswith('F_'))
        self.assertNotIn('creator:776', {c['id'] for c in categories['shirt']['choices']})
        caps = [choice for choice in categories['cap']['choices'] if choice['id'] != 'preserved']
        self.assertIn('creator:294', {c['id'] for c in caps})
        self.assertTrue(all(not c['enabled'] and c['reason'] for c in caps))

    @unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').is_file(), 'preserved source fixture unavailable')
    def test_shirt_and_shorts_previews_render_textured_native_silhouettes_from_front_and_back(self):
        from PIL import Image
        catalog = self.preview_catalog()
        categories = {category['id']:category for category in catalog['categories']}
        for category, identity in [('shirt','creator:38'),('shorts','creator:104')]:
            choice = next(choice for choice in categories[category]['choices'] if choice['id'] == identity)
            self.assertTrue('previews' in choice, 'native garment views must replace the tiny UV atlas')
            self.assertEqual(choice['preview_kind'], 'textured-native-garment')
            self.assertEqual(set(choice['previews']), {'front','back'})
            views = []
            for view in ('front','back'):
                data = base64.b64decode(choice['previews'][view].split(',',1)[1])
                with Image.open(BytesIO(data)) as image:
                    self.assertGreaterEqual(image.width, 384)
                    self.assertGreaterEqual(image.height, 384)
                    alpha = image.convert('RGBA').getchannel('A')
                    histogram = alpha.histogram()
                    visible = sum(histogram[1:])
                    self.assertGreater(visible, image.width*image.height*.15)
                    self.assertLess(visible, image.width*image.height*.85)
                    self.assertGreater(len(image.convert('RGB').getcolors(image.width*image.height) or []), 20)
                    views.append(data)
            self.assertNotEqual(views[0],views[1])
            self.assertEqual(choice['preview'], choice['previews']['front'])
            self.assertIn('model_sha256', choice['preview_source'])

    @unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').is_file(), 'preserved source fixture unavailable')
    def test_front_view_shows_existing_polo_buttons_and_back_does_not(self):
        import numpy as np
        from PIL import Image
        module = self.wardrobe()
        source = ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad'
        model = unpack_zlb(payloads(source.read_bytes())['media/gamepart/players/tukurxx00/playerlod00.ooo'])
        member = 'media/playercreatorpart/male/cloth/top/top_00/diff_00/1000_diffus_polo.dds'
        dds = unpack_zlb(payloads((self.assets / 'Media/Wads/PlayerCreatorMaleCloth.wad').read_bytes())[member])
        views,proof = module.render_garment_views(model,('M_MRF_VET___002_000_',),dds,size=384)
        button_pixels = {}
        for label,url in views.items():
            image = np.asarray(Image.open(BytesIO(base64.b64decode(url.split(',',1)[1]))).convert('RGB'))
            chest = image[96:173,170:214].astype(int)
            button_pixels[label] = int(((chest.min(2)>120)&(chest.max(2)-chest.min(2)<60)).sum())
        self.assertGreater(button_pixels['front'],button_pixels['back']+5,
                           'The native polo placket/buttons must identify the labeled front view')

    @unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/Houston00.wad').is_file(), 'preserved source fixture unavailable')
    def test_preserved_default_previews_use_the_open_athletes_exact_frozen_cloth_pixels(self):
        for character,target in [('federer','TukurXX'),('nadal','Houston')]:
            catalog = self.preview_catalog(character)
            source = ROOT.parent / f'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/{target}00.wad'
            entries = payloads(source.read_bytes())
            prefix = f'media/gamepart/players/{target.lower()}00/'
            texture = unpack_zlb(entries[prefix+'textures/tex.pak'])
            rows = {row['id']:row for row in parse_texpak(texture)}
            for category,identity in [('shirt','10000001c282a38d'),('shorts','10000001f1b2ff2a')]:
                choices = next(c for c in catalog['categories'] if c['id'] == category)['choices']
                preserved = next(c for c in choices if c['id'] == 'preserved')
                self.assertTrue('previews' in preserved, 'unchanged wardrobe must be visible before selecting another item')
                self.assertEqual(preserved['preview_kind'],'textured-native-garment')
                self.assertEqual(set(preserved['previews']),{'front','back'})
                self.assertEqual(preserved['preview_source']['character'],character)
                self.assertEqual(preserved['preview_source']['member'],prefix+'playerlod00.ooo')
                self.assertEqual(preserved['preview_source']['texture_sha256'],rows[identity]['payload_sha256'])
                self.assertNotEqual(preserved['preview'],choices[1]['preview'])

    @unittest.skipUnless((ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file(), 'private original Xbox assets unavailable')
    def test_unknown_foreign_category_and_unsupported_cap_choices_are_rejected(self):
        module = self.wardrobe()
        catalog = module.build_catalog(self.assets, include_previews=False)
        for selection in ({'shirt':'creator:104'}, {'shirt':'invented:999'}, {'cap':'creator:294'}, {'shirt':True}, {'coat':'creator:38'}):
            with self.subTest(selection=selection), self.assertRaises(ValueError):
                module.validate_selection(selection, catalog)

    @unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').is_file(), 'preserved source fixture unavailable')
    def test_existing_cloth_pixels_change_only_cloth_textures_with_models_and_face_exact(self):
        module = self.wardrobe()
        source = ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad'
        entries = payloads(source.read_bytes())
        prefix = 'media/gamepart/players/tukurxx00/'
        models = {f'lod{lod}':unpack_zlb(entries[prefix+f'playerlod{lod:02d}.ooo']) for lod in range(2)}
        texture = unpack_zlb(entries[prefix+'textures/tex.pak'])
        selected = {'shirt':'creator:38','shorts':'creator:104','shoes':'creator:53','socks':'creator:64'}
        same_models, changed, proof = module.apply_wardrobe(models, texture, self.assets, selected)
        self.assertEqual(same_models, models)
        self.assertNotEqual(changed, texture)
        self.assertEqual(len(proof['changes']), 4)
        before = {row['id']:row for row in parse_texpak(texture)}
        after = {row['id']:row for row in parse_texpak(changed)}
        changed_ids = {identity for identity in before if before[identity]['payload_sha256'] != after[identity]['payload_sha256']}
        self.assertEqual(changed_ids, {'10000001c282a38d','10000001f1b2ff2a','10000001226c8843','10000001b8bbaae6'})
        native_dds = payloads((self.assets / 'Media/Wads/PlayerCreatorMaleCloth.wad').read_bytes())
        for change in proof['changes']:
            row = after[change['texture_id']]
            raw_dds = unpack_zlb(native_dds[change['member']])
            self.assertEqual(changed[row['offset']:row['offset']+row['bytes']], raw_dds[128:128+row['bytes']])
        self.assertEqual(before['100000014e963a3d']['payload_sha256'], after['100000014e963a3d']['payload_sha256'])
        self.assertEqual(len(changed),len(texture))

    @unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad').is_file(), 'preserved source fixture unavailable')
    def test_cloth_texture_shared_with_face_or_wrong_geometry_is_rejected_before_mutation(self):
        module = self.wardrobe()
        source = ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX00.wad'
        entries = payloads(source.read_bytes())
        prefix = 'media/gamepart/players/tukurxx00/'
        original_model = unpack_zlb(entries[prefix+'playerlod00.ooo'])
        texture = unpack_zlb(entries[prefix+'textures/tex.pak'])
        cloth = b'M_MRF_VET___002_000_01'
        self.assertIn(cloth, original_model)
        face_model = original_model.replace(cloth, b'FACE'.ljust(len(cloth), b'_'), 1)
        with self.assertRaisesRegex(ValueError, 'non-cloth'):
            module.apply_wardrobe({'lod00':face_model}, texture, self.assets, {'shirt':'creator:38'})


if __name__ == '__main__':
    unittest.main()
