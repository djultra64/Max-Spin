"""Native engine hair02/03 selection and exact selected-style copies. djultra64."""
from pathlib import Path
import sys,struct,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'native-linux/tools'))
from character_formats import parse_model,payloads,unpack_zlb
import approved_character_models as models

class SyntheticHairVariants(unittest.TestCase):
 def test_orphan_far_variants_are_rejected_for_both_genders(self):
  import importlib.util
  from hair_lod_variants import PAIRS,validate_model_variants,ensure_model_variants
  spec=importlib.util.spec_from_file_location('hair_native_fixture',ROOT/'native-linux/tests/test_character_formats.py')
  fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
  for middle,far in PAIRS.items():
   raw=bytearray(fixture.fixture_model());node=parse_model(raw)['nodes'][0]
   struct.pack_into('<I',raw,node['start']+4,int(far,16))
   with self.subTest(gender_pair=(middle,far)):
    with self.assertRaises(ValueError):validate_model_variants(bytes(raw))
    with self.assertRaises(ValueError):ensure_model_variants(bytes(raw))
   struct.pack_into('<I',raw,node['start']+4,int(middle,16))
   with self.assertRaises(ValueError):validate_model_variants(bytes(raw))
   repaired,_=ensure_model_variants(bytes(raw));validate_model_variants(repaired)
   self.assertEqual(ensure_model_variants(repaired)[0],repaired)

@unittest.skipUnless((ROOT/'characters/9/native/TukurXX00.wad').is_file(), 'private athlete packages unavailable')
class NativeHairVariants(unittest.TestCase):
 def test_builder_emits_selected_style_at_both_native_low_details(self):
  for slot in range(9,17):
   for archive in sorted((ROOT/'characters'/str(slot)/'native').glob('*.wad')):
    for name,packed in payloads(archive.read_bytes()).items():
     if not name.endswith('playerlod01.ooo'):continue
     raw=unpack_zlb(packed);before=parse_model(raw)
     selected=next(n for n in before['nodes'] if n['id']=='75ed28ac')
     leaf=raw[selected['start']:selected['end']]
     baseline=unpack_zlb((ROOT.parent/'character-research/legacy/build/Federer-Creator-Save-Snapshot/Player00Lod01.ooo').read_bytes())
     after,proof=models._graft(baseline,leaf)
     nodes={n['id']:n for n in parse_model(after)['nodes']}
     # These are the actual original engine's 13A5B0 names: LOD1 hides
     # hair03; LOD2 hides hair02. Missing03 leaves zero selected-style hair.
     for lod,hidden in [(1,'02ea183a'),(2,'75ed28ac')]:
      visible=[n for ident,n in nodes.items() if ident in ('75ed28ac','02ea183a') and ident!=hidden]
      self.assertEqual(len(visible),1,(slot,archive.name,lod,'native detail has no selected hair'))
      self.assertEqual({k:{f:v for f,v in d.items() if f!='offset'}for k,d in visible[0]['streams'].items()},
                       {k:{f:v for f,v in d.items() if f!='offset'}for k,d in selected['streams'].items()})
      self.assertEqual(visible[0]['resources'],selected['resources'])
     n02,n03=nodes['75ed28ac'],nodes['02ea183a']
     a,b=after[n02['start']:n02['end']],after[n03['start']:n03['end']]
     self.assertEqual(a[:4]+a[8:],b[:4]+b[8:])

 def test_repair_is_idempotent_and_preserves_every_other_archive_member(self):
  from hair_lod_variants import correct_archive,ensure_model_variants
  for slot in range(9,17):
   for archive in sorted((ROOT/'characters'/str(slot)/'native').glob('*.wad')):
    raw=archive.read_bytes();revised,proof=correct_archive(raw)
    self.assertEqual(correct_archive(revised)[0],revised)
    before,after=payloads(raw),payloads(revised)
    self.assertEqual(set(before),set(after))
    for name in before:
     if not name.endswith('playerlod01.ooo'):self.assertEqual(before[name],after[name],name)
     else:
      model=unpack_zlb(after[name]);nodes={n['id']:n for n in parse_model(model)['nodes']}
      self.assertIn('02ea183a',nodes)
      self.assertEqual(ensure_model_variants(model)[0],model)
 def test_female_identity_pair_uses_the_same_native_selector_contract(self):
  from hair_lod_variants import ensure_model_variants,PAIRS
  import zlib
  self.assertEqual(PAIRS['f5d6a3ec'],f"{zlib.crc32(b'f_acc_chv_______000_03'):08x}")
  members=payloads((ROOT/'characters/15/native/Emoryxx00.wad').read_bytes())
  raw=unpack_zlb(next(v for k,v in members.items() if k.endswith('playerlod01.ooo')))
  nodes=parse_model(raw)['nodes'];female=bytearray(raw)
  for node in nodes:
   if node['id']=='75ed28ac':struct.pack_into('<I',female,node['start']+4,0xf5d6a3ec)
   elif node['id']=='02ea183a':struct.pack_into('<I',female,node['start']+4,0x82d1937a)
  revised,_=ensure_model_variants(bytes(female));identities={n['id']for n in parse_model(revised)['nodes']}
  self.assertTrue({'f5d6a3ec','82d1937a'}<=identities)
  self.assertEqual(ensure_model_variants(revised)[0],revised)
if __name__=='__main__':unittest.main()
