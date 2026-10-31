# Windows status and development build

Native Windows testing has exercised the original **Menu → Exhibition → Game** flow, including a serve. This verifies a limited gameplay flow; complete graphics, audio, controller, mode transition, Career, and save compatibility remain under test. Windows builds are experimental.

For downloaded packages, use the [portable ISO installer](install.md). End users select only an Xbox ISO and destination; the tools below are for developers.

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

The expected output folder is `build/native-windows/title`, containing `MaxSpin.exe` and nine `topspin_<module>.exe` workers. Keep these executables together. Resource preparation writes local caches under `native-linux/cache`, including `ui-layout-v1` for the corrected footer and match-intro layout, and requires fresh destinations, as described in the build instructions. These private UI archives are authenticated against the original game and do not modify it. Administrator access or filesystem links are not required. Runtime DLLs required by the installed SDL3, Vulkan, shaderc, and compiler environment must be available; these commands are not a standalone binary packaging procedure.

## Starting the portable application

Open MaxSpin.exe. With an unprepared package it opens the bundled installation
assistant. A completed portable installation uses its local assets, caches,
Saves and Settings folders. It never asks for an individual Xbox executable.
The bundled frontend also prepares optional athlete packages, without a system
Python installation. Temporary native sessions stay in Cache/Sessions.

## v0.1.2 build

The Windows build includes the shared replay viewport/aspect correction and the
updated SDK capture interfaces (diagnostics are off by default). The v0.1.2
build is checked with native compilation and CPU contracts. The earlier
Menu → Exhibition → Game gameplay observation above is historical; this
release has no new Windows gameplay session.

## Local athlete packages

The native launcher now loads the same `characters/1` through `characters/32`
packages as Linux. Keep the bundled portable runtime and the full package
source/tools with the launcher. `--characters PATH` selects another directory.
Empty or invalid slots keep the original athlete, while valid slots are frozen
for the whole session, including all mode transitions.

Preparation copies only modified archives, then native workers authenticate the
manifest and original/replacement hashes before routing exact disc resources.
The runtime keeps the verified files open against writes/deletion until exit.
Saves and executables are never redirected. This requires no administrator
privileges or filesystem links. See [authoring](../characters/AUTHORING.md).
