# Windows status and development build

Native Windows support is under development. A successful build alone does not establish complete graphics, audio, controller, mode transition, or save compatibility. Treat Windows builds as experimental until a release documents its actual validation.

## Building

Use a native Windows x64 Clang toolchain with CMake 3.25 or newer, Ninja, SDL3, Vulkan, and shaderc development packages. Generate all nine modules from your own game files using the Python pipeline described in [the source build instructions](build.md). The required Python packages and original filenames are the same on both platforms.

From an environment where those native tools and dependencies are discoverable:

```sh
cmake -S windows/native -B build/native-windows -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS_RELEASE="-O1 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG"
cmake --build build/native-windows --parallel
python native-linux/tools/setup_game.py --build-dir build/native-windows/title
```

The expected output folder is `build/native-windows/title`, containing `MaxSpin.exe` and nine `topspin_<module>.exe` workers. Keep these executables together. Resource preparation writes local caches under `native-linux/cache` and requires fresh destinations, as described in the build instructions. Runtime DLLs required by the installed SDL3, Vulkan, shaderc, and compiler environment must be available; these commands are not a standalone binary packaging procedure.

## Starting

Launch the window owner with your extracted game directory:

```text
MaxSpin.exe --assets "GAME_DIRECTORY" --resources "RESOURCE_DIRECTORY" --image menu
```

`RESOURCE_DIRECTORY` is the full path to the prepared `native-linux/cache` directory. An explicit `--resources` path avoids relying on a particular package layout. The launcher also accepts `--saves "SAVE_DIRECTORY"` and `--graphics "GRAPHICS_INI"`. Graphics preferences default to `Max Spin/graphics.ini` under Windows Local AppData.

Without `--saves`, saves default to the `Max Spin/Saves` folder under Windows Local AppData. Keep that directory when resetting graphics preferences. If `--assets` is omitted, the launcher can ask you to select the original `default.xbe`.

Windows gameplay, high frame rate physics, and broad controller/save compatibility are not claimed by these developer build instructions. Report failures with reproduction steps and a redacted log excerpt, without uploading original game files or saves.
