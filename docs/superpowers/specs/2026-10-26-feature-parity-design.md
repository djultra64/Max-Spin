# Linux and Windows feature parity

Own work: djultra64.

The user requires matching features on both platforms and authorizes the work needed to achieve it. Implement the existing Linux local athlete-package feature in the native Windows launcher, then make equal declared features a release requirement. Keep the shared package format,32slots, source hashes, native-format checks, shared database composition, two outfits, portraits, native-font names, wardrobe and hair variants. Invalid slots retain defaults. Original game data and saves must remain unchanged.

Approach chosen by implementer: retain one Python validator/composer and add a Windows session export plus authenticated native routing. A separate C reimplementation would duplicate validation. A full game-file copy would consume unnecessary disk space. Filesystem links would impose Windows privilege/filesystem requirements. This approach copies only changed WADs, requires no elevation or links, and uses the Python3 installation already required for game setup.

## Session protocol

`native-linux/tools/character_session.py --assets PATH --characters PATH --output NEW_DIRECTORY` runs the shared validator and composer. It writes only frozen replacements under `files/0000.wad`, etc., and `manifest.bin`. No original writes or full asset copies. Empty/invalid-only selections produce a valid zero-entry manifest. Output must not exist and partial output is removed on error.

Binary manifest: magic8bytes `MSCHAR1\0`, little-endian uint32 count (maximum96). Each record has uint16 ASCII resource-path byte length (maximum240), path bytes (strict supported `Media/Wads/Db.wad`, `Media/Wads/GameIni.wad`, or existing32native identities/bothoutfits under `Media/Wads/Players`), uint64 replacement length (maximum128MiB), raw32byte original SHA256, raw32byte replacement SHA256. Reject duplicates, trailing bytes and malformed records. Record order determines payload filename `files/%04u.wad`.

The Windows launcher defaults to adjacent `characters`, accepts `--characters`, scrubs inherited session variables and launches the helper only when numbered package manifests exist. Locate `py -3` or `python` without a shell; quote arguments correctly, wait for completion and show preparation errors before creating the game window. The launcher validates the exported session and pins its manifest digest into worker environment:
`MAX_SPIN_CHARACTER_SESSION`, `MAX_SPIN_CHARACTER_MANIFEST_SHA256`, `MAX_SPIN_CHARACTER_ASSETS`.

Native API in `windows/native/src/character_windows.{c,h}`:
- `int topspin_windows_character_session_validate(const wchar_t *assets,const wchar_t *session,char digest[65]);`
- `int topspin_windows_character_route(const char *guest,wchar_t *host,unsigned count);` tri-state0unclaimed,1replacement,-1invalid active session.
- `void topspin_windows_character_session_close(void);`

The existing `xbox_translate_path` wrapper invokes character routing after original translation, before its RUI-specific route. Only exact disc resources can redirect; U/T saves, executables, unknown paths, traversal, ADS and prefix collisions cannot. Verify original and frozen payload hashes and retain read handles without write/delete sharing during each process lifetime. Workers load the same pinned session across every handoff. The launcher closes handles and removes only its owned session files after workers exit.

On Windows, shared Python package reads use Win32 handles rejecting reparse components and nonregular/oversized files; retain POSIX descriptor behavior on Linux. Enforce portable path names and case-collision handling consistently.

## Verification and delivery

Tests first: shared export/fallback/composition, Windows bounded reads/reparse refusal, native manifest/hash/path routing including save isolation and changed payload refusal, helper command quoting and cleanup, session persistence across handoffs. Run native Windows CPU fixtures without starting game/GPU/windows; the user performs gameplay. Build both full native distributions. Keep normal releases only and djultra64 authorship. Record parity policy in repository instructions and release metadata/checks; update docs to show the same installation/format on both platforms. Publish a new normal version with both binaries and matching source after checks pass. Preserve v0.1.2 and all originals/saves.
