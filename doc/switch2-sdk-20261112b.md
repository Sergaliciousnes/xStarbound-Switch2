# SDK 20261112b verification and allocator comparison

## Installation and build findings

The installed version is `ounce-sdk 20261112b`, libounce `a14c2a51`, OunceVK `7c18856`, OunceHEN `a09a789`. All 313 regular files in `sdk/ounce-sdk.tar.xz` match their installed files byte for byte. Archive SHA-256: `8935a0fc554e30b078eba24766bfe9739e9cef774fe0f7aa1865cceaf032d888`. No reinstall or SDK modification was needed. The user confirmed the console and PC both have the matching update.

`libounce.a` is now a linker script: `EXTERN(_mallocInitialize) INPUT(-lounce_malloc -lounce_core)`. `libnx.a` forwards to `-lounce`. The malloc archive exports malloc/calloc/realloc/free/memalign, the newlib reentrant entry points and real mi_* implementation symbols. The linked benchmark's mi_version returns 30504; the runtime log records the API result directly without assuming a version-number encoding. `-lounce_core` uses newlib's allocator. Thus the previous missing-allocator issue is resolved.

The official `switch2vars.sh` plus `aarch64-none-elf-cmake` successfully configures, compiles and links an independent heap/mimalloc probe. This Windows devkitPro's devkita64.sh normalizes DEVKITPRO to `/opt/devkitpro`; that is its MSYS installation alias, and the probe succeeds. The project's established toolchain bridge is retained. It now reads `SWITCH2_CPU` from the SDK rather than retaining A57 tuning; older SDKs without that file keep the prior fallback. Existing CMake caches can retain old CPU flags, so this verification used a fresh isolated build directory.

The complete 1080p client, allocator comparison, newlib comparison, allocator routing regression and Vulkan/CPU/SD benchmark NROs build successfully in `build-switch2-sdk-20261112b`. The game audit verifies all 588 compilation commands use `-mcpu=cortex-a78c+crypto`, with no A57 tuning. Linked symbols/maps confirm the new SDK, its Switch 2 marker symbol, SDK mimalloc, the project's rpmalloc routes and serialized bootstrap cache. No original libnx archive members were linked. Heavy allocation/startup diagnostics remain off; light wait/I/O/exploration profiles remain on.

## Allocation ownership and compatibility

The game remains configured with `STAR_SWITCH2_GAME_RPMALLOC=ON`. C++/Star/Lua allocations still use the pinned rpmalloc backend. SDK C malloc and the aligned backing allocations used by rpmalloc now come from SDK mimalloc. Ownership-routed free/realloc wrappers still send each pointer to the allocator that owns it. The application does not pass rpmalloc pointers blindly to SDK free, replace the SDK heap initializer, or use newlib mallinfo/sbrk to describe mimalloc's heap. Its 4 GiB rpmalloc charged-backing cap is unchanged and is distinct from process memory and the SDK allocator's total live footprint.

The new routing-test NRO exercises early SDK/C++ allocations, mixed-allocator free/realloc, alignment, concurrent mapping/provenance, cross-thread frees after owner exit, TLS cleanup, failures/recovery and late static destruction. It compiled and linked against SDK mimalloc; those on-console assertions have not yet been run. The shared comparison source passes the existing host correctness/concurrency/cap/failure suite. Host CRT results do not benchmark the ARM SDK mimalloc implementation. Parser regressions cover complete pairs, historical captures, missing/duplicate/failed reports, SDK/CPU mismatch, malformed trials and baseline drift.

Build/link validation supports using this as a compatibility-test build, not a blanket claim of runtime safety. The prior working NROs are preserved byte for byte. Keep rpmalloc as the application allocator until actual console comparisons justify changing it. Do not install the desktop STAR_USE_MIMALLOC backend or enable multiple application allocators for this test.

## Three-allocator comparison without mixing heaps

Two independently launched NROs use identical bounded workloads and A78C flags:

1. `xallocator-mimalloc-rpmalloc.nro`: default SDK mimalloc before, explicit rpmalloc, default mimalloc after. Writes `sdmc:/xstarbound-allocator-compare-mimalloc.log`.
2. `xallocator-newlib-rpmalloc.nro`: genuine SDK `-lounce_core` newlib before, explicit rpmalloc, newlib after. Writes `sdmc:/xstarbound-allocator-compare-newlib.log`.

The newlib target lives in its own CMake directory, where only its standard runtime libraries are changed. The link audit verifies no mi_* code and no -lnx linkage in that target, while the mimalloc target contains real mi_* symbols. They do not interpose two competing process C allocators inside one NRO. rpmalloc backing comes from each respective baseline, so its backing conditions differ across the two reports and are recorded as such.

Each NRO retains 1/2/4-thread allocate/free and mixed-realloc workloads, three trials, before/after baseline checks, verified data patterns, cross-thread handoff, owner-exit and remote-free tests. rpmalloc's synthetic backing cap is 128 MiB; forced failures are confined to its test backend, not induced by exhausting console memory. No clocks, priorities, affinity, saves or assets are changed. The only output is the report. Allocator tests usually return to hbmenu after tens of seconds; check `finished status=0` and correctness lines.

Run both in the same handheld/docked mode and similar thermal conditions. Prefer an idle console and repeat in reverse order if results disagree. Use:

```
python scripts/switch2/analyze_allocator_compare.py path/to/xstarbound-allocator-compare-mimalloc.log path/to/xstarbound-allocator-compare-newlib.log
```

The analyzer requires a complete verified pair with matching SDK and CPU metadata, and reports baseline drift above 15%. Synthetic operations/second do not translate directly to FPS or establish one allocator as universally best. Small threaded latency and throughput, realloc and remote free behavior, memory retention, correctness and gameplay frame times all matter.

## Other test binaries and logs

- `xallocator-routing-sdk-20261112b.nro`: game mixed-ownership regression; returns `sdmc:/xstarbound-rpmalloc-routing.log`. Check routing-tests=verified and late-static-destructor=verified.
- `xvulkan-test-sdk-20261112b.nro`: existing bounded CPU/memory/SD scratch-file/offscreen Vulkan suite rebuilt against the SDK. Writes `sdmc:/xstarbound-benchmark.log`. It uses its existing scratch-file safety logic, not game saves or assets.
- `xclient-sdk-20261112b-1080p.nro`: comparison game build with rpmalloc and bootstrap caching retained. Use existing game assets/configuration/mods/storage. Save and Quit to flush the performance captures. It is not substituted for the published standard NRO.
- The matching loader's `/ouncehen/nro-cleanup.txt` is useful for checking post-exit threads/memory. Copy it after each relevant run if the loader replaces it. It is an external loader report, not proof of allocator correctness by itself.

## Expected performance effects and limits

- SDK mimalloc should remove the old globally locked newlib allocation path for SDK/C-library allocations and rpmalloc backing. The game's high-frequency C++/Star/Lua path already uses rpmalloc, so a repeat of the original huge allocator improvement is not implied. Thread-local arenas/retained pages also change memory accounting; watch gameplay memory and error counters as well as throughput.
- A78C compilation targets the correct CPU and can improve scheduling/code generation. It changes no hardware clock. The size of any improvement is workload-dependent and not yet measured.
- The supplied OunceVK release notes describe less shader-cache write contention, deduplicated concurrent shader compilation and faster large-shader compilation. These are credible startup/first-use pipeline hitch improvements, but not a demonstrated general GPU/FPS gain in this game. Repeated warm rendering and CPU object drawable generation are distinct costs.
- File-session ownership and cleanup fixes address the earlier post-exit FTP locked-log behavior and leaked resources after failure. They are chiefly reliability/lifecycle changes, not evidence of faster steady-state asset reads. Program markers address launcher identification. No project plugin integration is needed.
- OunceVK's heap-budget API now sees mimalloc's heap accounting. SDK heap free bytes are a lower bound and do not count all reusable space inside partially used allocator pages; rpmalloc backing may remain reserved while internally reusable. A changed reported budget alone does not prove increased live memory or a leak.
- This update does not by itself eliminate the previously measured object rendering, cold GUI/image processing, actual script initializer work or main-thread player-save pauses. Compare matched gameplay routes before attributing FPS changes to one SDK component.

No console measurements for these new binaries are available yet. Return the two dedicated allocator reports and the routing report first; then compare gameplay and the general benchmark if desired. All native audits and source/build hashes are packaged in the manifest and the local audit directory.
