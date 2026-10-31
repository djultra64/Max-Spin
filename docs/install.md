# Install Max Spin from your Xbox ISO

Download and extract the complete package for Windows or Linux.

1. Open **MaxSpin.exe** on Windows or **MaxSpin.sh** on Linux.
2. Choose your original **Top Spin Xbox ISO**.
3. Choose a new or empty folder for the portable installation and click **Install**.

Max Spin extracts the ISO, checks that the game is compatible and prepares its
resources automatically. When preparation finishes, click **Play**. You do not
need Python, an extraction utility, a terminal, PowerShell or administrator
access. The package includes its preparation tools and runtime libraries.
Your computer must support the package's operating system and Vulkan graphics.

The ISO stays unchanged and can be disconnected after installation. Use an Xbox
ISO or XISO; PC game files, archives and images from another console are not
supported. An incompatible or damaged image produces an explanation in the
assistant. You never need to find or select an individual game executable.

## Portable folder

Open the installed **MaxSpin.exe** or **MaxSpin.sh** to play again. The complete
folder can be moved to another writable location on a compatible computer of the
same operating system. Keep its contents together. Everything Max Spin creates
belongs inside that folder:

- `assets/`: game data extracted from your ISO.
- `Saves/`: your progress.
- `Settings/`: graphics and controller preferences.
- `Cache/` and `native-linux/cache/`: resources, shaders and temporary sessions.
- `Logs/`: startup diagnostics. The Windows supervisor also writes its log inside `Saves/`.

The installer creates no system installation, registry settings, desktop
shortcuts or files in AppData/home configuration folders. Existing system
graphics drivers and operating-system components are used normally.

If installation fails or you cancel, the assistant removes the unfinished files
it created and allows another attempt. It does not reuse an incomplete install.
An occupied destination is refused; choose a new or empty folder.

## Optional athlete packages

Put compatible packages in the numbered `characters/1` through `characters/32`
folders inside the installed game, then restart Max Spin. Both platforms use the
same format and bundled preparation tools. No separate Python installation is
needed. See [package authoring](../characters/AUTHORING.md).

## Development and verification

These instructions describe the new portable installer. A candidate is ready
for publication only after the frozen installation flow has passed on both
platforms. Compiling it or matching FEATURES.json alone is insufficient.

For building source rather than playing a downloaded package, see
[build instructions](build.md) and [Windows development](windows.md).
Experimental gameplay limitations remain in the [README](../README.md).
