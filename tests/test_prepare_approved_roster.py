"""Approved private package assembly contracts. Own work: djultra64."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))
from character_formats import namespace_textures, payloads, sha, unpack_zlb
import character_packages


def original_model_assembler(character, assets, research, repo):
    """Stand in only for slow approved geometry work; retain real native formats."""
    spec = importlib.util.spec_from_file_location('approved_native_fixtures', ROOT / 'native-linux/tests/test_character_formats.py')
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    models, packs = {}, {}
    for outfit in (0, 1):
        lods = {f'playerlod{lod:02d}.ooo': fixtures.fixture_model() for lod in (0, 1)}
        lods, packs[outfit], _ = namespace_textures(lods, fixtures.fixture_tex(), character)
        models.update({(outfit, lod): lods[f'playerlod{lod:02d}.ooo'] for lod in (0, 1)})
    return models, packs, {'kind': 'native model assembly fixture'}


class ApprovedPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = None
        if importlib.util.find_spec('prepare_approved_roster'):
            import prepare_approved_roster
            cls.module = prepare_approved_roster
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.assets = ROOT / 'assets'
        cls.research = ROOT.parent / 'character-research/legacy'
        cls.output = cls.root / 'candidates'
        if cls.module is not None and (cls.assets / 'Media/Wads/GamePart.wad').exists():
            cls.report = cls.module.prepare(cls.assets, cls.research, cls.output,
                                           assembler=original_model_assembler, legacy_portraits=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.assertIsNotNone(self.module, 'approved private roster packager is missing')
        if not (self.assets / 'Media/Wads/GamePart.wad').exists():
            self.skipTest('private original Xbox assets unavailable')

    def test_both_outfits_preserve_original_audio_and_use_only_existing_selector_members(self):
        for slot, target, name in ((10, 'Houston', 'Rafael Nadal'), (11, 'WangXXX', 'Novak Djokovic'), (12, 'KoenigX', 'Andre Agassi')):
            package = self.output / str(slot)
            manifest, originals, replacements, *_ = character_packages._load(package, self.assets)
            self.assertEqual((manifest['name'], manifest['author']), (name, 'djultra64'))
            self.assertEqual(len(manifest['files']), 2)
            portraits = [row for row in manifest['member_updates'] if row['member'].endswith('.dds')]
            self.assertEqual({row['member'] for row in portraits}, {
                f'media/gamepart/players/{target.lower()}00/vignette1.dds',
                f'media/gamepart/players/{target.lower()}00/vignettevisage.dds'})
            self.assertEqual(len(manifest['member_updates']), 4)
            for outfit in (0, 1):
                archive = f'Media/Wads/Players/{target}{outfit:02d}.wad'
                old, new = payloads(originals[archive]), payloads(replacements[archive])
                self.assertEqual(old.keys(), new.keys())
                for member in old:
                    if member.endswith(('soundscore.pml', 'soundevent.pml')):
                        self.assertEqual(old[member], new[member])
                prefix = f'media/gamepart/players/{target.lower()}{outfit:02d}/'
                self.assertIn(name.split()[-1].encode(), unpack_zlb(new[prefix+'player.ini']))
                self.assertNotEqual(old[prefix+'scorename.dds'], new[prefix+'scorename.dds'])

    def test_identity_updates_preserve_gameplay_stats_and_expose_serbian_flag_limit(self):
        forbidden = {'Age', 'Taille', 'Poids', 'ServeStyle', 'NaturalTalent', 'Value'}
        for slot in (10, 11, 12):
            manifest = json.loads((self.output / str(slot) / 'manifest.json').read_text())
            self.assertFalse(forbidden & {row['property'] for row in manifest['pml_updates']})
            self.assertEqual({row['archive'] for row in manifest['pml_updates']}, {'Media/Wads/Db.wad'})
            self.assertNotIn('Media/Wads/Db.wad', manifest['files'])
        proof = json.loads((self.output / '11/compatibility-report.json').read_text())
        self.assertFalse(proof['country']['native_country_available'])
        self.assertEqual(proof['country']['country'], 'Serbia')
        self.assertIn('flag', proof['country']['limitation'])

    def test_all_four_packages_compose_and_missing_or_corrupt_slot_preserves_defaults(self):
        packages = self.root / 'composition'
        shutil.copytree(self.output, packages)
        shutil.copytree(ROOT / 'characters/9', packages / '9')
        with contextlib.redirect_stderr(io.StringIO()) as warnings:
            with character_packages.asset_session(self.assets, packages) as selected:
                self.assertNotEqual(selected, self.assets)
                for target in ('TukurXX', 'Houston', 'WangXXX', 'KoenigX'):
                    self.assertNotEqual((selected / f'Media/Wads/Players/{target}00.wad').read_bytes(),
                                        (self.assets / f'Media/Wads/Players/{target}00.wad').read_bytes())
        self.assertEqual(warnings.getvalue(), '')
        (packages / '10/native/Houston00.wad').write_bytes(b'invalid')
        shutil.rmtree(packages / '11')
        with contextlib.redirect_stderr(io.StringIO()) as warnings:
            with character_packages.asset_session(self.assets, packages) as selected:
                for target in ('Houston', 'WangXXX'):
                    relative = f'Media/Wads/Players/{target}00.wad'
                    self.assertEqual((selected / relative).read_bytes(), (self.assets / relative).read_bytes())
                relative = 'Media/Wads/Players/KoenigX00.wad'
                self.assertNotEqual((selected / relative).read_bytes(), (self.assets / relative).read_bytes())
        self.assertIn('slot 10', warnings.getvalue())

    def test_rejects_existing_protected_output_and_unapproved_portrait(self):
        for output in (self.output, ROOT / 'characters/new-approved', self.assets / 'new-approved', self.research / 'new-approved'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.module.prepare(self.assets, self.research, output, assembler=original_model_assembler)
        portrait = self.root / 'wrong.png'
        portrait.write_bytes(b'unapproved image')
        with self.assertRaisesRegex(ValueError, 'approved portrait'):
            self.module.prepare(self.assets, self.research, self.root / 'bad-portrait',
                                portraits={'nadal': portrait}, assembler=original_model_assembler, legacy_portraits=True)
        self.assertFalse((self.root / 'bad-portrait').exists())

    def test_explicit_remaining_four_preserve_original_portraits_and_compose(self):
        output = self.root/'remaining-four'
        report = self.module.prepare(self.assets, self.research, output,
            characters=('becker', 'mcenroe', 'pioline', 'haas'),
            preserve_native_portraits=True, assembler=original_model_assembler)
        self.assertEqual({row['slot'] for row in report['packages']}, {13, 14, 15, 16})
        self.assertIn('provisional', report['portrait_mode'])
        before = payloads((self.assets/'Media/Wads/GameIni.wad').read_bytes())
        for slot, target in [(13, 'GideXXX'), (14, 'Riosxxx'), (15, 'Emoryxx'), (16, 'NashXXX')]:
            package = output/str(slot)
            manifest, _, _, *_ = character_packages._load(package, self.assets)
            self.assertEqual(len(manifest['member_updates']), 2)
            self.assertTrue(all(row['member'].endswith('/player.ini') for row in manifest['member_updates']))
            proof = json.loads((package/'compatibility-report.json').read_text())
            self.assertTrue(proof['portrait']['original_native_portraits_preserved'])
            self.assertIn('provisional', proof['selector_portraits'])
            for member, digest in proof['portrait']['preserved_members'].items():
                self.assertEqual(digest, sha(before[member]))
                self.assertIn(target.lower(), member)
        with contextlib.redirect_stderr(io.StringIO()) as warnings:
            with character_packages.asset_session(self.assets, output) as selected:
                after = payloads((selected/'Media/Wads/GameIni.wad').read_bytes())
                for key in before:
                    if key.endswith(('/vignette1.dds', '/vignettevisage.dds')):
                        self.assertEqual(after[key], before[key])
        self.assertEqual(warnings.getvalue(), '')

    def test_incomplete_model_assembly_leaves_no_partial_candidate(self):
        output = self.root / 'incomplete'
        def incomplete(character, assets, research, repo):
            return {(0, 0): b'incomplete'}, {0: b'incomplete'}, {}
        with self.assertRaisesRegex(ValueError, 'both outfits and both LODs'):
            self.module.prepare(self.assets, self.research, output, assembler=incomplete, legacy_portraits=True)
        self.assertFalse(output.exists())
        self.assertEqual(list(self.root.glob('.approved-roster-*')), [])

    def test_independent_portrait_roles_are_bound_and_routed_to_their_native_member(self):
        portraits, hashes = {}, {}
        for character in ('nadal', 'djokovic', 'agassi'):
            portraits[character], hashes[character] = {}, {}
            for role, color, box in (('card', 'red', (110, 20, 400, 500)), ('face', 'blue', (60, 40, 450, 450))):
                path = self.root / f'{character}-{role}.png'
                image = Image.new('RGBA', (512, 512))
                ImageDraw.Draw(image).ellipse(box, fill=color)
                image.save(path)
                portraits[character][role] = path
                hashes[character][role] = sha(path.read_bytes())
        output = self.root / 'independent-roles'
        cloth = 'Media/Wads/PlayerCreatorMaleCloth.wad'
        source_hash = sha((self.assets/cloth).read_bytes())
        def wardrobe_assembler(character, assets, research, repo):
            models, packs, proof = original_model_assembler(character, assets, research, repo)
            proof['wardrobe'] = [{'outfit': outfit, 'source_files': {cloth: source_hash}} for outfit in (0, 1)]
            return models, packs, proof
        try:
            self.module.prepare(self.assets, self.research, output, portraits=portraits,
                                portrait_hashes=hashes, assembler=wardrobe_assembler)
        except TypeError as error:
            self.fail('Independent trusted card/face inputs are unsupported: '+str(error))
        manifest = json.loads((output / '10/manifest.json').read_text())
        self.assertEqual(manifest['base_files'][cloth], source_hash)
        for row in manifest['member_updates']:
            if not row['member'].endswith('.dds'):
                continue
            image = Image.open(io.BytesIO(unpack_zlb((output/'10'/row['path']).read_bytes()))).convert('RGBA')
            red, green, blue, alpha = image.getpixel((image.width//2, image.height//2))
            if row['member'].endswith('/vignette1.dds'):
                self.assertGreater(red, 240)
                self.assertLess(blue, 10)
                self.assertEqual(image.size, (256, 256))
            else:
                self.assertGreater(blue, 240)
                self.assertLess(red, 10)
                self.assertEqual(image.size, (64, 64))
        for mode in ('swapped', 'untrusted', 'mutated', 'duplicate'):
            selected = {character: dict(roles) for character, roles in portraits.items()}
            trusted = {character: dict(roles) for character, roles in hashes.items()}
            if mode == 'swapped':
                selected['nadal']['card'], selected['nadal']['face'] = selected['nadal']['face'], selected['nadal']['card']
            elif mode == 'untrusted':
                trusted.pop('nadal')
            elif mode == 'duplicate':
                selected['nadal']['face'] = selected['nadal']['card']
                trusted['nadal']['face'] = trusted['nadal']['card']
            else:
                mutated = self.root/'mutated-card.png'
                Image.new('RGBA', (512,512), 'green').save(mutated)
                selected['nadal']['card'] = mutated
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.module.prepare(self.assets, self.research, self.root/('bad-'+mode),
                                    portraits=selected, portrait_hashes=trusted, assembler=original_model_assembler)
        def changed_wardrobe_source(character, assets, research, repo):
            models, packs, proof = wardrobe_assembler(character, assets, research, repo)
            proof['wardrobe'][1]['source_files'][cloth] = '0'*64
            return models, packs, proof
        with self.assertRaisesRegex(ValueError, 'wardrobe source hash mismatch'):
            self.module.prepare(self.assets, self.research, self.root/'bad-wardrobe-source',
                                portraits=portraits, portrait_hashes=hashes, assembler=changed_wardrobe_source)
        selection = self.root/'selection.json'
        rows = {character: {role: {'path': str(path), 'sha256': hashes[character][role]} for role,path in roles.items()} for character,roles in portraits.items()}
        selection.write_text(json.dumps({'status':'approved', 'portraits':rows}))
        self.module.prepare(self.assets, self.research, self.root/'selected-roles',
                            portrait_selection=selection, assembler=original_model_assembler)
        selected_manifest = json.loads((self.root/'selected-roles/10/manifest.json').read_text())
        selected_images = {row['member']:row['sha256'] for row in selected_manifest['member_updates'] if row['member'].endswith('.dds')}
        self.assertEqual(selected_images, {row['member']:row['sha256'] for row in manifest['member_updates'] if row['member'].endswith('.dds')})


class ExplicitReviewModeTests(unittest.TestCase):
    def test_invalid_character_selection_or_portrait_conflicts_fail_before_reading_assets(self):
        import prepare_approved_roster as module
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = [
                ({'characters': ()}, 'character'),
                ({'characters': ('unknown',)}, 'character'),
                ({'characters': ('becker', 'becker'), 'preserve_native_portraits': True}, 'character'),
                ({'characters': ('becker',)}, 'preserve_native_portraits'),
                ({'characters': ('becker',), 'preserve_native_portraits': True, 'legacy_portraits': True}, 'portrait'),
                ({'characters': ('becker',), 'preserve_native_portraits': True, 'portraits': {}}, 'portrait'),
                ({'characters': ('becker',), 'preserve_native_portraits': True, 'portrait_hashes': {}}, 'portrait'),
                ({'characters': ('becker',), 'preserve_native_portraits': True, 'portrait_selection': root/'selection.json'}, 'portrait'),
            ]
            for index, (options, message) in enumerate(cases):
                with self.subTest(options=options), self.assertRaisesRegex(ValueError, message):
                    module.prepare(root/'absent-assets', root/'absent-research', root/str(index), **options)
                self.assertFalse((root/str(index)).exists())


if __name__ == '__main__':
    unittest.main()
