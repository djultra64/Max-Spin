"""Reference-selected Xbox wardrobe preservation checks. Own work: djultra64."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'native-linux/tools'))
from character_formats import parse_texpak,sha,payloads,unpack_zlb,namespace_textures
from character_wardrobe import build_catalog,_preview_model
try:
    import approved_roster_wardrobe as wardrobe
except ImportError:
    wardrobe=None

class ApprovedWardrobeTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(wardrobe,'Reference wardrobe helper is missing')
        self.assets=ROOT/'assets'

    @unittest.skipUnless((ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file(), 'private native wardrobe fixtures unavailable')
    def test_each_athlete_has_two_specific_enabled_existing_outfits(self):
        for name in ['federer','nadal','djokovic','agassi','becker','mcenroe','pioline','haas']:
            outfits=[wardrobe.selection_for(name,i) for i in range(2)]
            self.assertNotEqual(outfits[0],outfits[1])
            for index,outfit in enumerate(outfits):
                catalog=wardrobe.catalog_for(self.assets,index)
                choices={c['id']:{x['id']:x for x in c['choices']} for c in catalog['categories']}
                self.assertEqual(set(outfit),{'shirt','shorts','shoes','socks'})
                for category,identity in outfit.items():
                    self.assertNotEqual(identity,'preserved')
                    self.assertTrue(choices[category][identity]['enabled'])
                    self.assertTrue(choices[category][identity]['member'].endswith('.dds'))

    @unittest.skipUnless((ROOT.parent / 'character-research/preservation-manifest.json').is_file() and (ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file(), 'private native wardrobe fixtures unavailable')
    def test_raw_native_application_preserves_geometry_and_noncloth_pixels(self):
        model,pack,_=_preview_model()
        models={'playerlod00.ooo':model,'playerlod01.ooo':model}
        after_models,after,proof=wardrobe.apply(models,pack,self.assets,'agassi',0)
        self.assertEqual(after_models,models)
        before_rows={x['id']:x for x in parse_texpak(pack)}
        after_rows={x['id']:x for x in parse_texpak(after)}
        changed={x['texture_id'] for x in proof['changes']}
        self.assertEqual(changed,{'10000001c282a38d','10000001f1b2ff2a','10000001226c8843','10000001b8bbaae6'})
        for identity,before in before_rows.items():
            if identity not in changed:
                self.assertEqual(before['payload_sha256'],after_rows[identity]['payload_sha256'])
        self.assertTrue(proof['face_textures_unchanged'])
        self.assertTrue(proof['geometry_uv_skinning_unchanged'])
        self.assertEqual(proof['selection']['shorts'],'creator:452')

    def test_invalid_names_outfits_and_mutating_returned_choice_are_safe(self):
        for args in [('unknown',0),('federer',2),('federer',True)]:
            with self.assertRaises(ValueError):wardrobe.selection_for(*args)
        result=wardrobe.selection_for('agassi',0)
        result['shirt']='invalid'
        self.assertEqual(wardrobe.selection_for('agassi',0)['shirt'],'creator:89')

    @unittest.skipUnless((ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX01.wad').is_file() and (ROOT / 'assets/Media/Wads/PlayerCreatorMaleCloth.wad').is_file(), 'private native wardrobe fixtures unavailable')
    def test_alternate_and_namespaced_bodies_keep_models_and_face_bytes_exact(self):
        source=ROOT.parent/'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Players/TukurXX01.wad'
        members=payloads(source.read_bytes());prefix='media/gamepart/players/tukurxx01/'
        models={f'playerlod{i:02d}.ooo':unpack_zlb(members[prefix+f'playerlod{i:02d}.ooo']) for i in range(2)}
        pack=unpack_zlb(members[prefix+'textures/tex.pak'])
        for namespaced in (False,True):
            before_models,before_pack=models,pack
            if namespaced:before_models,before_pack,_=namespace_textures(models,pack,'test-federer-wardrobe')
            for name in ['federer','nadal','djokovic','agassi','becker','mcenroe','pioline','haas']:
                after_models,after,proof=wardrobe.apply(before_models,before_pack,self.assets,name,1)
                self.assertEqual(after_models,before_models)
                changed={x['texture_id'] for x in proof['changes']}
                before_rows={r['id']:r for r in parse_texpak(before_pack)}
                for row in parse_texpak(after):
                    if row['id'] not in changed:
                        self.assertEqual(row['payload_sha256'],before_rows[row['id']]['payload_sha256'])

if __name__=='__main__':unittest.main()
