# Portable ISO installer implementation plan

> Execute with superpowers:executing-plans; use independent agents only for isolated extraction/native adapter work under superpowers:dispatching-parallel-agents.

**Goal:** Install and run from an Xbox ISO and destination folder, without user-installed tools or writes outside the portable destination.

**Architecture:** Shared XDVDFS reader, transactional installer and Qt wizard frozen with Python; existing preparation reused in-process. Portable launch adapters root all state inside the installation.

**Tech Stack:** Python standard library, Qt Widgets/PySide6, PyInstaller onedir, existing C Windows supervisor.

**Spec:** ../specs/2026-10-31-portable-iso-design.md

## Global constraints

Only ISO and destination are user inputs. No gameplay automation. No external
Python or system installation. Identical Linux/Windows features. Attribution djultra64.

## Review focus

Malformed disc trees and path aliases; interrupted/cancelled installs; moving
the installed directory; hidden writes by subprocesses and GUI frameworks;
frozen helper process recursion and incomplete packaged dependencies.

### Task 1: ISO extraction

Create `portable/xdvdfs.py`, `tests/test_xdvdfs.py`.
Interface: `XboxISO(path).entries`, `extract(destination, progress=None, cancelled=None)`.
- [x] Write/run failing synthetic ISO extraction and rejection tests.
- [x] Implement bounded extraction and supported original Xbox offsets.
- [x] Run tests on Linux and Windows.

### Task 2: Installation engine and wizard

Create `portable/install.py`, `portable/app.py`, installer tests.
Interfaces: `install(iso, destination, payload, progress=None, cancelled=None)`;
frozen application accepts `--tool SCRIPT [ARGS]` and `--play` internally.
- [x] Write/run failing install transaction/relocation/confinement tests.
- [x] Implement fresh destination, payload copying, extraction, preparation and marker.
- [x] Build two-input wizard with progress, cancellation, actionable failures and Play.
- [x] Verify real preparation and GUI without starting gameplay.

### Task 3: Portable launch adapters

Modify Windows native launcher/athlete helper and Linux launcher helpers.
- [x] Write/run failing portable state/default-path/helper tests.
- [x] Root assets/resources/settings/saves/sessions/logs inside installation.
- [x] Use bundled helper for Windows athlete packages and Linux startup.
- [x] Verify native Windows behavior and Linux integration.

### Task 4: Packaging and user instructions

Modify `tools/pack_release.py`, create `tools/build_portable.py`, update FEATURES.json and docs.
- [x] Write/run failing packaging tests for missing frontend/runtime dependencies.
- [x] Include frozen onedir frontend and complete runtime dependency closure.
- [x] Update installation guide to ISO → destination → Play.
- [x] Run full suite, parity checker, frozen frontend and platform verification.
- [x] Obtain independent review and address material findings. Do not publish unverified binaries.
