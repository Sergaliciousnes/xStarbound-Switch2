> Historical development notes: the early proof-of-concept status below is superseded. The port now runs gameplay and mods on hardware. See [current installation/controls](switch2-tester-readme.md) and [source build instructions](switch2-build.md). Captured logs referenced here are local investigation artifacts and are not distributed with the source.

# Switch 2 proof of concept

The SDK Vulkan clear example has been built on Windows/MSYS2 and confirmed
working on the console. xStarbound now has a Switch 2 CMake toolchain, native
Vulkan integration, and an asset-free renderer test. Both NROs compile and
link. The engine renderer test has now passed on the console; game startup
and gameplay remain unverified.

## Confirmed console results

The supplied console logs confirm 180 presented frames and 180 verified frames
at 960x540 on `Switch 2 GPU (NVK model 0x808)`, reporting Vulkan API 1.3.0.
The runtime probe passed concurrent SD reads, positional writes/cursor
restoration, randomness, and engine-thread creation. The process reported
9,152 MiB available and six allowed CPU cores.

The diagnostic uploaded 1,244 textures, retired 544 after frame fences, and
reported 2,530 unique descriptors plus 205 reuses. The captured
`dist-switch2/vulkan-world.png` shows the synthetic fixture. Validation layers
and debug messages were unavailable; the zero validation-error counter must
be interpreted with that limitation. Stderr contains only the driver's NVK
non-conformance notice. Logs are retained in `dist-switch2/`.

The next hardware milestone is the game main menu with the user's base-game
`assets/packed.pak`, then a fresh local world, save, exit, and reload.

## Second-launch diagnostic update

The client log in `dist-switch2/second_launch/xstarbound-startup.log` stops
before asset scanning while opening `./storage/xclient.log`, with an I/O error.
It does not yet establish whether the newly supplied base asset pack loads.
Closing/reopening the host title restored FTP access to the startup logs.
The underlying storage-log open failure is not yet identified.

The updated client treats storage-file logging as optional on Switch 2 and
continues with `sdmc:/xstarbound-startup.log` if opening/rotating `xclient.log`
fails. Required storage operations still report errors. File-open errors now
include errno, open flags, and the last native Horizon filesystem result.
The update compiled and linked; its console behavior still needs testing.

## Third-launch display update

The user confirmed the main menu is visible. `dist-switch2/third_launch/xclient.log`
confirms both asset packs load, Lua preprocessing and engine databases load,
configuration/player storage are created, and the main update loop runs.
The log reports an 800x600 (4:3) swapchain, explaining horizontal stretch when
VI fills a 16:9 display. The title/main-menu milestone is reached; playing,
saving/reloading a local world remains unverified.

The revised client creates a fixed 1280x720 SDL/VI render target before Vulkan
initialization. UI dimensions use the same size. Desktop window/fullscreen/
maximize requests and resize notifications cannot reintroduce a 4:3 buffer or
mismatched UI dimensions. Existing configuration files need not be deleted.
The new logs should report `fixed render size 1280x720` and a `1280x720`
swapchain. Compilation/linking and NRO header checks passed; display behavior
of this revision remains to be tested on the console.

## Loading performance investigation

The successfully tested client revisions were Debug builds. Their actual
compile commands use `-Og -g -fno-lto` without `NDEBUG`, including engine asset
code and the Pluto/Lua VM. The build script now defaults to a separate
RelWithDebInfo directory using conservative `-O2 -g -DNDEBUG` settings. This
retains the working filesystem behavior, renderer and thread counts so the
next console run can compare build optimization without an I/O redesign.

The supplied third-launch log reports asset initialization taking 14.09 s and
`Root::fullyLoad` waiting 42.33 s for its database preload work. These are
startup wall-clock observations, not CPU profiles. Individual database times
can include dependency/lock waits and overlap, so they must not be summed or
interpreted as exclusive execution times. New aspect-fixed launch timings have
not yet been supplied, and no optimized-client timing has been measured.

Further candidates found in the code, not yet quantified:

- PackedAssetSource parses archive metadata/index through an unbuffered
  DataStreamIODevice. Variable-length integers can issue one-byte reads.
  Archive indexes are read during source discovery and again during loading.
- The temporary Horizon positional-I/O implementation serializes operations
  on each shared File while it seeks, reads/writes, and restores its cursor.
  This can restrict simultaneous reads from the same pack. Do not remove its
  lock without replacing the positional-I/O implementation correctly.
- Initial loading uses four Root database workers and two asset workers.
  Available process cores alone do not show actual CPU utilization or clocks.
- No external mods still entails loading the full base-game databases and the
  required xStarbound asset pack/on-load scripts.

Next measurement: launch the optimized client from a fully closed host title,
keep the same assets/configuration/storage, and collect startup/stderr and
storage/xclient.log. Confirm `build profile RelWithDebInfo` in startup output.
Compare asset and fully-loaded timings with the baseline. Test both an initial
launch and a repeat launch; record handheld/docked state. No speedup is claimed
until those console measurements are available.

## Console controls update

The initial native HID control layout is implemented in the optimized client.
See `switch2-controls.md` for the mapping and hardware acceptance checks.
A standalone host test passed movement hysteresis, button/shoulder edges,
focus/disconnect releases, cursor timing, touch precedence and clamping.
The full optimized client cross-compiles and links. Controller hardware feel
and touchscreen alignment remain to be checked on the console.

## World-entry file replacement update

The logs in `dist-switch2/enter_world/` stop on `rename error: File exists`
when entering a world. The preceding missing player `metadata` warning is a
recoverable first-run condition, not the fatal exception. The old rename error
omitted paths, so the exact failing file is not established; player metadata
and other saves use the affected `.new` replacement path.

The Linux file implementation assumed rename could overwrite an existing
file. The Switch 2 implementation now first attempts direct rename; only an
EEXIST failure for regular files invokes a backup/promote/rollback sequence.
The old destination is moved to a unique backup, the staged file is promoted,
and the backup is removed only after success. Promotion failure attempts to
restore the old destination. Failed cleanup retains the backup and logs a
warning instead of aborting an already-successful save. Existing backups are
never overwritten. All staged overwriteFileWithRename writes are synced and
closed before replacement. Failure messages now include both paths, native
filesystem results, the replacement stage and rollback details.

This sequence is not power-loss atomic. An interruption between the renames
can leave a `.replace-backup-<suffix>` file; preserve it for recovery. Back up
storage before testing. Do not delete storage or old saves to work around the
original error. The application must be fully closed before transferring logs.

Standalone host failure-injection tests passed: absent/existing destination,
missing source, backup failure/collision, promotion rollback, rollback failure,
cleanup failure and refusal to replace a directory. These test the replacement
state machine, not Horizon's actual filesystem. The optimized client compiles
and links; world entry and save/reload remain to be verified on the console.

## Shipworld temporary-file update

The logs in `dist-switch2/enter_world_2/` confirm successful player login to
the local server and initial ship creation, then fail with
`tmpfile error: No such file or directory`. UniverseServer creates the initial
ship using File::ephemeralFile(), which still used the inherited P_tmpdir.
That Unix temporary path is unavailable on the console.

All File temporary-file/directory helpers now use
`sdmc:/switch/xstarbound/storage/tmp`, created on demand. Anonymous temporary
files retain a private on-disk name until their descriptor is closed, then are
removed; they are not unlinked while open. The native File handle owns this
cleanup path without changing the public File layout or persistent-file names.
After abnormal termination, leftover temporary files may remain in this folder.
Keep them separate from permanent player/world saves.

The same close-before-unlink order is used to release SD lock sentinels.
An old build can have left `storage/universe/universe.lock`; remove only that
stale sentinel after fully closing the host title if it remains. Do not delete
player files, universe databases, or shipworld files as a workaround.

The updated xvulkan-test NRO now checks multiple concurrent temporary files,
seek/positional reads, resizing, flush, close/destructor cleanup and exclusive
lock release, before the previous SD I/O/thread/rendering checks. Both NROs
cross-compile and link and their headers/architecture checks pass. The new
runtime checks and ship creation still require a console run. No new host-only
simulation is claimed as validation of Horizon's real filesystem.

## First-world freeze diagnostics

The `enter_world_3` logs confirm the temporary-file/lock runtime regression
passed and the local server creates the ship and begins warping the player.
Logs then end after the randomized starter-world search starts and runtime
configuration is written. There is no logged exception or Vulkan failure.
The user reports a frozen loading animation and no audio for two minutes.
The freeze's cause is not established by these logs.

The new client arms an independent watchdog on starting a local server. It
writes `sdmc:/xstarbound-world-trace.log` every five seconds through its own
stdio file, without the engine logger or engine mutexes. Snapshots contain
approximate stage ages for the main thread, Vulkan, server tick, client-world
initialization/lighting waits, celestial chunk work and lighting worker.
Counters show presented frames, server ticks, completed client world starts,
random search attempts, loaded/generated chunk misses and completed file
reads/writes. Read/write counts are operation counts, not bytes or durations.
The celestial stage is shared by its callers and is approximate, not a stack
trace. Idle worker/lighting labels can normally have large ages.

Console frame-fence, acquire-image and readback waits now have a 30-second
timeout, so a supported timed wait can produce a logged Vulkan timeout instead
of silently waiting forever. Queue present/device idle and shader creation do
not offer the same timeout here; their stages are still reported by the
watchdog. This is a diagnostic revision, not a confirmed freeze fix.

A host test of the real watchdog implementation with clock/sleep shims passed:
while main was blocked for six seconds, snapshots continued and background
server counters advanced. That test also confirmed repeated arming does not
spawn a second watchdog/truncate the log. This does not validate Horizon's
thread scheduling/filesystem. The complete client cross-compiles and links.

Next console run: replace xclient.nro, retry the same character, and allow
40-60 seconds if it freezes. Fully close the host title, then collect
`/xstarbound-world-trace.log`, startup/stderr and `storage/xclient.log`.
Keep player/universe files to reproduce the same transition. Remove only a
stale universe.lock after the host title is fully closed if one remains.

## Build on Windows

Use devkitPro MSYS2 (not native Windows CMake). Required packages:

```sh
pacman -S --needed cmake ninja switch-sdl2 switch-libpng switch-freetype switch-harfbuzz switch-libogg switch-libvorbis switch-libopus
export DEVKITPRO=/c/devkitPro
cd "/c/Users/Prairie/Downloads/Switch 2 Homebrew Game/xStarbound-main"
sh scripts/switch2/build.sh xvulkan-test_nro
sh scripts/switch2/build.sh xclient_nro
```

A host `glslangValidator` is required. The build script recognizes the Windows
Vulkan SDK through `VULKAN_SDK`, or uses the shader compiler in `PATH`.
Outputs now default to `build-switch2-optimized/source/application/xvulkan-test.nro`
and `build-switch2-optimized/source/client/xclient.nro`. The default profile is
RelWithDebInfo (`-O2 -g -DNDEBUG`, normal floating-point semantics, no LTO).
The previous Debug output in `build-switch2/` is retained for comparison.
To rebuild Debug explicitly, use
`STAR_BUILD_TYPE=Debug STAR_BUILD_DIR="$PWD/build-switch2" sh scripts/switch2/build.sh xclient_nro`.
The script disables desktop integrations, OpenGL, asset packing, optional
allocators, tests, and Tracy. Do not enable `STAR_RECENT_COMPILER`: its desktop
configuration can select x86 architecture flags.

The toolchain derives from devkitPro's Switch CMake support, substitutes
libounce, puts Switch 2 libraries first, defines `__SWITCH2__`, and enables NRO
packaging. MSYS CMake's absolute PCH include paths are incompatible with the
native Windows cross compiler, so PCH is disabled on that host.

## First console test: actual engine renderer

Create `sdmc:/switch/xstarbound/` and place `xvulkan-test.nro` there. Launch
through the same OunceHEN loader used for `vulkan-clear`. No game assets or
save files are required. This executable runs the real renderer's synthetic
world fixture for 180 frames, verifies rendered pixels, and exits automatically.
It exercises embedded shaders, textures, descriptors, offscreen world rendering,
lighting/compositing, readback, and presentation. It does not load a game world.

Evidence:

- `sdmc:/switch/xstarbound/vulkan-diagnostic.log`
- `sdmc:/switch/xstarbound/vulkan-world.png` (when capture succeeds)
- `sdmc:/xstarbound-vulkan-startup.log`
- `sdmc:/xstarbound-vulkan-stderr.log`

A passing log reports nonzero presented and verified frames and zero validation
errors. Validation layers may be unavailable on the console; zero reported
validation errors alone does not establish correctness. The verification path
requires a readable RGBA8/BGRA8 UNORM swapchain and reports failure otherwise.

## Game deployment

Use a separate test installation with assets you own. Place these under
`sdmc:/switch/xstarbound/`:

```text
xclient.nro
xsbinit.config
assets/packed.pak
xsb-assets/xSBassets.pak
mods/
storage/
```

Use the `xsbinit.config` in the corresponding build directory; it uses paths
relative to the SD game directory.
Do not copy a desktop configuration that contains build-machine paths. The
client changes to this SD directory before loading configuration and keeps
that directory instead of replacing it with SDL's executable base path.
Startup output goes to `sdmc:/xstarbound-startup.log` and
`sdmc:/xstarbound-stderr.log`. Normal game logs are under storage.
Start with a new player/world to test loading, saving, audio, controls, and exit.

## Horizon adaptations and remaining limits

- Vulkan uses the static OunceVK loader, VI surface on `nwindowGetDefault()`,
  and native drawable dimensions. SDL provides window events/input; it is not
  asked to load Vulkan or create a Vulkan window.
- Random bytes use libounce `randomGet`; allowed CPU cores come from
  `svcGetInfo`. Engine pthreads have 1 MiB stacks. Unsupported thread naming
  and timed mutex diagnostics are omitted; normal mutex locking remains.
- Positional SD file reads/writes use seek/read-or-write/restore protected by
  a per-File mutex, including ordinary cursor operations. Console stress tests
  of asset streaming and saving remain necessary.
- File mappings in Soup use owned read buffers. Binary module loading is
  unavailable, memory-protection/foreign-code facilities are unsupported,
  and Soup's OS DNS record-query backend reports failure (other resolver
  backends remain). The initial target is local play, not multiplayer parity.
- Exception messages remain available, but symbolic stack traces are not.
- SD lock files use exclusive sentinel creation rather than flock. A crash
  can leave a stale lock; remove only the relevant stale lock after confirming
  the game has exited. Never delete locks from an active session.
- Actual memory budget, game performance, suspend behavior, and driver feature
  compatibility must be measured on the console; the working SDK sample does
  not establish those properties for the engine.

## Fourth world-entry trace: CPU render stall

`enter_world_4_diagnostic/xstarbound-world-trace.log` records one completed
world start, 129 presented frames, and server ticks continuing to 616 while
main remains in application rendering for approximately 59 seconds. The
lighting worker is back in its condition wait. No later frame reaches the
Vulkan presentation checkpoints. These observations locate the stall in CPU
application rendering, but do not identify the blocking operation.

The refined diagnostic adds render/texture channels and main gather/paint
boundaries. Render checkpoints cover entity callbacks/directives, lightmap
copy/preparation, tile copying, sky data, tile chunks, stars, debris, orbiters
and horizon. Texture checkpoints cover standalone conversion and atlas
insertion. Labels retain the most recent checkpoint and are not call stacks.
Use `xstarbound-switch2-cpu-render-diagnostics.zip` for the next hardware run.

## Fifth world-entry trace: zero-worker tile copy deadlock

`enter_world_5_diagnostic` remains at `gather: copy tile array` for 57.8 seconds,
with one completed world start, 160 frames and the server advancing to 607
ticks. `TileSectorArray::tileEachTo` calls the parallel sector-column visitor.
The visitor queues jobs in a static worker pool and waits on their handles.
Its worker count came directly from `std::thread::hardware_concurrency()`.

Disassembling that function in the actual linked client ELF shows exactly
`mov w0, #0; ret`. Thus the pool started zero threads and queued jobs could
never finish. `SectorArray2D::getWorkerPool` now clamps the requested count to
at least one. This leaves all detected nonzero counts unchanged, preserves
parallel visitor behavior and avoids changing the general WorkerPool API
(which supports intentionally stopped pools). The finer watchdog remains.

This is a concrete deadlock fix; successful world rendering still requires a
console run. Update package: `xstarbound-switch2-tile-worker-fix.zip`.

## World entry confirmed and additional controls

The user confirmed successful world entry with the tile-worker minimum-count
fix. The next controls revision adds ZL as a second left mouse trigger while
retaining ZR, and maps physical Nintendo Y to E (default PlayerInteract).
Both triggers are combined before mouse edge generation, so releasing only
one does not release a click still held by the other. Interact follows normal
key down/up semantics and is released on disconnect or focus loss.
Update package: `xstarbound-switch2-zl-y-controls.zip`.

## Native worker count, world block cache and FPS overlay

The user confirmed ZL click/Y interact work and requested a performance pass.
On Switch 2, SectorArray2D now queries the native core mask through
Thread::numberOfProcessors (six cores in prior console logs) instead of the
standard library's constant-zero hardware_concurrency. Other platforms keep
the standard-library count and minimum-one fallback.

Switch 2 Vulkan parallax idle retention is 256 MiB. Cleanup bypasses sorting
and filtering if all idle textures fit. Live textures remain protected, so
the configured allowance describes idle logical RGBA bytes rather than total
CPU/GPU texture memory. The existing 64 MiB allowance remains on desktop.

WorldStorage enables a lazily populated 32 MiB raw block LRU and expands index
retention to 4096 nodes. ReadBlockCache is disabled by default elsewhere.
rawReadBlock checks transaction buffers first; committed reads can use cached
full blocks, including leaves. rawWriteBlock and commitWrites invalidate changed
blocks. Open/close/rollback/compaction clear stale entries. Unchanged cached
blocks survive ordinary commits. Existing file format and flush ordering stay
in place. Per-file positional I/O locking remains necessary for cursor safety.

Installed libounce fsdev_seek machine code confirms SEEK_CUR/SEEK_SET mutate
local offsets; the positional wrapper does not imply four filesystem IPCs.
The cache targets repeated actual read calls. Startup read counts do not prove
exploration I/O saturation; new terrain and asset misses can still stall.

A shadowed, 16-pixel FPS overlay draws last at (8,8) in physical screen space.
Its source is the existing renderFps monitor; it is independent of debug HUD,
interface scale and camera zoom. It is enabled for Switch 2 builds only.

The portable cache host test passes, including concurrent access. A real
BTreeDatabase warm-read/write/rollback/commit/reopen regression is included in
both the core tests and asset-free test NRO; the latter needs a hardware run.
No console speedup or overlay appearance has been measured yet. Update package:
xstarbound-switch2-performance-fps.zip. Compare the same world/zoom/mode while
stationary, revisiting terrain and exploring new terrain. Detailed notes:
dist-switch2/performance-fps-notes.txt.

## Planet slowdown: responsive frame meter and phase measurements

The user reports roughly 10 FPS on planets and high-50s on ships, with motion
hitches, and no apparent benefit from the previous performance pass. Those
observations do not identify whether CPU update/render, GPU work or streaming
is dominant. The previous FPS source used a one-second TickRateMonitor decay.
Switch 2 now publishes the reciprocal of each completed-frame interval instead,
with frame milliseconds and a one-second held peak in the bottom-left overlay.

StarSwitch2FrameProfile accumulates main-thread CPU wall times and workload
counts. The independent watchdog emits [performance] rows every five seconds
into the existing world trace. Nested phases overlap; averages/maxima are per
frame, and snapshot boundaries are approximate. The first row may include menu
frames. Measurements cover updates (including catch-up count), gather/paint,
tile setup, sky, lighting wait, finish, Vulkan fence/upload/textures/acquire/
record/present, asset misses and positional reads. Main-thread-only I/O timings
must not be interpreted as total background storage work. GPU fence/present
waits are CPU observations, not GPU timestamps. Trace output now buffers each
snapshot and flushes once rather than writing each field separately.

The linked SDK pthread syscall implementation already applies the process's
allowed core mask before starting worker threads. No affinity change is made.
recordGeometry skips identical consecutive descriptor binds and push constants,
with conservative resets at render-pass boundaries. Draw order is unchanged.
This optimization does not establish the cause of the reported slowdown.

Host tests verify instant 100 ms -> 10 FPS response, next-frame recovery,
peak hold/expiry, profile snapshots and worker-thread exclusion. The independent
watchdog blocked-main test still passes. Hardware captures of stationary/moving
ship and planet play are needed to choose the next substantial optimization.
Update package: xstarbound-switch2-frame-profile.zip.

## Detailed CPU measurements after world_trace

Six steady planet windows in world_trace contained 339 frames, averaging
88.54 ms/frame: update 42.56 ms, gather 16.32 ms and paint 23.00 ms. Tile setup
averaged 0.51 ms, previous-frame GPU fence wait 0.12 ms, and main-thread file
reads 0.05 ms. Client catch-up ran three updates per rendered frame. A 1225 ms
frame included 1165 ms of updating. These observations locate the broad costs,
but do not establish the individual expensive routines or cause of CPU delays.

The detailed profiler splits updates into packets, world simulation, player
scripts, interface/mixer and storage. Entity update/render callbacks are grouped
by player, monster/NPC, object, plant and other. Gathering separates light
sources/preparation, highlights, entity callbacks/directives, tile copies and
environment data. Painting separates lighting-map preparation, parallax,
entity layers, terrain/liquids, particles, overlays and cleanup. Main-thread
Lua protected calls/resumes and explicit collection are timed; Lua-call wall
time includes callbacks and automatic GC. Save serialization, writes and sync
can distinguish save-related hitches from other update work. Workload counts
include entity passes, drawable layer sets, particles, periodic saves and packets.

Same-phase nesting is suppressed so recursive Lua calls and nested save wrappers
do not inflate totals. Distinct phases remain inclusive/overlapping, and maxima
refer to sums per rendered frame. Host tests cover nested suppression, explicit
stop and exception unwinding, in addition to prior meter/snapshot tests. Extra
clock reads add profiling overhead; no speed improvement is claimed. The ZIP
is xstarbound-switch2-cpu-detail-profile.zip. Capture ship/planet stationary and
moving intervals, then collect the existing trace after fully closing the title.

## world_trace_2: plant generation and entity painting

The first loaded world in this run is the planet; a later warp returns to the
ship. Six planet windows (+15..+40s, 334 frames) average 89.92 ms/frame. Main
updates cost 38.11 ms, gather 18.42 ms and paint 24.90 ms. Plant callbacks cost
11.24 ms; entity-layer painting costs 16.01 ms. Packet handling is 8.55 ms and
outgoing state generation 6.71 ms. Lua calls average 0.89 ms, main file reads
0.34 ms and GPU fence waiting 0.13 ms. Nested timings overlap. Periodic saves
also produce isolated ~140 ms work, while multi-second startup/warp/disconnect
frames should be distinguished from steady gameplay.

Plant::render previously generated every piece for all loaded plants before
WorldPainter performed image-level visibility checks. On Switch 2 the client
now compares cached conservative plant visual bounds with its padded viewport
using wrapping WorldGeometry. A render overload skips only visual pieces,
preserving damage particles/audio and all simulation/network processing.
Envelopes cover flips, any branch/leaf angle, crown translation and damage
jitter. Piece changes rebuild the envelope. Directives, inspection/highlights,
interactive targeting and unavailable/very wide bounds bypass culling.

The pure production envelope helper passed 871,200 corner checks. The frame
profiler and blocked-main watchdog tests still pass. New trace fields show
plants drawn/culled per frame and entity delta read/write time. The watchdog
also queries CPU/GPU rates read-only through clkrst and records errors when
unavailable. No clock/voltage/affinity/simulation-rate settings are changed.
This is a targeted rendering optimization; console gains and plant appearance
still require verification. ZIP: xstarbound-switch2-plant-culling.zip.

## world_trace_3: culling gains and denied clock telemetry

world_trace_3 shows 50-78 plants culled/frame. Selected planet windows (+50 to
+90.1s, 683 frames) average 64.89 ms, with plant generation 3.09 ms and entity
painting 4.98 ms. The fast +70.1..+80.1s window averages 45.39 ms, while the
later +85.1..+90.1s window averages 94.99 ms with approximately 24 visible plants.
Entity-delta serialization rises to 14.29 ms/frame in the latter window. Different
play intervals preclude treating this as a controlled speedup measurement.

All direct clock results are 0x1015 (SM module 21, NotAllowed description 8).
Zero frequency outputs therefore represent failed reads, not measured zero Hz.
The user confirmed Switch 2 handheld versus Switch Lite Linux at 2091 MHz with
Perf OC All. CPU clock is a plausible contributor to serial main-thread cost,
but actual Switch 2 rates and any clock changes remain unmeasured.

The read-only power-context revision labels failed rates unavailable and logs
the failure stage/decoded result. It also queries the applet's current performance
configuration ID and cached operation/performance mode. IDs are not frequency
measurements. The watchdog samples the captured real main-thread handle through
InfoType_ThreadTickCount to report scheduled/elapsed time; that ratio is neither
frequency nor whole-system CPU utilization. These diagnostics help distinguish
waiting/preemption from scheduled work. Unsupported/denied APIs report errors.
No clock, voltage, performance profile, affinity or access policy is changed.
Client and renderer cross-build and the watchdog host test pass. New native
queries still require hardware validation. Package: xstarbound-switch2-power-context.zip.


## Compile-time clean comparison and disposable ship storage

STAR_SWITCH2_DIAGNOSTICS defaults OFF for Switch 2. This omits the watchdog
implementation, hot-scope profiling, stage/counter updates and legacy debug-HUD
argument formatting/map locking. The responsive FPS overlay and ordinary error
logs remain. Desktop HUD behavior and explicit diagnostic builds are retained.
The build script now defaults to build-switch2-clean; set
STAR_SWITCH2_DIAGNOSTICS=ON and a separate STAR_BUILD_DIR to collect traces again.

STAR_SWITCH2_RAM_SHIP_STORAGE defaults OFF. An optional comparison build uses
Buffer for disposable loaded/new ship databases only. Persistent ship chunks,
planet files, backups, locking and flush behavior retain their existing paths.
The SD temporary-directory helper is used at creation, not per block read. Seek
operations in the SDK adjust a local cursor; the pread fallback does not issue
four filesystem-service calls just because it makes four C/POSIX calls.

The diagnostic-only and RAM-ship binaries are packaged separately for controlled
comparison. Audit and test procedure: dist-switch2/performance-audit-clean-build.md.
These changes need console FPS/hitch comparison and save/reload validation.


## Buffered wait attribution with RAM ship storage

The wait capture is separately enabled by STAR_SWITCH2_WAIT_PROFILE=ON and keeps
STAR_SWITCH2_DIAGNOSTICS=OFF. RAM-backed disposable ship storage is retained and
now defaults ON in CMake/the build script. Five-second frame summaries, sampled
lock/IO/state scopes and maintenance critical sections are accumulated in fixed
RAM buffers; sdmc:/xstarbound-waits.log is written after normal application exit.
No periodic capture SD flush or watchdog thread runs while playing. A forced
termination can lose the capture. Detailed hot scopes sample one frame in eight;
coarse frame stages run every frame. Selected locks use a sampled failed try-lock
to distinguish observed contention from a long acquisition interval alone.
The capture also separates deliberate pacing, records scene labels, 128 worst
frames >=100ms, and scheduled main-thread CPU time at approximate window bounds.
All durations are wall times, nested values overlap, and sampling can miss events.
Startup/exit logs identify the capture and successful report write. Full capture
procedure and interpretation: dist-switch2/wait-profile-notes.txt. Package:
xstarbound-switch2-wait-profile.zip. Existing clean comparison archives remain
available for rollback and overhead comparison.


## Wait-profile save ordering fix

wait_profile logs confirm capture startup and normal in-game Quit, but stop at
Root teardown without reaching the report writer previously placed after
m_application.reset(). This does not identify why teardown failed to reach the
writer; it does establish that saving there was too late to be reliable.
The report now freezes/writes immediately after the event loop, before application,
audio and Root teardown. Background appenders stop through the capture flag and
short event mutex; the writer does not wait for maintenance threads to finish.
A failed root-path write preserves the frozen capture for a retry at
sdmc:/switch/xstarbound/storage/xstarbound-waits.log. Both failures log errno and
its message. The host regression covers failed open, successful retry before
background teardown, and ignoring background events completing after freeze.
RAM ship storage and the sampling/capture timings are retained. Package:
xstarbound-switch2-wait-profile-save-fix.zip.


## Sustained gameplay: allocation comparison

A first throughput change removes repeated allocation without changing update
rates: empty ItemDescriptor parameters share immutable empty JSON; NetElementTop
reads/writes use a stack Buffer with an empty-owner shared_ptr alias for the
existing IODevice stream; Vulkan Draw holds four texture versions inline; star
rendering streams cached sectors instead of copying a combined PointSet per frame.
The generate() API retains its existing collected-list path and cell/point order.
The alias stream cannot escape its call and dies before its stack Buffer.

The comparison enables STAR_SWITCH2_GAMEPLAY_REGRESSION on the client source.
Before capture/world loading, it checks the old/new network writer bytes/version
steps and receiver, empty parameters isolation, star points/order and cache reuse,
and inline texture-slot lifetimes. The optional renderer test includes these
checks too. Fine attribution adds sampled update/interface/mixer/inventory/armor
and world paint sub-stages. RAM ship storage, the pre-teardown capture save and
old-heavy-diagnostics-off settings are retained. These are allocation reductions,
not a measured promise of playable FPS; actual gains need the next console run.
Package/procedure: dist-switch2/gameplay-allocation-notes.txt and
xstarbound-switch2-gameplay-allocation.zip.

The sustained comparison also samples the global RNG mutex without changing its
algorithm or adding random draws. TTL-cache accesses use this mutex to smear
access timestamps, a path absent from the earlier cache/worker wait capture.
Only one call in 32 on detailed main-thread frames is timed; the report prints
measured sample totals/peaks/contentions and avoids extrapolating sparse waits.
It is a candidate for hidden blocking, not an established culprit before capture.

The optional `xvulkan-test_nro` now includes bounded baseline benchmarks before
and after its existing runtime/world-renderer checks: native single-thread CPU
kernels (three 200ms trials each), an exclusive 8 MiB scratch-file SD test with
verified sequential/random reads, and offscreen Vulkan fill/blending at 1/4/16
draws. GPU timestamps are optional; submit-to-fence wall time is always labeled
separately. Nothing changes clocks, priorities or affinity. No benchmark is run
by the game client. Results go to `sdmc:/xstarbound-benchmark.log`; deployment,
safety bounds, timing limitations and the portable Linux CPU baseline command
are documented in `dist-switch2/benchmark-notes.txt`. The test-only package is
`dist-switch2/xstarbound-switch2-benchmarks.zip`.

`STAR_SWITCH2_IO_PROFILE=ON` adds bounded, all-thread engine File attribution
and enables the SDL wait-profile lifecycle. It records actual read/write bytes,
counts, cursor/work wall times, metadata operations, per-frame main-thread I/O,
logical packed-asset read categories, script-cache reuse/load requests and
committed B-tree block-cache outcomes. Fixed per-thread buffers (about 29 MiB)
are saved to `sdmc:/xstarbound-io.log` before teardown, with a storage fallback.
There are no periodic report writes. Normal stdio/fstream, raw filesystem APIs
and RAM Buffer operations are outside coverage. See `dist-switch2/io-profile-notes.txt`
for capture procedure, boundaries and overflow behavior. Parser:
`scripts/switch2/analyze_io_profile.py`. This build measures candidate causes;
it does not expand caches or change persistent save semantics.

The subsequent sky/CPU build removes per-cell temporary polygon allocations,
streams cached debris points instead of copying a combined string-bearing list,
and avoids star-name list copies on warm frames. It keeps point/draw ordering
and adds independent double-precision debris compatibility checks at startup.
The sky aggregate is split into six stages; world simulation and Vulkan CPU
geometry preparation have finer scopes/work counts. Selected detailed-frame
scopes pair wall and thread-running time with bounded query counts/calibration.
The game client also samples the original newlib heap lock through
`--wrap=__malloc_lock`, forwarding exactly once and guarding early startup before
TLS access. It does not replace the allocator. These samples and paired CPU
times are saved in the existing wait report. The testing NRO adds matching
memory/allocator v1 workloads for the Switch 1 baseline comparison. Details:
`dist-switch2/sky-cpu-profile-notes.txt`; package:
`dist-switch2/xstarbound-switch2-sky-cpu-profile.zip`.

The heap-attribution revision adds inclusive per-stage heap-lock entry counts
to existing sampled scopes, without additional allocation hooks or timers.
It reuses textures already loaded by the debris bounds pass for that frame,
avoiding per-piece path conversion/lookup and temporary SkyOrbiter strings.
The final quad path is shared with ordinary orbiters; stale-name fallback and
per-frame asset lookup/reload behavior are retained. The SDK allocator and
simulation policy remain unchanged. The user's Linux game used jemalloc;
the standalone Linux benchmark used static glibc, so its allocation score must
not be labeled a jemalloc measurement. See `dist-switch2/heap-attribution-notes.txt`
and `dist-switch2/xstarbound-switch2-heap-attribution.zip`.
