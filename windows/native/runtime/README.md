# Native Windows runtime adapters

Own work: djultra64. These adapters consume the current shared C runtime and
Vulkan backend. They do not contain original game assets.

`windows-runtime.patch` adds the native guest CPU token to the isolated Windows
SDK. `windows-save-runtime.patch` adds native transaction hooks to its Windows
file backend. Apply both through `../prepare_sdk.py`; the original shared SDK
and the existing Vulkan patch remain separate. `manifest.json` records exact
baseline/result file hashes and patch hashes.

## Guest execution and GPU progress

`guest_turn_windows.c` uses Win32 SRW locks, condition variables and FLS cleanup
for one guest CPU, FIFO turns and priority for time-critical guest threads.
`guest_turn_interrupts_disable/enable` implement idempotent virtual CLI/STI.
Guest threads must join before running guest code. Host GPU/audio workers do
not join unless they enter guest callbacks through the shared kernel path.

Preemption is cooperative: the Windows generation pipeline inserts
`guest_turn_checkpoint()` at guest labels, and the kernel checks again when
returning to guest code. A checkpoint never hands over while CLI is active or
a wrapped host critical section is held. This preserves host lock ownership
without suspending a host thread in a library call. Supplying only affinity,
function-entry checkpoints or guest-code range registration is insufficient
for a guest thread spinning entirely within one function.

The runtime header adapts native waits, sleeps, yields and critical sections.
A contended host lock is released before reacquiring the guest token and is
retried after reacquisition. Guest Rtl critical sections use separate adapters:
owning a guest mutex still allows unrelated guest threads to run. Native
WaitAll remains one Win32 operation.

Registered guest threads suspend cooperatively through `guest_thread_windows.h`.
The explicit FIFO queue skips parked threads while preserving nested suspend
counts. A running holder parks at a safe checkpoint, and a waking waiter cannot
reenter guest code until resumed. Guest birth is registered before its initial
native start gate is released, so `CREATE_SUSPENDED` and suspension before first
entry preserve that contract. Only never-registered host threads retain native
Win32 suspension. The actual Nt/Ke imports and guest-thread factories select
this path; current thread priority is checked when choosing the next turn.

Condition-variable users must release the guest token **before** taking their
predicate lock, and reacquire it **after** releasing that lock. The executor
job patch and `nv2a_progress_windows.c` follow that order. The progress epoch
and native condition predicate share a lock; notifications cannot be lost
between checking the epoch and sleeping. CLI/high-IRQL progress waits reject.

The actual SDK executor-owner and decoder TLS qualifiers select the compiler's
existing `XBOX_THREAD_LOCAL` contract. `_WIN32` alone does not imply support for
`__declspec(thread)` in a Clang/MinGW build.

## Whole-title Save publication

`material_save_transaction_windows.c` preserves the existing transaction API.
The Windows kernel uses wide-path and native `HANDLE` hooks rather than
converting Windows handles to POSIX file descriptors.

Before the first commit, reads resolve the existing plain
`UserData/4d530035` directory. A transaction copies its original file bytes into
a private generation, supplies separate temporary and RuntimeParams workspaces,
and redirects both file opens and attribute queries. The actual guest NtClose
import uses the tracked native close path. It rejects stale-handle
mutations, writes outside those workspaces, open staged handles, source changes,
links, malformed paths, oversized trees and failed/partial native writes.

A successful operation validates the source again, flushes bounded staged files,
and atomically replaces one small `.4d530035.current` manifest in the same
UserData directory. That manifest is the single publication point; neither
nonempty directory replacement nor a series of individual file renames is used.
Every fresh worker resolves the manifest during startup. The original plain
directory remains available as the pre-generation backup. Recognized orphan
workspaces are cleaned without following reparse points; cleanup and generation
counts are bounded. A per-title native lock rejects concurrent worker ownership.

The normal launcher stores saves under `%LOCALAPPDATA%/Max Spin/Saves`, or the
folder selected with `--saves`. Back up the complete save folder with the game
closed. To export conventional title files after a generation commit, read the
second line of `UserData/.4d530035.current` and copy the contents of that named
sibling generation to the export destination. Without a manifest, export the
plain `UserData/4d530035` contents. The file bytes, including original Pack data,
are unchanged. Preserve the complete save folder when moving between Max Spin
installations.

## FATX volume queries

The native file backend retains the original partition image identity when a
volume-directory open redirects to its host directory. Size queries read the
actual FATX superblock sectors-per-cluster field and use that image's capacity,
with available space bounded by the image and its own host filesystem. A raw
handle using unbuffered I/O is read with an aligned transfer and its file
position is restored. Short result buffers are rejected without writing them.

This preserves the original cache mount's 64KiB requirement rather than reporting
the host/default 16KiB geometry. The original formatter's filesystem-control
request retains the shared runtime's behavior; the formatter ignores that result.

## Native verification

The fixtures use actual Windows threads, synchronization and file operations.
The save fixture compiles the actual patched SDK `kernel_file.c`; only the
Xbox-to-fixture path translation boundary is supplied by the fixture.

```powershell
cmake -S windows/native/runtime/tests -B out/windows-runtime -G Ninja `
  -DCMAKE_C_COMPILER=clang -DTOPSPIN_SDK_ROOT=path/to/prepared/sdk
cmake --build out/windows-runtime
ctest --test-dir out/windows-runtime -V
```

All 25 native Windows contracts passed on 2026-10-24 with Clang 22.1.8. They cover
actual executor owner/callback TLS, two-thread TLS isolation, token/CLI exclusion,
guest/host critical sections, cooperative nested suspension and safe guest birth,
exact SDK NtClose/NtSuspend/NtResume import bodies, thread-exit cleanup, GPU epochs and multiple
waiters, original-handle mutation rejection, exact file bytes and Unicode paths,
rollback, relative-parent mapping, malformed manifests, source/handle/budget
gates, repeated commits, abrupt process-exit recovery, failed manifest replacement
preserving the last complete generation, concurrent process exclusion and fresh
worker reload, FATX volume geometry from original image headers, raw no-buffering
file-position preservation, redirected image identity, short-buffer canaries and
volume-handle cleanup. The original compiler TLS qualifiers were first observed failing
both native two-thread fixtures.

These CPU/file contracts do not establish title gameplay, controller/audio
behavior, frame pacing, persistent-window presentation or power-loss durability
on every Windows filesystem. Those require their own native acceptance runs.

The generic SDK's duplicated file-handle bookkeeping has not been extended by
this adapter. None of the nine current title images imports NtDuplicateObject;
future consumers using that API must track duplicates and close-source behavior
before relying on the staged-handle publication gate.
