# Existing Xbox wardrobe textures

Own tool work: djultra64. All clothing pixels and catalog names come from the
user's original Xbox resources; no new uniform art or recolored artwork is made.

The primary preserved Federer and Nadal outfits use these compatible garment families:

| Category | Existing family | Diffuse resource ID | Enabled original catalog entries |
| --- | --- | --- | --- |
| Shirt | `top_00`, object `M_MRF_VET___002_000_` | `10000001c282a38d` | 52 |
| Shorts | `short_02`, object `M_MRF_VET___003_000_` | `10000001f1b2ff2a` | 28 |
| Shoes | `shoes_01`, paired `M_VET_CHS_*_001_____` | `10000001226c8843` | 10 |
| Socks | `socks_00`, paired `M_VET_CHT_*_000_____` | `10000001b8bbaae6` | 10 |

These are appearance choices for those exact existing shapes. Other shirt,
short and shoe shapes require creator morph/hide/body assembly and are not
substituted. No licensed athlete body or neck is swapped into either character.

The source catalog is `Media/Wads/Db.wad`, member
`media/db/profils(m).pml`, table `TPlayerCreatorItems`. Male entries use their
original `H_` descriptions and IDs, `TypeID` 13/14/15/17, and the matching
`TypeIndex` 0/2/1/0. `TextureCode1` selects the existing numeric-prefix diffuse
DDS in `Media/Wads/PlayerCreatorMaleCloth.wad`. Female entries, ambiguous paths,
missing sources, auxiliary `dark_`/`light_` textures and incompatible layouts are
not enabled. Original procedural blend/hue fields are recorded for inspection.

The tool applies the existing **raw diffuse texture**, retaining the baked
reflection maps. It does not reproduce creator procedural dark/light blending,
hue adjustment or material assembly. Shirt and shorts previews render actual
indexed native garment geometry and UVs with the original diffuse pixels, from
the front and back at512×512. The current preserved outfit also has previews
from the opened athlete's own frozen texture pack. The viewer uses a light neutral
background and click-to-enlarge images. Shoes and socks retain labeled atlas
previews. Native title rendering remains the acceptance check for final lighting.

Caps are creator haircut/accessory geometry, not cloth diffuse variants.
Original male items 69, 70 and 294 are recorded as disabled choices with a reason.
Enabling them requires separate validation of hair replacement, head fit, morphs
and hide rules. Preserved headwear remains available as the unchanged default.

`native-linux/tools/character_wardrobe.py` supplies:

- `build_catalog(assets, include_previews=True, character=None)`: source-bound catalog with
  category/choice IDs, original labels and fields, SHA256, native paths/layouts
  and optional PNG data-URL garment views or labeled atlas thumbnails. The
  character argument selects the actual preserved default for Federer or Nadal.
- `validate_selection(selection, catalog)`: rejects unknown, foreign-category
  and disabled choices. `preserved` means no change.
- `apply_wardrobe(models, texture_pack, assets, selection)`: returns identical
  model bytes, a private adjusted texture pack, and a proof. Call it before
  per-athlete texture ID namespacing.

A recipe selects catalog IDs independently of geometry and profile controls:

```json
{
  "wardrobe": {
    "shirt": "creator:38",
    "shorts": "creator:104",
    "shoes": "creator:53",
    "socks": "creator:64"
  }
}
```

Each selection is applied to the primary outfit and both of its native LOD models.
The second outfit retains its original clothing because it uses different families.
Preparation records primary_outfit_only and second_outfit_preserved explicitly.
The compiler verifies that the selected diffuse ID belongs exclusively to the
expected garment objects in every supplied LOD, including both shoes/socks. It
rejects a resource shared with face/body geometry or a different garment family.
Original Xbox DDS dimensions and DXT1/DXT5 formats must match the baked texture
row. Only the exact existing leading mip blocks are copied; surplus smaller
source mips are omitted to retain the baked pack's original size and mip count.
No pixel decoding/re-encoding is used in the actual wardrobe application.

The proof records both source WAD hashes, selected native DDS hashes, original
item IDs, unchanged model hashes, affected diffuse IDs and before/after pixel
hashes. Every other texture and every model/UV/weight/index byte remains exact.
Previews are generated only for private local viewer output. The ignored local
`face-builder/catalog/wardrobe.json` contains metadata and hashes, not game art.
Garment preview fixtures are bound to the preserved research manifest; neither
preview rendering nor selection writes to original game assets.
