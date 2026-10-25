# Performance update — djultra64

The 120 FPS scene-capture experiment is closed at the user's request.
No 120 FPS mode is enabled or claimed.

A four-run comparison at720p, original AA and the60FPS cap used the same original
handoff and timed inputs. In the20–40second window, the development build without
capture measured59.69 and59.65FPS; the earlier BS/SDK136 snapshot measured59.65FPS.
The development scene-capture bundle measured14.75FPS without a debugger.
Background activity was not isolated and gameplay was not deterministic. This is
limited evidence for one scene, not broad hardware or sustained performance
acceptance. The BS comparison is not a new measurement of this public package.

Two changes are carried into the existing release source:
- Cache the fixed startup contiguous-reclaim option once. Allocation spans and
  free/reuse epochs still use their original live locks and checks.
- Strip inherited experimental visual-capture, forced-executor and profiler
  environment overrides from normal Linux play.

The first avoids repeated environment searches in a frequent memory query.
Tests run the actual allocator with eight concurrent query threads, check that
the option is read once, and verify live free/reuse epochs and bounds. The shell
test verifies the actual launcher with a CPU-only child. Neither test runs the
game. No specific FPS gain is claimed for this backport without new measurement.

Linux's nine workers have been relinked with the selected cache change. Windows
shares the updated runtime source; its previously published binaries have not
been replaced or retested in this closeout. Saves, graphics preferences, input
mapping and authentic third-party attribution are preserved.
