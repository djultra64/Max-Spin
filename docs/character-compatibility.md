# Native Xbox character compatibility

Own work: djultra64. Private source-derived assets remain outside tracked source.
This records the measured conversion contract, not athlete likeness approval.

## Verified native route

`native-linux/tools/prepare_characters.py` reads the preserved character WADs
under `../character-research/legacy/build/Federer-Creator-Test/Media/Wads` and
checks their byte counts and SHA256 against the migration preservation manifest.
It rebuilds **Xbox original archive templates**, retaining every unrelated
payload. It never installs a PC `Db.wad`, `GameIni.wad`, executable or ADN.

The actual original selector capture
`reports/character-native-candidates-20261025/default-034000ms.png` establishes
row-major display order: Sampras, Hewitt, Gambill, Grosjean, Kuerten, Chang,
Blake, Robredo, Tukur, Houston, Wang, Koenig, Gide, Rios, Emory, Nash. Their
database IDs are7 through22. Thus Federer occupies folder**9**, internal
Tukur/Id15; Nadal occupies folder**10**, internal Houston/Id16. The alphabetical
PlayerList thumbnail table is not the displayed selector order.

Private candidates are generated outside the active `characters` directory.
`reports/character-candidates-ui-20261025` contains both verified native
packages. The earlier `character-candidates-20261025` directory records the
superseded thumbnail-order experiment and must not be activated.

## Measured formats

- **PAM_PAK WAD:**36-byte header;8-byte data records;24-byte lookup records;
  CP1252 name pool; case-insensitive full-path CRC32. Header/table/name CRCs,
  parent hierarchy and payload bounds are checked. An empty or unchanged
  replacement returns the exact original archive, including original padding.
- **PAM-ZLB:** bounded block dimensions, offset table, all three metadata CRCs,
  block CRCs, decoded lengths and complete zlib consumption are checked.
  Original raw versus compressed storage is retained when replacing a member.
- **PML:** complete bounded tree/dictionary reader; stable table/row selectors
  such as `TRPlayer/Id=15/Name=Forehand`. Unmodified encoding is byte-identical.
  Shared database changes address existing target rows and properties only.
- **OOO:** root`0x1234567c` contains a typed object array. Supported records are
  rigid`0x12345679` and skin`0x1234569b`; the parser consumes every byte through
  the final ending. It checks MA2/MA3 descriptors, resource tables, draw opcodes
  and counts, material/texture slots, strip index ranges, bounds, stream byte
  counts, finite positions/normals/UVs, and bone palettes.
- **Skin streams:** formats0/14/15/16 have strides32/40/48/52. Skinned records
  contain one/two/three explicit floating weights and an implicit final weight.
  Every explicit weight is finite and within0..1 (floating tolerance1e-5), their
  sum is bounded, and every used16-bit palette index addresses an existing row.
  Conversion never changes these bytes.
- **FIG:** ignored serialized pointer, node count,44-byte transforms/name
  offsets and a CP1252 string pool. Palette bone IDs are CRC32 of the actual
  case-sensitive names. In particular, Xbox names `B_lèvre_inf_G_01_01` and
  `B_front_G_01` resolve the two custom face palette IDs absent from shipped
  Tukur's own palette; no bone renaming or weight remapping is necessary.
- **Tex.pak:** magic`0x1234569f`,36-byte rows and contiguous complete mip payloads.
  Character formats0/4/8 are supported. BC1/BC3 pixels remain byte-identical;
  each athlete receives its own resource IDs, with matching typed OOO reference
  changes, so simultaneous packages cannot replace another athlete's face. IDs
  also bind pixel/layout identity: a selected primary-outfit texture cannot
  collide with an unused unchanged copy in the second outfit's complete pack.

The Xbox rigid loader itself supports136-byte `PAM-OBJ` userdata:
Menu`C75F0` calls`D1840`, which reads`0x88` bytes or rewinds when the signature
is absent. The converter removes only these structurally identified captured
runtime records. It verifies node bindings, full vertex/skin streams, materials
and indices survive. A raw search-and-delete is never used.

Representative original Xbox Tukur LOD00 decoded hash:
`633f13375ac2e858547925254ebcbb3c8b4337dd49f84e8b1301cfab5191a445`
(602808bytes,23objects). The retained custom LOD00 has37objects and different
topology; matching magic alone would not prove compatibility. All eight actual
outfit/LOD meshes passed the complete grammar, original Xbox312-name FIG binding
and texture dependency checks after conversion.

## Metadata and dependencies

Both outfits retain the preserved creator uniforms and their original pixel
payloads, unless an explicit compatible wardrobe texture is selected. Original
Xbox INI fields stay exact except the three display-name fields and
`Accessories/Hair`, which is emptied deliberately. Baked models contain their
own hair; retaining Manka/Dread draws an additional original hairstyle. No
headband, shadow or other attachment is removed. PlayerList handedness and the targeted
database identity, body, style and skill values compose independently for both
athletes. `FromPlayerCreator` stays at the Xbox original value; baked OOO is the
authoritative route and no edited PC003 ADN regeneration is requested.

Score names and retained source portraits use the exact original Xbox DDS header
and layout. Where source BC2/BC3 alpha encodings differ, only the alpha blocks
are converted; source RGB blocks remain exact. Alpha quantization can differ
between BC2 and BC3. The portrait is retained research, not proof that it matches
the final edited head. Optional `--federer-portrait` and `--nadal-portrait` PNG
inputs replace stale portraits with the Face Builder's transparent renderer
export. Preparation checks a square64..2048 PNG of at most8MiB, decodes RGBA,
and encodes the original256×256 and64×64 layouts with exact original DDS
headers. This path resizes/encodes supplied pixels without image generation.
Original sound-event and sound-score PML members stay
byte-identical; generic athlete-neutral announcer replacement remains separate
work rather than accepting arbitrary resource paths.

Failed standalone DDS reads in the native preview are the original archive
fallback. For example, all requested IDs ending`74bec81e`, `0b55dd4a`,
`d28ceb52`, `f7aebf9f`, `f2f3bf91`, `3917dd5c`, `72569427`, `f8ee514c`
exist with their exact namespaces in original`Media/Textures/Tex.pak`.
Original`GamePart.wad`'s Manka hair references the first five. The subsequent
global Tex.pak open appears in the actual session log; these are not missing
namespaced character dependencies. The later duplicate-hair correction empties
the legacy attachment setting while preserving intrinsic baked hair.

## Face Builder recipes

Preparation accepts `--federer-recipe path.json` and `--nadal-recipe path.json`.
The optional compiler verifies the recipe against independently reconstructed
immutable mesh/texture identities, applies geometry before the Xbox conversion,
and preserves UVs, indices, skinning and materials. No recipe retains the
existing cached geometry exactly. Explicit zero Federer controls reconstruct the
accepted immutable Face Builder base rather than numerically undoing a previous
trial.

Optional `recipe.profile` contains `displayName`, `firstName`, `lastName`,
`shortName`, `age`, `countryId`, `heightCm`, `weightKg`, `leftHanded`,
`backhandStyle`, `playStyle`, and a `skills` dictionary. The editor catalog uses
the70 original Xbox country rows and23 skills explicitly declared0..10.
Endurance, ServeMastery and unknown SkillNN semantics are preserved. Backhand
choices are native0/1; playing-style choices are the proven0/1/2. Integer,
range, identity and CP1252 string validation precede composition. Applicable
PlayerList/Players/Profils records agree, and a changed display name regenerates
the scoreboard bitmap. Missing optional profile/portrait inputs retain research
defaults. The source catalog and strict validation live in
`native-linux/tools/character_profile.py`.

Optional `recipe.uvAlignment` carries the nine anatomical point names (and
legacy eye-inspection names), each with a mesh triangle/barycentric surface and
desired atlas UV point. Optional `recipe.pointControls` separately moves actual
geometry using source-bound barycentric Gaussian interpolation. The editor's
main mesh→texture workflow produces both fields together.
Preparation invokes `character_uv.apply_uv_alignment` after optional geometry
compilation and before Xbox conversion. Its per-model proof records the exact
changed UV bytes. This route changes only typed face UV records; positions,
normals, skinning, topology and source texture pixels stay fixed. No alignment
is inferred or automatically applied. Unsupported or folding correspondence
rejects preparation as a whole. Native UV fold validation checks the actual
indexed triangle strips after float32 serialization in every outfit and LOD.

Optional `recipe.wardrobe` selects existing original Xbox diffuse textures for
the **primary outfit only**. The second outfit keeps its existing clothes and
pixels. Both native meshes, body UVs, skinning and face textures remain intact.
Each outfit's proof names this scope explicitly. Source clothing WAD/database
hashes are bound; caps and incompatible garment geometry are rejected instead
of substituting an unverified shape. The editor labels this first-outfit scope.

`reports/character-recipe-validation-20261025/zero` and `nose-height` demonstrate
actual private packages for explicit zero and `noseHeight=0.3`, including all
four Federer outfit/LOD variants and simultaneous unchanged Nadal. Both were
accepted by the real frozen-session loader. Their JSON recipes sit beside those
directories. This establishes the recipe-to-native-resource path, not visual
acceptance of those example edits.

## Native title evidence and limits

`reports/character-native-head-preview-20261025/default-047000ms.png` shows both
replacement3D heads, Federer white band, preserved uniforms and equipment
together in the original Head-to-Head screen, with R.Federer and R.Nadal names.
This is real native title rendering, beyond an offline viewer. Root-controlled
match/outfit/animation evidence is recorded separately in the migration report.
Athlete likeness, facial expression quality and physical-controller acceptance
remain manual judgments.

No nine-image binary rebuild is needed for this Linux resource route: the
launcher passes one frozen private assets root through every image handoff.
Original disc resources and saves are not opened for writing. Windows workers
also accept an assets root, but the supplied Windows launcher still has its
fixed compiled UI override contract; this Linux preparation/launcher work does
not claim tested Windows automatic character integration.
