"""Keep approved Federer hair identical across gameplay LODs. djultra64."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))
from character_formats import parse_model, payloads, sha, unpack_zlb
try:
    import federer_hair_consistency as hairfix
except ImportError:
    hairfix = None


@unittest.skipUnless((ROOT/'face-builder/build/automatic-federer/native').is_dir(),
                     'private accepted Federer models unavailable')
class FedererHairConsistency(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(hairfix, 'Federer native hair consistency correction is missing')

    def model(self, outfit, lod):
        return unpack_zlb((ROOT/f'face-builder/build/automatic-federer/native/Player{outfit:02d}Lod{lod:02d}.ooo').read_bytes())

    def test_high_remains_exact_and_low_replaces_only_obsolete_style_in_both_outfits(self):
        for outfit in (0, 1):
            for lod in (0, 1):
                before = self.model(outfit, lod)
                after, proof = hairfix.correct_model(before, lod)
                original, revised = parse_model(before), parse_model(after)
                self.assertEqual(proof['removed_objects'], 1 if lod else 0)
                self.assertEqual(any(n['id']=='02ea183a' for n in revised['nodes']),bool(lod))
                self.assertEqual(revised['vertices'], original['vertices']+(1662-1168 if lod else 0))
                if not lod:
                    self.assertEqual(before, after)
                nodes = {node['id']: node for node in revised['nodes']}
                for node in original['nodes']:
                    if node['id']=='02ea183a':
                        continue
                    target = nodes[node['id']]
                    self.assertEqual(before[node['start']:node['end']],
                                     after[target['start']:target['end']], (outfit,lod,node['id']))
                repeated, repeat_proof = hairfix.correct_model(after, lod)
                self.assertEqual(repeated, after)
                self.assertEqual(repeat_proof['removed_objects'], 0)

    def test_shared_builder_guard_rejects_stale_hair_but_accepts_approved_band(self):
        import approved_character_models as assembler
        guard = getattr(assembler, 'validate_hair_attachment', None)
        self.assertTrue(callable(guard), 'Native builder hair consistency guard is missing')
        raw = self.model(0, 1)
        selected = next(node for node in parse_model(raw)['nodes'] if node['id']=='75ed28ac')
        with self.assertRaisesRegex(ValueError, 'obsolete native hair'):
            guard(raw, selected)
        clean, _ = hairfix.correct_model(raw, 1)
        proof = guard(clean, selected)
        self.assertEqual(proof['selected_rows'], 1662)
        self.assertTrue(proof['selected_native_streams_exact'])
        corrupted = bytearray(clean)
        node = next(node for node in parse_model(clean)['nodes'] if node['id']=='75ed28ac')
        corrupted[node['streams'][0]['offset']] ^= 1
        with self.assertRaisesRegex(ValueError, 'selected native hair'):
            guard(bytes(corrupted), selected)

    def test_rejects_unrecognized_selected_or_obsolete_geometry(self):
        for identity in ('75ed28ac', '02ea183a'):
            raw = bytearray(self.model(0, 1))
            node = next(n for n in parse_model(bytes(raw))['nodes'] if n['id']==identity)
            at = node['streams'][0]['offset']
            raw[at] ^= 1
            with self.subTest(identity=identity), self.assertRaisesRegex(ValueError, 'hair geometry'):
                hairfix.correct_model(bytes(raw), 1)

    def test_generic_preparer_never_reintroduces_the_old_low_hair(self):
        import tempfile
        from prepare_characters import prepare
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/'candidate'
            prepare(ROOT/'assets', ROOT.parent/'character-research/legacy', output,
                    characters=('federer',))
            for archive in sorted((output/'9/native').glob('*.wad')):
                for name,data in payloads(archive.read_bytes()).items():
                    if not name.endswith('.ooo'):
                        continue
                    nodes={node['id']:node for node in parse_model(unpack_zlb(data))['nodes']}
                    if name.endswith('playerlod01.ooo'):
                        self.assertEqual(nodes['02ea183a']['streams'][0]['sha256'],nodes['75ed28ac']['streams'][0]['sha256'])

    @unittest.skipUnless((ROOT/'characters/9/native/TukurXX00.wad').is_file(),
                         'private active Federer package unavailable')
    def test_archive_preserves_high_model_texpak_scoreboard_ini_and_audio_exact(self):
        for outfit in (0, 1):
            raw = (ROOT/f'characters/9/native/TukurXX{outfit:02d}.wad').read_bytes()
            corrected, proof = hairfix.correct_archive(raw)
            before, after = payloads(raw), payloads(corrected)
            self.assertEqual(set(before), set(after))
            changed = [name for name in before if before[name]!=after[name]]
            from hair_lod_variants import ensure_model_variants
            if any(ensure_model_variants(unpack_zlb(data))[0]!=unpack_zlb(data)
                   for name,data in before.items() if name.endswith('playerlod01.ooo')):
                self.assertEqual(changed, [f'media/gamepart/players/tukurxx{outfit:02d}/playerlod01.ooo'])
            else:
                self.assertEqual(changed, [])
            self.assertTrue(proof['all_other_members_byte_identical'])
            self.assertTrue(proof['texture_pack_byte_identical'])
            repeated, _ = hairfix.correct_archive(corrected)
            self.assertEqual(repeated, corrected)


if __name__=='__main__':
    unittest.main()
