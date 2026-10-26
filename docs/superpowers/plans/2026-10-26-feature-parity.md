# Feature Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load identical native athlete packages on Linux and Windows and require parity for future releases.
**Architecture:** Shared Python validation/composition; bounded binary Windows session export; native authenticated disc routing, owned by the persistent launcher.
**Tech Stack:** Python3 standard library, Win32 C/Clang, CMake/Ninja.
**Spec:** ../specs/2026-10-26-feature-parity-design.md

## Global Constraints

No game/GPU/window/input testing by agent. Preserve original game resources, saves and old releases. Own work djultra64. Normal GitHub releases only. No administrator rights, links or whole-disc copies. Equal supported user features on Linux and Windows.

## Review Focus

- Partial invalid slot must not corrupt accepted neighboring slots: shared composer/export test.
- Win32 junction/ADS/case collisions must not bypass validation: reader/path tests.
- Changed payload or manifest must not redirect silently: native hash/handle fixtures.
- Paths with spaces and Unicode must survive helper invocation: supervisor CPU fixture.
- All nine mode handoffs must retain frozen routes; stale environment must not leak: launcher/supervisor tests.

## Task1: Shared package reader and Windows export

Files: native-linux/tools/character_packages.py, character_session.py, new bounded Windows reader, tests/test_character_session.py and reader tests.
- [ ] Write and run RED tests for actual export with accepted/invalid/conflicting slots, source preservation, bounds, rollback and portable names.
- [ ] Factor shared replacement collection from asset_session; implement protocol exactly as spec with secure Windows reads and no symlinks.
- [ ] Run shared suites and actual private8athlete validation; run reader/export fixtures on native Windows.

## Task2: Native session routing

Files: windows/native/src/character_windows.{c,h}, ui_layout_windows.c, windows/native/character-tests and tests.
- [ ] RED native fixture for valid route, malformed manifest, digest mismatch, source/payload changes, disc aliases and save isolation.
- [ ] Implement bounded parser/hash/held handle routing and hook existing translator without changing UI behavior.
- [ ] Run CPU tests locally where portable and on real native Windows; no title/GPU.

## Task3: Persistent launcher integration

Files: windows/native/src/character_launcher.{c,h}, launcher_windows.c, windows/native/CMakeLists.txt, native-linux/CMakeLists.txt and launcher fixtures.
- [ ] RED fixture for preparation CLI/defaults, quoting, missing interpreter, zero slots, cleanup and handoff environment.
- [ ] Launch shared Python helper before game window, pin validated session for whole supervisor lifetime, add --characters and guaranteed owned cleanup.
- [ ] Native build and CPU fixture, retain existing controls/save/window behavior.

## Task4: Parity gate and delivery

Files: AGENTS.md, feature declaration/checker, packaging tests, README/authoring/install/Windows docs.
- [ ] RED package test refusing a platform feature mismatch; implement parity declaration/check in packager.
- [ ] Update user instructions and docs; run full available suites, branch review, Linux and Windows builds.
- [ ] Build matching source/relink distributions, verify hashes/private exclusions, merge and publish normal v0.1.3 with both binaries. Rescue Windows artifacts before controlled shutdown.
