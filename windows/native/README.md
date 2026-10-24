# Max Spin native Windows

This target builds the native x64 SDL3/Vulkan implementation. See [Windows build and launch instructions](../../docs/windows.md) for prerequisites, original game files, local resource preparation, and current validation limits.

The build prepares an isolated SDK from the bundled source and applies the Windows patches in the order recorded in `prepare_sdk.py`. The native window owner and all nine module workers must remain together.

Native runtime source and contracts are documented in [runtime/README.md](runtime/README.md). Keep third-party notices and the complete corresponding source and relinking materials with binary distributions.
