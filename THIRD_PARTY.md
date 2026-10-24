# Third-party notices

Max Spin integration and project work are attributed to **djultra64**. Dependencies retain their original authors and licenses. The root [MIT license](LICENSE) applies to original Max Spin code; it does not replace the licenses below or license the original game.

## xboxrecomp

The bundled runtime and translation tools originate from [vyanhursky/xboxrecomp](https://github.com/vyanhursky/xboxrecomp), baseline `32aab4d1111f3caa1aa58f8ed4e8461b17ceb542`, as used by [DefJamFFNY-recomp](https://github.com/vyanhursky/DefJamFFNY-recomp), baseline `a0c9109f01dcf77f4123e12d005b76dc5b6f15ed`. Max Spin includes title integration changes and runtime modifications.

The runtime's MIT license names **sp00nz**. Keep [its license](vendor/xboxrecomp/LICENSE), [notices](vendor/xboxrecomp/NOTICE), [license texts](vendor/xboxrecomp/LICENSES/README.md), and [contributor credits](vendor/xboxrecomp/CONTRIBUTORS.md) with the source.

### LGPL components

The runtime includes xemu-derived MCPX APU and NV2A sources under the **GNU Lesser General Public License**. Their authors include **espes**, **Jannik Vogel**, and **Matt Borgerson**. Keep the individual version notices in those files: some headers specify version 2 or later, while the upstream notice describes version 2.1 or later. In addition to the files listed in [vendor/xboxrecomp/NOTICE](vendor/xboxrecomp/NOTICE), `src/nv2a/nv2a_core.c` and `src/nv2a/nv2a_state.h` identify themselves as LGPL v2+ adaptations. The retained license text is [vendor/xboxrecomp/LICENSES/LGPL-2.1.txt](vendor/xboxrecomp/LICENSES/LGPL-2.1.txt).

The corresponding runtime source is included in `vendor/xboxrecomp`, including Max Spin modifications. The [build instructions](docs/build.md) explain how to rebuild the runtime together with locally generated game modules. Users can modify these components and rebuild and relink their own local executables. If distributing binaries, preserve notices and provide the corresponding source and the materials and instructions required by the applicable LGPL terms; the source repository alone does not describe a complete binary distribution package.

## DefJamFFNY host integration

The host vocabulary and hardware integration in `native-linux/src/host.h`, `native-linux/src/hardware/ac97_bm.c`, and `native-linux/src/hardware/nv2a_regs.c` originate from DefJamFFNY-recomp. **Copyright (c) 2026 Vlad Yanhursky**, MIT. See [the full license](native-linux/licenses/DefJamFFNY-MIT.txt).

## Super-xBR

The sprite filtering code adapts Super-xBR work by **Hyllian**, copyright 2015, under the MIT license. See [the full notice](native-linux/licenses/super-xbr-MIT.txt). The reference implementation and any retained notices remain covered by their original license.

## Controller artwork

[Xelu's Free Controller Prompts](https://thoseawesomeguys.com/prompts/) are by **Nicolae (Xelu) Berbece**, with updates by **Paul Paun (2021)**, released under **CC0-1.0**. Original notices and source identities are retained in `native-linux/assets/controller-prompts-xelu/`. Max Spin resizes selected artwork into a controller prompt atlas. This artwork is separate from original game resources.

## Host and build dependencies

SDL3 uses the zlib license; the bundled runtime retains its text in [vendor/xboxrecomp/LICENSES/SDL3-zlib.txt](vendor/xboxrecomp/LICENSES/SDL3-zlib.txt). Vulkan, shaderc, Python, Capstone, pyxbe, and any other installed build tools retain their own licenses. Consult their installed distributions before redistributing them. Graphics drivers and system libraries are supplied by the host system unless an individual release explicitly states otherwise.

## Original game

No license in this repository grants rights to the original game's executable code, assets, trademarks, music, or other protected material. Users supply the game files needed for local generation and execution. Game-derived generated C, resource caches, and saves are excluded from this source release. Locally built native workers contain translations generated from the supported game executable images; the Max Spin MIT license does not grant rights to that underlying game code. Native builds still require the original game files.
