# Building Max Spin on Linux

These steps build the native SDL3/Vulkan implementation from source. Run commands from the repository root. Build outputs and game-derived resources remain local.

## Requirements

- A 64-bit Linux environment with a native C/C++ toolchain, GNU-compatible linker, CMake **3.25 or newer**, and a build program supported by CMake.
- Python 3 with virtual environment support, **pyxbe 1.0.4**, and **Capstone 5.0.9**.
- SDL3 development headers and library, Vulkan development headers and loader, and shaderc development headers and library. A Vulkan-capable graphics driver is required to run the application.
- Your own extracted files from the original Xbox version of Top Spin. Preserve filenames and directories. Different game revisions have not been established as compatible with the current profiles.

The complete runtime source and its license notices are included in `vendor/xboxrecomp`. Its patches are already incorporated; do not apply the patch series a second time. You do not need a proprietary Xbox SDK.

## Prepare the game files

Place the extracted disc contents under `assets/`, including `Media/` and these nine executable images:

| Module | Original filename |
| --- | --- |
| Boot | `default.xbe` |
| Menu | `TopSpin(Menu).xbe` |
| Game | `TopSpin(Game).xbe` |
| Career | `TopSpin(Career).xbe` |
| Training | `TopSpin(Training).xbe` |
| Player creator | `TopSpin(PlayerCreator).xbe` |
| Shop | `TopSpin(Shop).xbe` |
| Online menu | `TopSpin(Online).xbe` |
| Stop | `TopSpin(Stop).xbe` |

The Online module is required for original menu transitions; building it does not enable the original online service. The repository provides no game-file download or extraction service.

## Generate and build

Create an isolated Python environment and install the translation dependencies:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install pyxbe==1.0.4 capstone==5.0.9
mkdir -p reports
```

Generate each module locally from your executable images:

```sh
for image in boot menu game career training playercreator shop online stop; do
    python native-linux/pipeline.py "$image" || break
done
```

Every generation must complete successfully before proceeding. Translation produces private analysis output in `native-linux/analysis/` and C source in `native-linux/src/recomp/`; neither belongs in a source release.

Configure all modules into one build directory and compile:

```sh
cmake -S native-linux -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTOPSPIN_IMAGE=menu \
  -DTOPSPIN_ADDITIONAL_IMAGES='boot;game;career;training;playercreator;shop;online;stop'
cmake --build build --parallel
```

The target names and `TOPSPIN_` configuration variables retain the original title identifier for source compatibility. The application is named Max Spin.

Use your SDL3 installation's `SDL3_DIR` when CMake cannot discover its package. For nonstandard Vulkan or shaderc installations, CMake also accepts `Vulkan_INCLUDE_DIR`, `Vulkan_LIBRARY`, `SHADERC_INCLUDE_DIR`, and `SHADERC_LIBRARY`.

## Prepare local resources

After all nine modules and the window host have built, run:

```sh
python native-linux/tools/setup_game.py --build-dir build
```

This verifies the bundled CC0 controller artwork, reads your original game resources, creates the local controller/HUD, Graphics Options, and sprite caches, and writes `build/manifest.json` so the launcher selects those resources. Original game files are not changed. No original game resources are embedded in the public source tree.

Preparation requires new destination directories and a new build manifest. It refuses existing outputs. To prepare another installation, use a separate working copy and build directory; do not remove a running application's caches. Generation and resource preparation validate only the supported profiles and resource identities, not full gameplay compatibility.

## Launch

```sh
TOPSPIN_NATIVE_BUILD_DIR="$PWD/build" bash native-linux/play.sh
```

The launcher opens the menu and keeps one window across native module transitions. A desktop session is required. Use the top-level `MaxSpin.sh` launcher when provided by your source checkout or release package.

Saves are stored separately in `native-linux/saves/`. Local caches, logs, generated sources, and saves must remain excluded from commits and source archives.

## Modifying and relinking the runtime

Edit runtime sources under `vendor/xboxrecomp` and rebuild with the same CMake command. This rebuilds the affected runtime libraries and relinks the local modules against them. Translator changes require regenerating affected modules before rebuilding. You supply the original game files for that generation; generated game code is not supplied by this repository.

Keep upstream notices intact. See [THIRD_PARTY.md](../THIRD_PARTY.md) for mixed licensing and binary redistribution requirements.
