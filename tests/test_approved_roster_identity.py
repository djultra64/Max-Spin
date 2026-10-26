"""Approved identities compose without borrowing unrelated PC stats. djultra64."""
from pathlib import Path
import json
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))
from character_formats import Pml, pack_zlb, payloads, rebuild_pam, unpack_zlb
from character_packages import _pml_property
try:
    import approved_roster_identity as identities
except ImportError:
    identities = None


@unittest.skipUnless((ROOT / 'assets/Media/Wads/Db.wad').is_file() and (ROOT / 'assets/Media/Wads/GameIni.wad').is_file(), 'user-supplied Xbox database fixtures unavailable')
class ApprovedIdentityTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(identities, 'Approved Xbox identity helper is missing')
        self.db = (ROOT / 'assets/Media/Wads/Db.wad').read_bytes()
        self.ini = payloads((ROOT / 'assets/Media/Wads/GameIni.wad').read_bytes())

    def test_both_outfits_names_and_handedness(self):
        for character, target, full, display, short, left in [
            ('federer', 'TukurXX', 'Roger Federer', 'R. Federer', 'Fed', '0'),
            ('nadal', 'Houston', 'Rafael Nadal', 'R. Nadal', 'Nad', '1'),
            ('djokovic', 'WangXXX', 'Novak Djokovic', 'N. Djokovic', 'Djo', '0'),
            ('agassi', 'KoenigX', 'Andre Agassi', 'A. Agassi', 'Aga', '0'),
            ('becker', 'GideXXX', 'Boris Becker', 'B. Becker', 'Bec', '0'),
            ('mcenroe', 'Riosxxx', 'John McEnroe', 'J. McEnroe', 'McE', '1'),
            ('pioline', 'Emoryxx', 'Cedric Pioline', 'C. Pioline', 'Pio', '0'),
            ('haas', 'NashXXX', 'Tommy Haas', 'T. Haas', 'Haa', '0'),
        ]:
            changes = identities.database_updates(self.db, character)
            trees = {m: Pml(unpack_zlb(p)) for m, p in payloads(self.db).items() if m.endswith('.pml')}
            for change in changes:
                trees[change['member']].set(change['node'], change['property'], change['value'])
            for outfit in range(2):
                path = 'TPLayerInfo/DirName=%s%02d' % (target, outfit)
                player_list = trees['media/db/playerlist.pml']
                self.assertEqual(player_list.get(path, 'Name'), display)
                self.assertEqual(player_list.get(path, 'ShortName'), short)
                self.assertEqual(player_list.get(path, 'RightHanded'), str(1-int(left)))
                member = 'media/gamepart/players/%s%02d/player.ini' % (target.lower(), outfit)
                result = identities.character_ini(unpack_zlb(self.ini[member]), character)
                self.assertRegex(result, rb'LongName[ \t]*=[ \t]*'+display.replace('.', r'\.').encode())
                self.assertRegex(result, rb'(?m)^Name[ \t]*=[ \t]*'+full.split()[-1].encode())
                self.assertNotIn(b'Hair = Manka', result)
                self.assertNotIn(b'Hair = Dread', result)
            for table, member in [('TRPlayer', 'media/db/players(m).pml'), ('TRPlayer', 'media/db/profils(m).pml')]:
                path = table+'/Id='+str(identities.ROSTER[character]['id'])
                self.assertEqual(trees[member].get(path, 'Name'), full)
                self.assertEqual(trees[member].get(path, 'LeftHanded'), left)

    def test_updates_are_disjoint_and_leave_stats_untouched(self):
        seen = set()
        allowed = {'Name','ShortName','Firstname','Lastname','Country','BirthplaceID','ZoneID','Available','RightHanded','LeftHanded'}
        for character in ['federer','nadal','djokovic','agassi','becker','mcenroe','pioline','haas']:
            changes = identities.database_updates(self.db, character)
            keys = {(c['member'], c['node'], c['property']) for c in changes}
            self.assertFalse(seen & keys)
            seen.update(keys)
            self.assertTrue(all(c['property'] in allowed and '/Name=' not in c['node'] for c in changes))
            # Real PML serialization must retain every non-selected table/row/skill.
            original = payloads(self.db)
            for member in {c['member'] for c in changes}:
                before = Pml(unpack_zlb(original[member]))
                after = Pml(unpack_zlb(original[member]))
                for c in changes:
                    if c['member'] == member:
                        after.set(c['node'],c['property'],c['value'])
                rebuilt = Pml(after.encode())
                for c in changes:
                    if c['member'] == member:
                        rebuilt.set(c['node'],c['property'],before.get(c['node'],c['property']))
                self.assertEqual(rebuilt._fingerprint(), before._fingerprint())

    def test_country_catalog_is_native_and_missing_serbia_is_explicit(self):
        changes = identities.database_updates(self.db, 'nadal')
        self.assertTrue(any(c['property']=='BirthplaceID' and c['value']=='189' for c in changes))
        status = identities.country_status(self.db, 'djokovic')
        self.assertFalse(status['native_country_available'])
        self.assertEqual(status['country'], 'Serbia')
        changes = identities.database_updates(self.db, 'djokovic')
        self.assertFalse(any(c['property'] in ('BirthplaceID','ZoneID') for c in changes))
        self.assertTrue(any(c['property']=='Country' and c['value']=='Serbia' for c in changes))

    def test_remaining_four_bind_real_slots_and_native_countries(self):
        slots = {row['slot']: row for row in json.loads((ROOT/'characters/slots.json').read_text())['slots']}
        for character, slot, target, identifier, country_id, token, zone in [
            ('becker', 13, 'GideXXX', 19, 75, 'PAM_2955960879', 1),
            ('mcenroe', 14, 'Riosxxx', 20, 215, 'PAM_1299652986', 2),
            ('pioline', 15, 'Emoryxx', 21, 70, 'PAM_3227162272', 1),
            ('haas', 16, 'NashXXX', 22, 75, 'PAM_2955960879', 1),
        ]:
            with self.subTest(character=character):
                changes = identities.database_updates(self.db, character, target=target, identifier=identifier)
                self.assertEqual(identities.ROSTER[character]['slot'], slot)
                self.assertEqual((slots[slot]['target'].casefold(), slots[slot]['database_id']), (target.casefold(), identifier))
                status = identities.country_status(self.db, character)
                self.assertTrue(status['native_country_available'])
                self.assertEqual((status['id'], status['country_token'], status['zone_id']), (country_id, token, zone))
                self.assertTrue(any(row['property'] == 'Country' and row['value'] == token for row in changes))
                with self.assertRaises(ValueError):
                    identities.database_updates(self.db, character, identifier=identifier-1)

    def test_missing_required_native_country_rejects_instead_of_inventing_fallback(self):
        member = 'media/db/profils(m).pml'
        original = payloads(self.db)[member]
        tree = Pml(unpack_zlb(original))
        tree.set('TCountry/Id=75', 'ImageFileName', 'AbsentGermany')
        missing_germany = rebuild_pam(self.db, {member: pack_zlb(tree.encode(), original)})
        with self.assertRaisesRegex(ValueError, 'native country is unavailable'):
            identities.database_updates(missing_germany, 'becker')

    def test_unknown_or_mismatched_target_is_rejected(self):
        with self.assertRaises(ValueError):
            identities.database_updates(self.db, 'unknown')
        with self.assertRaises(ValueError):
            identities.database_updates(self.db, 'djokovic', target='KoenigX')
        with self.assertRaises(ValueError):
            identities.database_updates(self.db, 'djokovic', identifier=18)

    def test_generated_patches_pass_the_actual_loader_allowlist(self):
        for character in ['federer','nadal','djokovic','agassi','becker','mcenroe','pioline','haas']:
            info = identities.ROSTER[character]
            manifest = {'slot': info['slot'], 'target':info['target']}
            trees = {m:Pml(unpack_zlb(p)) for m,p in payloads(self.db).items() if m.endswith('.pml')}
            for change in identities.database_updates(self.db, character):
                tree = trees[change['member']]
                _pml_property(tree, change, manifest)
                self.assertEqual(tree.get(change['node'],change['property']),change['value'])


if __name__ == '__main__':
    unittest.main()
