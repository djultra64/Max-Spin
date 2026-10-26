# Using a prebuilt preview

A native package includes the Max Spin launcher, nine native game modules, controller artwork, preparation tools, source, licenses, and runtime relinking materials. **You must supply your own extracted original Xbox game files.** These previews have incomplete compatibility; see the limitations in the [README](../README.md) and [Windows status](windows.md).

## First launch

On Linux, double-clicking MaxSpin.sh now shows a visible message when the original
game files are missing, the native executables have not been built or are
incomplete, or the local resources have not been prepared. It checks these
requirements before opening the game window. Unexpected startup failures also
show a message and save their details in native-linux/logs/launcher-*.log.
The launcher uses Zenity or KDialog for the message. If neither is available,
it opens the explanation with the desktop text-file opener (xdg-open or GIO).

An ISO file alone is not ready to launch. This version uses the extracted disc
contents; put those files, including Media and all nine executable images, in
assets and follow the preparation step below. The launcher does not extract an
ISO or compile the game automatically. A prebuilt package already includes the
native executables, so its first setup does not require recompilation.

## Prepare your files

1. Extract the complete package into a writable folder.
2. Install Python 3. Resource preparation uses its standard library; the translation packages listed in the build instructions are needed when generating game code from source.
3. Place your extracted disc contents in the package's `assets` folder, preserving the original filenames and directories, including `Media` and all [nine executable images](build.md#prepare-the-game-files).
4. Follow the preparation command for your platform below. It reads the original resources and creates local caches without modifying the originals.

Preparation runs once per fresh package and refuses existing resource directories or a build manifest. Keep the package contents together. When updating, extract the new package separately and preserve your complete save folder.

## Linux

Requirements are 64-bit Linux, **glibc 2.38 or newer**, SDL3, Vulkan, shaderc, and a Vulkan-capable graphics driver. Shaderc's SPIRV-Tools and C++ runtime dependencies must also be installed. The native executables directly require glibc symbols through version 2.38; compatibility also depends on your distribution's library versions. Broader distribution compatibility is still being tested.

From the extracted package folder:

```sh
python3 native-linux/tools/setup_game.py --build-dir bin
./MaxSpin.sh
```

Preparation also creates a private UI layout cache for lower footer prompts and the corrected match-intro transition. Original game files stay unchanged. Linux uses a private asset view; Windows redirects authenticated UI archives and frozen athlete-package archives and requires neither administrator access nor filesystem links. Keep this cache with its prepared installation; create it again when moving to a fresh package.

Linux runtime libraries are supplied by your system. A missing-library error should be resolved through the matching distribution packages. Saves are stored in `native-linux/saves`; graphics preferences are in `native-linux/cache/pc-preferences/graphics.ini`.

## Windows

Windows native previews remain experimental. The verified gameplay flow and remaining limits are described in [Windows status](windows.md). A completed Windows package includes its non-system runtime DLLs. Keep `MaxSpin.exe`, the nine `topspin_<module>.exe` files, and those DLLs together in the extracted package folder.

From PowerShell in that folder:

```powershell
python native-linux/tools/setup_game.py --build-dir .
.\MaxSpin.exe --assets "$PWD\assets" --resources "$PWD\native-linux\cache" --image menu
```

Use `py -3` instead of `python` if that is how your Python 3 installation is invoked. The `--resources` argument selects the local resources created by preparation. You can select another original game directory with `--assets`; use that same directory with `setup_game.py --assets` during preparation.

Saves default to `Max Spin/Saves` under Windows Local AppData. Graphics preferences default to `Max Spin/graphics.ini` there. The launcher accepts `--saves` and `--graphics` for explicit locations. Preserve the complete save folder when moving or updating the application.

## Optional athlete packages on both platforms

Place compatible packages in the numbered `characters/` folders beside the
launcher. Both platforms use the same manifest, native outfits, portraits,
scoreboard name lettering and validation. Empty or rejected slots retain their
original athlete. Keep Python3 available on Windows; it prepares the frozen
session before the game window opens. No administrator privileges or filesystem
links are needed. Only changed archives are copied. Original game files and
save files remain separate.

See [package authoring](../characters/AUTHORING.md). Both launchers accept
`--characters PATH`. Restart the game to load package changes.

## Playing and reporting issues

The default is a 60 FPS host cap. Sustained 60 FPS, validated 120 FPS gameplay, complete Career appearance compatibility, and all-mode coverage are not promised by a preview package. See [controls](controls.md), [graphics](graphics.md), and [troubleshooting](troubleshooting.md).

Original game rights are separate from Max Spin's own code license. Native game modules contain generated translations of the original executable images; the Max Spin MIT license does not license that underlying game code. Bundled runtime components retain their own licenses. Source and instructions for rebuilding and relinking them are included in `RELINKING.md` in native packages.
