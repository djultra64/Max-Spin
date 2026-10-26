"""Keep custom cutout hair visible across native mip distances. djultra64."""
from pathlib import Path
import io
import struct
import sys
import tempfile
import json
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))
from character_formats import parse_model, parse_texpak, payloads, sha, unpack_zlb
try:
    import hair_mip_coverage as correction
except ImportError:
    correction = None

HAAS_SOURCE = ROOT/'reports/render-release-20261025/activation-backups/source/16/native'


def dds_chain():
    """Literal half-opaque base; intentionally lost alpha in lower mips."""
    levels = []
    header = None
    for size in (8, 4, 2, 1):
        image = Image.new('RGBA', (size, size), (47, 83, 121, 48))
        if size == 8:
            image.putalpha(Image.frombytes('L', (8, 8), bytes([255]*4+[0]*4)*8))
        stream = io.BytesIO()
        image.save(stream, format='DDS', pixel_format='DXT5')
        encoded = stream.getvalue()
        if header is None:
            header = bytearray(encoded[:128])
            struct.pack_into('<I', header, 28, 4)
        levels.append(encoded[128:])
    return bytes(header)+b''.join(levels)


def levels(raw):
    """Independent Pillow decoder, including only addressable native pixels."""
    width, height = struct.unpack_from('<II', raw, 12)[::-1]
    offset = 128
    for level in range(max(1, struct.unpack_from('<I', raw, 28)[0])):
        w, h = max(1, width >> level), max(1, height >> level)
        size = ((w+3)//4)*((h+3)//4)*16
        header = bytearray(raw[:128])
        struct.pack_into('<II', header, 12, h, w)
        struct.pack_into('<I', header, 28, 1)
        image = Image.open(io.BytesIO(bytes(header)+raw[offset:offset+size])).convert('RGBA')
        yield offset, size, list(image.getchannel('A').getdata())
        offset += size


class HairMipCoverage(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(correction, 'Native custom hair mip coverage correction is missing')

    def test_cutout_coverage_survives_minification_without_repainting_rgb_or_base(self):
        before = dds_chain()
        after, proof = correction.correct_dds(before)
        self.assertEqual(after[:128+64], before[:128+64])
        self.assertEqual(len(after), len(before))
        self.assertGreater(proof['changed_levels'], 0)
        decoded = list(levels(after))
        self.assertEqual([sum(a >= 128 for a in alphas) for _, _, alphas in decoded], [32, 8, 2, 1])
        for start, size, _ in decoded:
            for block in range(start, start+size, 16):
                self.assertEqual(after[block+8:block+16], before[block+8:block+16])
        again, repeat = correction.correct_dds(after)
        self.assertEqual(again, after)
        self.assertEqual(repeat['changed_levels'], 0)

    def test_transparent_or_single_level_material_stays_byte_identical(self):
        for alpha in (0, 255):
            stream = io.BytesIO()
            Image.new('RGBA', (8, 8), (18, 42, 71, alpha)).save(stream, format='DDS', pixel_format='DXT5')
            raw = stream.getvalue()
            self.assertEqual(correction.correct_dds(raw)[0], raw)
        with self.assertRaises(ValueError):
            correction.correct_dds(dds_chain()[:-1])

    @unittest.skipUnless((HAAS_SOURCE/'NashXXX00.wad').is_file(), 'private frozen Haas source unavailable')
    def test_haas_native_hair_terminal_mips_survive_and_all_other_archive_members_stay_exact(self):
        source_hashes = ('e5fa08f8084aef6e79f88be6d9e3311994a924c5a46c86f15f74c021bbb2b9aa',
                         '6b080d9396906e6d5b9a628033896bf5eb6a46f1202d237346049cce339c884a')
        for outfit in (0, 1):
            before = (HAAS_SOURCE/f'NashXXX{outfit:02d}.wad').read_bytes()
            self.assertEqual(sha(before), source_hashes[outfit], 'Frozen original Haas fixture changed')
            after, proof = correction.correct_archive(before)
            original, revised = payloads(before), payloads(after)
            self.assertEqual(set(original), set(revised))
            self.assertTrue(proof['changed_texture_ids'])
            for name in original:
                if not name.endswith('/textures/tex.pak'):
                    self.assertEqual(original[name], revised[name], name)
                    continue
                old_pack, new_pack = unpack_zlb(original[name]), unpack_zlb(revised[name])
                old_entries, new_entries = parse_texpak(old_pack), parse_texpak(new_pack)
                self.assertEqual(old_entries[0].keys(), new_entries[0].keys())
                for old, new in zip(old_entries, new_entries):
                    self.assertEqual({k:v for k,v in old.items() if k!='payload_sha256'},
                                     {k:v for k,v in new.items() if k!='payload_sha256'})
                    start, size = old['offset'], old['bytes']
                    if old['id'] not in proof['changed_texture_ids']:
                        self.assertEqual(old_pack[start:start+size], new_pack[start:start+size])
                changed = {entry['id']:entry for entry in new_entries if entry['id'] in proof['changed_texture_ids']}
                self.assertIn('100000033001aa1a', changed)
                entry = changed['100000033001aa1a']
                # Native Haas authored terminal two levels were all alpha48.
                for offset in (entry['offset']+entry['bytes']-32, entry['offset']+entry['bytes']-16):
                    self.assertGreaterEqual(new_pack[offset], 128)
            self.assertEqual(correction.correct_archive(after)[0], after)

    @unittest.skipUnless((ROOT/'assets/Media/Wads/GamePart.wad').is_file()
                         and (ROOT.parent/'character-research/preservation-manifest.json').is_file()
                         and (ROOT/'face-builder/build/automatic-haas/haas-face-v1.obj').is_file(),
                         'private original assets and preserved accepted character sources unavailable')
    def test_both_future_package_builders_publish_corrected_alpha_and_honest_pixel_proof(self):
        from prepare_characters import prepare as prepare_generic
        from prepare_approved_roster import prepare as prepare_approved
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            research = ROOT.parent/'character-research/legacy'
            prepare_generic(ROOT/'assets', research, root/'generic', characters=('federer',))
            prepare_approved(ROOT/'assets', research, root/'approved',
                             characters=('haas',), preserve_native_portraits=True)
            for output, slot in ((root/'generic', 9), (root/'approved', 16)):
                package = output/str(slot)
                proof = json.loads((package/'compatibility-report.json').read_text())
                for outfit in proof['outfits']:
                    self.assertTrue(outfit.get('hair_mip_coverage', {}).get('changed_texture_ids'), outfit)
                    self.assertFalse(outfit.get('all_source_pixel_payloads_preserved', True))
                for path in (package/'native').glob('*.wad'):
                    raw = path.read_bytes()
                    self.assertEqual(correction.correct_archive(raw)[0], raw)


if __name__ == '__main__':
    unittest.main()
