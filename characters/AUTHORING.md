# Adding athletes with native packages

Own work: **djultra64**.

Each numbered folder replaces an existing athlete identity in the game. You can
add a package without recompiling the executable. This system does not increase
the number of roster slots or automatically convert PC models to Xbox formats.
A portrait image alone does not create a playable model.

**Using an existing compatible package does not require a separate Python
installation.** Both portable launchers include their preparation runtime.
Keep the complete application package, including `portable-runtime`, together.

The command-line examples below are for **package authors**, rather than normal
installation or gameplay. Run them from the folder containing `native-linux/`,
`assets/` and `characters/`. These authoring commands require Python 3.10 or
later on Linux or Windows. In the examples, use `py -3` or `python` on Windows
instead of `python3`. Preparation and validation do not start the game or modify
save files.

Both launchers load the same packages from `characters/` when a session starts.
On Windows, the launcher prepares temporary copies of modified WADs, and the
nine native modules authenticate their paths and hashes. No administrator
privileges or filesystem links are required. Packages remain frozen through all
mode transitions; restart the game to load later changes. For portable
installations, keep packages in the `characters/` folder inside the selected
installation directory.

## Choosing a slot

Check the catalog before preparing files:

```sh
python3 native-linux/tools/character_package_tool.py catalog
```

`slots.json` contains the same mapping and its source evidence. For men,
folders 1–8 correspond to the original top row: Sampras, Hewitt, Gambill,
Grosjean, Kuerten, Chang, Blake and Robredo. Folders 9–16 are the original
bottom row: Tukur, Houston, Wang, Koenig, Gide, Rios, Emory and Nash.
The private review uses those last eight slots for Federer, Nadal, Djokovic,
Agassi, Becker, McEnroe, Pioline and Haas. This public distribution leaves
all 32 folders empty and retains the original athletes. Back up any private
package before replacing it or changing its portraits.

The female slot allocation is:

| Folder | Native identity | Database ID |
| --- | --- | --- |
| 17 | HingisX | 516 |
| 18 | AnnaXXX | 517 |
| 19 | Barbara | 518 |
| 20 | Daniela | 519 |
| 21 | Dementi | 520 |
| 22 | Coetzer | 521 |
| 23 | Harkler | 522 |
| 24 | Shaughn | 523 |
| 25 | Yamanix | 524 |
| 26 | Baretto | 525 |
| 27 | Tellawi | 526 |
| 28 | Abercro | 527 |
| 29 | Auyeung | 528 |
| 30 | Hammond | 529 |
| 31 | sergeye | 530 |
| 32 | Hillxxx | 531 |

**This female allocation is provisional and follows the native IDs. Its
visible selector order has not yet been verified.** The alphabetical order in
`playerlist.pml` has not been treated as evidence of selector order. Female
screen positions are marked as unknown in the catalog; no new female packages
are included. Female packages use only `players(w).pml` and `profils(w).pml`;
male packages use the corresponding `(m)` files.

Reserving 32 identities does not mean every historical mesh and texture variant
is already supported. Structural checks of original female resources found that
the complete outfits for slots 17, 19, 22, 23 and 24 are compatible with the
current reader. Slots 18 and 20 contain an unsupported vertex variant; slot 21
contains an additional texture format, and slots 25–32 include original resource
types that remain restricted in new packages. A future package for these
identities needs resources converted to supported formats, or the reader must
first be extended and verified. Until a package passes validation, retain the
original athlete. Checks are never skipped to activate a template.

## Creating a private template

This example prepares female slot 17 in a separate working folder:

```sh
python3 native-linux/tools/character_package_tool.py template \
  --assets assets --output-parent athlete-drafts --slot 17 --name "My athlete"
```

The result is:

```text
athlete-drafts/
  17/
    manifest.template.json
    outfit0.wad
    outfit1.wad
```

Both WADs are copied from your original files and continue to display the
original athlete until you prepare replacements. The template is disabled:
there is no `manifest.json`, and `compatibility.validated` is `false`. An
existing folder is never overwritten. Do not copy the template to `characters/`
expecting a new face; complete its resources and metadata first.

The manifest uses `maxspin-character-v1` and contains:

- `slot`, `target` and `gender`: the numbered slot, exact native identity and
  `male` or `female` family. Do not change `target` to the new athlete's name.
- `name` and `author`: the package's descriptive name and author. Use `djultra64`
  for this project's own work; retain third-party credits where applicable.
- `files`: exactly the two outfit WADs for the same target, with paths inside
  the package and SHA256 hashes.
- `base_files`: SHA256 hashes of the specific originals used to prepare the
  package. These are not original files that you should include in the package.
- `pml_updates`: individual property changes within `Db.wad`.
- `member_updates`: permitted individual replacements inside `GameIni.wad`.
- `compatibility`: the `xbox` platform, validation status and concrete evidence.
  Refreshing hashes does not certify compatibility or change this status.

## Required resources

Each `outfit0.wad` / `outfit1.wad` must retain the inventory of its original
Xbox WAD. For Hingis, members belong to `media/gamepart/players/hingisx00/`
and `hingisx01/`, respectively:

| Native resource | Requirement |
| --- | --- |
| `playerlod00.ooo` | Highest-detail model with a consistent head, body, hair and outfit. |
| `playerlod01.ooo` | Lower-detail model representing the same person, hair and accessories. |
| `textures/tex.pak` | Every texture used by both models, including hair and clothing. |
| `player.ini` | Preserve dependencies, sections and keys. Only names in `General` may be changed, and `Accessories/Hair` may be disabled with an empty string if the hair is already integrated. |
| `scorename.dds` | Scoreboard name artwork, present in both outfits. Preserve the native header, dimensions, format and data length. |
| `soundscore.pml`, `soundevent.pml` | Preserve the members and their original audio dependencies. |

Preserve the rest of the inventory. Exporting a single high-quality model or
changing only one outfit is insufficient. Integrated hair must exist in both
LODs; removing an accessory without adding its hair to the model can leave an
incomplete head. Clothing requires geometry and textures from the same native
family; do not swap male and female resources by ID.

The lower-detail file also handles distant detail. If you integrate rigid hair
from the creator, retain both native identities: `M_ACC_CHV_______000_02` and
`M_ACC_CHV_______000_03` (`F_` prefix for women). Both must contain the selected
hairstyle and its accessories. The engine hides `02` when it selects `03`;
removing the distant variant leaves the head without its opaque coverage. The
`hair_lod_variants.ensure_model_variants` helper copies the chosen style to the
distant identity and preserves the textures and geometry of other objects. The
package validator rejects a missing integrated pair or mismatched styles.

The validator checks models, bones, resources, textures, dependencies and
collisions between packages. When creating new textures, assign identities that
do not conflict with other slots; `character_formats.namespace_textures` is the
existing technical helper for rebuilding models and the texture package
together. Changing only the IDs in `tex.pak` leaves unresolved references.
Identical textures may share an ID. The template tool is not a modeler, mesh
editor or format converter.

For portraits, prepare two separate square images, preferably 512 × 512 PNGs
with transparent backgrounds: a half-body pose and a face crop. The members
the game ultimately consumes inside `GameIni.wad` are:

- `vignette1.dds`: **256 × 256**, card/pose.
- `vignettevisage.dds`: **64 × 64**, face only. This is the full native filename.

Both retain the original DDS header and payload size. Declare each member
separately in `member_updates`, using the native target and outfit path that
actually exists in the original archive. Do not invent alternate-outfit
portraits that are absent from its inventory. Preserve the original PAM-ZLB
wrapper when the member has one. `pack_zlb` and `rebuild_pam` in
`character_formats.py` preserve these contracts. Copying a PNG and renaming
it `.dds` does not convert it.

Scoreboard names are prepared as bitmaps using the same native italic
NovaBlackSSK typeface as the current packages; `scoreboard_font.py` reuses the
private original atlas. Changing a text name in the database does not
automatically change this bitmap. The existing face editor remains limited to
the characters already configured; this package format does not promise a
general graphical editor for future female athletes.

## Metadata changes by property

Female example for slot 17's `pml_updates`:

```json
[
  {
    "archive": "Media/Wads/Db.wad",
    "member": "media/db/playerlist.pml",
    "node": "TPLayerInfo/DirName=HingisX00",
    "property": "Name",
    "value": "My athlete"
  },
  {
    "archive": "Media/Wads/Db.wad",
    "member": "media/db/players(w).pml",
    "node": "TRPlayer/Id=516",
    "property": "Name",
    "value": "My athlete"
  },
  {
    "archive": "Media/Wads/Db.wad",
    "member": "media/db/profils(w).pml",
    "node": "TRPlayer/Id=516",
    "property": "Name",
    "value": "My athlete"
  }
]
```

This fragment illustrates the schema; also complete `Name`/`ShortName` for
`HingisX01`, the names in both `player.ini` files and the `Firstname` /
`Lastname` / `Country` identity fields in the `TRPlayer` rows you need. This
avoids mixed text between the selector, profile card, Career and match.
Properties must already exist in the original; no new tables, rows or keys
are added.

The selector list permits `Name`, `ShortName`, `Available`, `FromPlayerCreator`
and `RightHanded`. Retain `FromPlayerCreator=0` for integrated native models.
`IsWoman`, `DirName`, `DBName`, `CodeName` and `Id` are not rewritten. Player
rows allow names and country, age 15–65, height 120–230 cm, weight 30–180 kg,
handedness 0/1, serve style 0–3 and talent 0–2; `ZoneID` 0–8 and
`BirthplaceID` 0–1000 retain the native catalog. Use a consistent country,
country token, city and identifiers from the original: a number within the
allowed range does not guarantee that a particular city exists.
`TPlayers/Id=516` in `profils(w).pml` accepts existing numeric attributes;
names are changed in `TRPlayer`.

An existing skill uses a selector such as `TRPlayer/Id=516/Name=Forehand`, the
`Value` property and an integer from 0 to 10. Changing `Endurance` through this
route is not supported. Use JSON strings for all values, including numbers,
for example `"24"`. Text must be representable in Windows-1252, contain no
control characters and be at most 128 characters long.

Example of an individual portrait in `member_updates`:

```json
[
  {
    "archive": "Media/Wads/GameIni.wad",
    "member": "media/gamepart/players/hingisx00/vignette1.dds",
    "path": "portraits/pose.zlb",
    "sha256": "REPLACE_USING_REFRESH"
  }
]
```

The payload file may have any extension; its contents must be the correct
native DDS/PAM-ZLB member. Add another entry for the face. Do not declare
entire `Db.wad` or `GameIni.wad` archives in `files`: they are rejected so
multiple athletes can change their own rows without overwriting one another.

## Refreshing hashes, validating and installing

After editing resources and declaring changes:

```sh
python3 native-linux/tools/character_package_tool.py refresh \
  --assets assets --package athlete-drafts/17
```

This command calculates hashes for declared files, adds the required original
`Db.wad` / `GameIni.wad` bindings when individual changes are present, and
preserves compatibility status. The originals must be the untouched Xbox files
from your installation; do not point `--assets` at an already modified
temporary view. You may optionally bind `GamePart.wad` for skeleton checking
and the clothing WAD for the corresponding gender as a preparation source.
These shared WADs are not copied into the package.

Once you have confirmed the Xbox structure of every resource, record a
verifiable description in `compatibility.evidence`, set `validated` to `true`
and rename `manifest.template.json` to `manifest.json` in the working folder.
Evidence must distinguish structural validation from visual and gameplay
checks. Then run:

```sh
python3 native-linux/tools/character_package_tool.py validate \
  --assets assets --packages athlete-drafts
```

The JSON report lists accepted packages, rejected packages and disabled folders.
A rejection returns exit code 1. `valid: true` with the slot under `disabled`
means there is no active manifest yet: check that it appears under `accepted`.
The command validates files and composition without starting the game.

To install a package, copy its complete numbered folder into `characters/`,
first backing up any previous package in that slot. Run the validator again
with `--packages characters` to detect texture conflicts with other installed
athletes. Changes take effect on the next launch. If a package fails, its slot
retains the complete original identity and a diagnostic is emitted; other valid
slots remain active. To disable a package, move `manifest.json` out of its
numbered folder.

The user performs the final in-game checks: selector and profile card, both
outfits, hair and clothing at different distances, portraits, scoreboard and
handedness. Automatic validation does not establish animation quality, facial
likeness or acceptance with real controllers. Original files and saves remain
separate from the launcher's private temporary view.

## File provenance and distribution

You need your own legally obtained Xbox resources to prepare and use these
packages. Original WADs, executables, private atlases, photographs and
third-party material with uncertain permission are not included in the public
code or a public template. Locally generated templates contain private copies
of your own WADs and must not be published as freely licensed resources.
Use your own or authorized images and models, and retain their licenses and
credits. A package already integrated with game-derived data remains private;
distributing the tools' code does not grant rights to that data.
