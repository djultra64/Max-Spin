# v0.1.4 portable installer validation

Own work: djultra64. Checked on 2026-10-31.

## Installation behavior

The packaged, frozen applications installed a private test ISO made from the
existing supported Xbox game files on both Linux and Windows. The ISO reader
extracted all 766 files and their SHA256 values matched the originals.

On each operating system, the completed installation was moved to another
folder. The frozen GUI and bundled preparation helpers then ran successfully.
The Windows checks explicitly waited for each GUI-subsystem process to finish
and checked its actual exit code and generated files. Both bundled athlete
helpers prepared a session without a system Python installation.

Linux's relocated private UI cache also passed its original/patched archive
identity and asset-view validation. Windows native CPU fixtures checked the
setup handoff with and without an existing completion marker, bundled helper
execution, Unicode paths and portable path confinement. No gameplay session
was opened, controlled or closed during these checks.

The custom Qt folder picker passed isolated-profile checks on Windows and
Linux. Windows cancellation and application teardown finished with native exit
code 0. The picker uses direct directory enumeration instead of Qt's native
dialogs or filesystem model, avoiding their settings and shell-cache writes.

## Automated checks

- Project tests: 140 passed, 55 skipped, 114 subtests passed.
- Windows ISO-reader tests: 22 passed, including junction rejection.
- Windows UI tests: 18 passed, native process exit code 0.
- Windows native launcher/helper fixture: 1/1 passed.
- Linux/Windows feature declarations match; archive payload hashes verified.
- Rescued Windows payload hashes match the package accepted on Windows.

The full suite including bundled upstream translator tests reports 822 passed,
68 skipped, 245 subtests passed and eight existing failures. These same
upstream failures were observed before the installer changes:

- `vendor/xboxrecomp/tools/recomp/test_lifter_fpu.py::FpuLifterTest::test_runtime_template_exports_shared_fpu_state`
- `vendor/xboxrecomp/tools/recomp/test_reserved_idents.py::ReservedIdentTest::test_unmangled_names_are_rejected`, for `isnan`, `isinf`, `isfinite`, `isnormal` and `signbit`.
- `vendor/xboxrecomp/tools/recomp/test_runtime_helpers_defined.py::RuntimeHelpersDefinedTest::test_xmm_registers_are_global_state`
- `vendor/xboxrecomp/tools/recomp/test_switch_slot_fallback.py::test_compiled_original_positive_and_negative_slots`

## Scope

This release verifies installation, resource preparation, relocation and helper
execution. It does not add gameplay coverage or establish compatibility with
every Linux distribution, graphics driver or game edition. Existing experimental
gameplay limitations remain in effect.
