# Max Spin

Max Spin is an experimental native PC recompilation project for the original Xbox version of **Top Spin**, maintained by **djultra64**. It combines locally generated game code with an open source compatibility runtime and PC controls and graphics integration.

**You must supply your own extracted game files.** This repository does not include the game, executable images, music, movies, textures, save files, or generated translations of the original executable code.

**Experimental preview.** The Linux implementation uses SDL3 and Vulkan. Windows support is under development; see [Windows status](docs/windows.md). The default is a **60 FPS host cap**; sustained 60 FPS and validated 120 FPS gameplay are not promised. Online play is not validated.

On the current Linux build, loading an existing Career has been exercised. Some appearance variants can still have black materials or incomplete clothing previews. New-save behavior on this build, complete Career progression, every appearance variant, and visual recovery after a failed save remain under test. Graphics, mode transitions, input devices, and save compatibility can still have defects.

## Getting started

For a native package, follow [Using a prebuilt preview](docs/install.md): supply the original game files, prepare local resources once with Python 3, and start the launcher.

To compile the project yourself, use the [source build instructions](docs/build.md). See [controls](docs/controls.md) and [graphics settings](docs/graphics.md) for configuration.

See [troubleshooting](docs/troubleshooting.md) when a build or launch fails. When reporting an issue, describe the game mode, the last action before the failure, and the settings needed to reproduce it. Share a small, redacted log excerpt rather than original game files or saves.

## Credits and licensing

Max Spin integration and project work: **djultra64**. The original game remains the work of its respective creators and rights holders. This independent project is not affiliated with or endorsed by them.

Original project code is available under the [MIT license](LICENSE). Bundled dependencies have their own licenses, including LGPL components; see [third-party notices](THIRD_PARTY.md). AI assistance is described in [AI_DISCLOSURE.md](AI_DISCLOSURE.md).
