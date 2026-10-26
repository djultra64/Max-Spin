# Offline title file-open evidence

Own work: djultra64. No title, GPU, game window or input execution.

The private exported analysis under `native-linux/analysis/menu/disasm/asm/text.asm` records the original instructions used below. The same addresses are represented in generated C where these SDK/CRT routines were lifted. No original game bytes are included in this fixture.

- Guest `0x111C3` and `0x111CF` pass the literal Db.wad and GameIni.wad paths into `sub_00098140`.
- Its existence probe calls `sub_00097190`, which calls `sub_0010C361(path,0x8000)`. The archive reader `sub_00097BE0` calls `sub_000976B0`, which also calls `sub_0010C361` for a disc file.
- At guest `0x10C386`, the CRT open wrapper sets ECX to `0x40` (`_SH_DENYNO`) before `sub_0010C0CB`. Guest `0x10C14E` maps that selector to the literal share mask `3`. Guest `0x10C276` pushes this mask into `sub_00016538` (CreateFileA equivalent).
- Guest `0x16642` forwards the caller's share mask unchanged into the NtCreateFile argument list. Guest `0x1664F` adds access `0x100080` (SYNCHRONIZE and FILE_READ_ATTRIBUTES); guest `0x1665A` calls NtCreateFile. Read-only, normal, existing disc archive opens therefore use DesiredAccess `0x80100080`, ShareAccess `3`, FileAttributes `0x80`, CreateDisposition `1`, CreateOptions `0x60`.
- The title's `sub_0009D0D0` and `sub_0009E790` read-only file paths also call the same CreateFileA wrapper with explicit share mask `1` (guest `0x9D107` / `0x9E7DC`). Both include FILE_SHARE_READ.

The actual SDK `kernel_file.c` maps ShareAccess bits 1/2/4 directly to FILE_SHARE_READ/WRITE/DELETE and passes them unchanged to CreateFileW. Its NtOpenFile implementation delegates to NtCreateFile. These observed disc reads are compatible with the session's retained GENERIC_READ handles and deny-write/delete sharing.

`sdk_file_open.c` compiles the selected real SDK `kernel_file.c`, then opens and reads the frozen synthetic payload through `xbox_NtCreateFile` and `xbox_NtReadFile` using both authentic share masks. It does not replace the native file-open implementation with a mock. An SDK path root is supplied only at the original translator boundary; the real character-routing wrapper runs before the actual SDK file open.

Configure the CPU fixture with `-DTOPSPIN_SDK_ROOT=<prepared-sdk>` to enable these additional reads. The portable run exercises the actual POSIX SDK implementation; a native Windows run is required to establish Win32 sharing behavior. ShareAccess zero was not observed on these traced archive paths and is not claimed compatible with retained data-read handles.
