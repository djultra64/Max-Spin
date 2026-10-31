# Portable ISO installation

User requirement: Windows and Linux ask only for an original Xbox ISO and the
destination folder. No external Python, extraction programs, shell commands,
administrator privileges, or writes to AppData/home/system temporary folders.
Installation, extracted game data, saves, preferences, caches, logs and sessions
belong to the chosen destination. This is the highest priority; migrating old
installations is not required. Do not start gameplay for automated verification.

Implementation choice: one shared Python installation engine and Qt Widgets
wizard, frozen as an onedir application including Python, Qt and required
libraries. Onedir avoids onefile extraction into system temporary directories.
The frozen entry point also runs the existing preparation and launch helpers,
without using the machine's Python. Native Windows launcher remains the game
supervisor; Linux retains its existing Python supervisor. All mutable state is
explicitly rooted under the portable installation. Installation uses a fresh,
exclusive destination and only publishes its completion marker after ISO
extraction, game compatibility and resource preparation succeed.

Approaches considered: native independent Windows/Linux installers would duplicate
the workflow; a web frontend adds a browser/server dependency. A shared frozen
desktop wizard reuses the existing resource preparation and keeps platform
behavior aligned. The user's explicit two-input workflow fixes the design;
routine implementation decisions are delegated to the implementer.

Validate malformed/truncated/wrong-game ISO input, supported XDVDFS disc offsets,
unsafe paths and aliases, destination conflicts, cancellation, relocation,
resource generation, bundled helper execution, and absence of outside writes.
Package creation must reject an absent/incomplete frozen frontend or runtime
dependency set. Functional checks must run on both operating systems before any
publication; matching feature declarations alone do not establish parity.
