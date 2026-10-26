"""Native scoreboard lettering contracts. Own work: djultra64."""
from pathlib import Path
import importlib
import io
import sys
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'native-linux/tools'))


@unittest.skipUnless((ROOT / 'assets/Media/Rui/Base.rui').exists(), 'private native fonts unavailable')
class ScoreboardFontTests(unittest.TestCase):
    def font(self):
        return importlib.import_module('scoreboard_font')

    def test_native_italic_reference_has_same_federer_extent_without_system_font(self):
        with patch('PIL.ImageFont.truetype', side_effect=AssertionError('system font fallback')):
            image = self.font().render_score_name(ROOT / 'assets', 'R. Federer')
        self.assertEqual(image.size, (128, 16))
        self.assertEqual(image.getbbox(), (71, 3, 125, 16))
        proof = self.font().font_provenance(ROOT / 'assets')
        self.assertEqual(proof['font'], 'NovaBlackSSK_14')
        self.assertEqual(proof['resource'], 'Media/Rui/Base.ruirr')

    def test_all_eight_surnames_share_native_style_and_keep_initials_out_of_bitmap(self):
        module = self.font()
        for label in ('R. Federer', 'R. Nadal', 'N. Djokovic', 'A. Agassi',
                      'B. Becker', 'J. McEnroe', 'C. Pioline', 'T. Haas'):
            with self.subTest(label=label):
                image = module.render_score_name(ROOT / 'assets', label)
                surname = label.split('. ', 1)[1].upper()
                self.assertEqual(image.tobytes(), module.render_score_name(ROOT / 'assets', surname).tobytes())
                x0, y0, x1, y1 = image.getbbox()
                self.assertEqual((y0, x1, y1), (3, 125, 16))
                self.assertGreaterEqual(x0, 5)

    def test_unknown_glyph_or_control_text_rejects_instead_of_arial_fallback(self):
        module = self.font()
        for text in ('', 'A\nB', '\u4e00', 'A' * 129):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    module.render_score_name(ROOT / 'assets', text)

    def test_common_package_helper_preserves_native_dds_header_and_transparency(self):
        from character_formats import payloads, unpack_zlb
        from prepare_characters import score_name_dds
        entries = payloads((ROOT / 'assets/Media/Wads/Players/Houston00.wad').read_bytes())
        source = entries['media/gamepart/players/houston00/scorename.dds']
        with patch('PIL.ImageFont.truetype', side_effect=AssertionError('system font fallback')):
            encoded = score_name_dds(source, 'R. Nadal', ROOT / 'assets')
        original, result = unpack_zlb(source), unpack_zlb(encoded)
        self.assertEqual(result[:128], original[:128])
        self.assertEqual(len(result), len(original))
        with Image.open(io.BytesIO(result)) as image:
            self.assertEqual(image.size, (128, 16))
            self.assertEqual(image.convert('RGBA').getchannel('A').getextrema(), (0, 255))


if __name__ == '__main__':
    unittest.main()
