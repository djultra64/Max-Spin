# Max Spin

**Top Spin for PC, rebuilt from the original Xbox game.** Max Spin is a native recompilation for **Windows and Linux**, maintained by **djultra64**, with modern graphics options and controls adapted to today's controllers.

[![Max Spin Linux build — Exhibition court at 3840 × 2160](docs/images/max-spin-linux-4k.png)](docs/images/max-spin-linux-4k.png)

*Captured from our Linux build at 3840 × 2160. Click the image for full resolution.*

## Features

- **Native Windows and Linux builds** using SDL3 and Vulkan.
- **4K rendering and 16:9 widescreen**, including full-width loading artwork and corrected menu spacing.
- **60 FPS frame cap by default.**
- **Antialiasing and sprite filtering**, with Original, Super-xBR 2x and Super-xBR 4x options for supported 2D sprites.
- **Xbox, PlayStation and Nintendo Switch glyphs**, selected automatically from the active controller, with a manual family selector.
- **Controls mapped by action and physical position**, including Nintendo's A/B and X/Y layout, keyboard support and optional action remapping.
- **In-game Graphics Options** for resolution, antialiasing, sprite filtering, display mode and controller prompts.
- **Windowed and fullscreen output**, with a persistent game window and shared preferences across mode transitions.
- **Separate save and graphics settings folders**, keeping personal progress independent of display preferences.

## Getting started

1. Download the package for your platform from [Releases](https://github.com/djultra64/Max-Spin/releases/latest).
2. Supply your own extracted Top Spin game files and prepare the local resources with Python 3, following the [installation guide](docs/install.md).
3. Start **MaxSpin.exe** on Windows or **MaxSpin.sh** on Linux.

See [controls](docs/controls.md), [graphics settings](docs/graphics.md) and [troubleshooting](docs/troubleshooting.md) for setup details. To compile the project yourself, follow the [source build instructions](docs/build.md).

## Athlete packages (Linux, v0.1.2)

You can replace existing roster athletes with a compatible local package without
recompiling the game. Folders **1–16** address men and **17–32** address women.
The female allocation follows native database IDs; its visible selection order
still needs verification. Empty or invalid folders retain the original athlete.
This release includes the tools and empty folders; private custom athlete art
and original game resources are supplied separately by their owners.

The [complete authoring guide, in Spanish](characters/AUTHORING.md) explains the
folder structure, required files, validation, installation and rollback. Each
package requires `manifest.json`, both native Xbox outfit WADs, both model detail
levels, matching skeleton/material/texture dependencies, and native menu assets:

```text
characters/<number>/
  manifest.json
  outfit0.wad
  outfit1.wad
  shared/                 # optional typed GameIni replacements
```

Inside each outfit, preserve `playerlod00.ooo`, `playerlod01.ooo`, `textures/tex.pak`
and its original dependencies. Supply a posed card portrait (`vignette1.dds`,
256×256), a close face portrait (`vignettevisage.dds`, 64×64), and the scoreboard
name (`scorename.dds`, 128×16) using original native layouts. The scoreboard
lettering belongs inside both outfit WADs; the supplied helper uses the original
game font. Portrait source PNGs should be transparent, preferably 512×512. Copy
all original required members when assembling a native WAD; this is a minimum
structure, not permission to delete unknown dependencies.

From the package root:

```sh
python3 native-linux/tools/character_package_tool.py catalog
python3 native-linux/tools/character_package_tool.py template --assets assets --output-parent athlete-drafts --slot 17 --name "My athlete"
python3 native-linux/tools/character_package_tool.py validate --assets assets --packages characters
```

A template stays disabled as `manifest.template.json` until its compatibility
has been established and its manifest completed. The tool refreshes hashes and
validates complete packages and conflicts; it does not sculpt faces or convert
arbitrary PC models. The guide lists supported native formats and current
female-resource conversion limits. Never replace whole shared databases: use
the supported target-specific typed updates. Original assets and saves stay
unchanged. Automatic loading of these local athlete packages is currently Linux-only. The native Windows launcher keeps the original roster; package installation there is not implemented yet.

The replay correction scales the original replay rectangles and camera aspect to
16:9 while retaining the original camera sequence and subviews. This release
includes the same runtime correction in Linux and Windows builds. The Linux
review has passed the user’s visual replay checks; the new Windows build is
verified by compilation and CPU contracts, without a new gameplay session.

Custom hair preparation retains both native low-detail mesh identities `02`
and `03` with the selected hairstyle. The original engine hides `02` at far
distance and `03` at middle distance. Keeping both prevents a change of hairstyle
or density as the camera moves; the package validator rejects missing or
mismatched integrated pairs. Texture preparation also preserves overall cutout
coverage in lower mip levels without changing base alpha or hair colors.
The user accepted the final Linux visual review. See
[the authoring guide](characters/AUTHORING.md) for male/female identifiers and
the repair helper. Diagnostic hair and crowd tracing remains off by default.

## Bringing the Xbox game to PC

The original game ties its menus, graphics and transitions to Xbox hardware. Max Spin recompiles the game code and connects it to a native PC runtime, adapting rendering, input and window management along the way.

That work includes keeping one window across game modes, matching button prompts to each controller's actual layout, repositioning menu footers and stretching the loading artwork to 16:9 while preserving the ball's alignment. Prepared local resources carry these interface changes while keeping the original game files intact. More about the [UI and loading improvements](docs/ui-layout-20261025.md).

## Credits and licensing

Max Spin integration and project work: **djultra64**. The original game remains the work of its respective creators and rights holders. This independent project is not affiliated with or endorsed by them.

Original project code is available under the [MIT license](LICENSE). Bundled dependencies retain their own licenses; see [third-party notices](THIRD_PARTY.md). The project license does not cover the original game. AI assistance is described in [AI_DISCLOSURE.md](AI_DISCLOSURE.md).
