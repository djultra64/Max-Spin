"""Rasterize scoreboard names from the original italic glyph atlas. djultra64.

No operating-system font or generated lettering is used. Fixed scaling matches
the existing Federer nameplate's 54 by 13 pixel silhouette and native alignment.
"""
from functools import lru_cache
from pathlib import Path
import hashlib
import math
import re
import struct

from PIL import Image

FONT = 'NovaBlackSSK_14'
SCALE_X = 54 / 80
SCALE_Y = 13 / 15


@lru_cache(maxsize=4)
def _font(assets):
    from prepare_menu_prompts import original_entry
    abc, _ = original_entry(Path(assets), 'Media/Rui/Base.rui', 'Fonts/' + FONT + '.abc')
    xpr, _ = original_entry(Path(assets), 'Media/Rui/Base.ruirr', 'Fonts/' + FONT + '.xpr')
    if len(abc) < 28 or len(abc) > 2 * 1024 * 1024 or struct.unpack_from('<I', abc)[0] != 6:
        raise ValueError('Unsupported native scoreboard font metrics')
    width, height = struct.unpack_from('<II', abc, 8)
    ranges = struct.unpack_from('<I', abc, 20)[0]
    if any(n < 1 or n > 2048 or n & (n - 1) for n in (width, height)):
        raise ValueError('Invalid native font atlas size')
    records = 28 + 4 * ranges
    if not 1 <= ranges <= 1024 or records > len(abc):
        raise ValueError('Invalid native font ranges')
    count = struct.unpack_from('<I', abc, records - 4)[0]
    if not 1 <= count <= 65536 or len(abc) != records + 28 * count:
        raise ValueError('Invalid native font glyph count')
    if (len(xpr) < 32 or xpr[:4] != b'XPR0' or
            struct.unpack_from('<II', xpr, 4) != (len(xpr), 32)):
        raise ValueError('Invalid native font texture')
    fmt = struct.unpack_from('<I', xpr, 24)[0]
    if ((fmt >> 8) & 255 != 0x0e or (fmt >> 16) & 15 != 1 or
            (1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)) != (width, height) or
            len(xpr) != 32 + ((width + 3) // 4) * ((height + 3) // 4) * 16):
        raise ValueError('Unsupported original scoreboard atlas encoding')
    atlas = Image.frombytes('RGBA', (width, height), xpr[32:], 'bcn', (2, 'DXT3'))
    glyphs, index, previous = {}, 0, -1
    for n in range(ranges):
        low, high = struct.unpack_from('<HH', abc, 24 + n * 4)
        if low > high or low <= previous or index + high - low + 1 > count:
            raise ValueError('Invalid native font codepoint range')
        for code in range(low, high + 1):
            at = records + index * 28
            uv = struct.unpack_from('<4f', abc, at)
            if not all(math.isfinite(v) and 0 <= v <= 1 for v in uv):
                raise ValueError('Invalid native font UVs')
            pixels = [uv[i] * (width if i % 2 == 0 else height) for i in range(4)]
            box = tuple(round(v) for v in pixels)
            bearing, ink_width, advance = struct.unpack_from('<3h', abc, at + 16)
            if (any(abs(a - b) > .001 for a, b in zip(pixels, box)) or
                    box[0] >= box[2] or box[1] >= box[3] or
                    not -128 <= bearing <= 128 or not 0 <= ink_width <= 256 or
                    not 0 <= advance <= 256):
                raise ValueError('Invalid native font glyph rectangle or metrics')
            glyphs[chr(code)] = (atlas.crop(box), bearing, advance)
            index += 1
        previous = high
    if index != count:
        raise ValueError('Native font ranges do not consume all glyphs')
    proof = {'font': FONT, 'metrics_resource': 'Media/Rui/Base.rui',
             'resource': 'Media/Rui/Base.ruirr',
             'abc_sha256': hashlib.sha256(abc).hexdigest(),
             'xpr_sha256': hashlib.sha256(xpr).hexdigest(),
             'scale': [SCALE_X, SCALE_Y], 'right_edge': 125, 'top': 3}
    return glyphs, proof


def font_provenance(assets):
    return dict(_font(str(Path(assets).resolve()))[1])


def render_score_name(assets, name):
    if not isinstance(name, str) or not name.strip() or len(name) > 128 or any(ord(c) < 32 for c in name):
        raise ValueError('Invalid scoreboard name')
    label = re.sub(r'^(?:[^\W\d_]\.\s*)+', '', name.strip()).upper()
    if not label:
        raise ValueError('Invalid scoreboard surname')
    glyphs, _ = _font(str(Path(assets).resolve()))
    if any(c not in glyphs for c in label):
        raise ValueError('Scoreboard name has an unsupported native glyph')
    width = sum(glyphs[c][2] for c in label) + 512
    height = max(glyphs[c][0].height for c in label)
    if width > 8192:
        raise ValueError('Scoreboard name exceeds native raster bounds')
    canvas = Image.new('RGBA', (width, height))
    pen = 256
    for code in label:
        glyph, bearing, advance = glyphs[code]
        canvas.alpha_composite(glyph, (pen + bearing, 0))
        pen += advance
    bounds = canvas.getbbox()
    if bounds is None:
        raise ValueError('Scoreboard name has no visible native glyphs')
    cropped = canvas.crop(bounds)
    target = (max(1, round(cropped.width * SCALE_X)), max(1, round(cropped.height * SCALE_Y)))
    reduction = min(1, 120 / target[0], 13 / target[1])
    target = tuple(max(1, round(n * reduction)) for n in target)
    scaled = cropped.resize(target, Image.Resampling.LANCZOS)
    result = Image.new('RGBA', (128, 16))
    result.alpha_composite(scaled, (125 - scaled.width, 16 - scaled.height))
    return result
