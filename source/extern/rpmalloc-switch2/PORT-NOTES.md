# Isolated Switch 2 rpmalloc candidate

Pinned upstream: rpmalloc 1.4.5, commit `e4393ff85585d91400bcbad2e7266c011075b673`. `UPSTREAM.json` records original source URLs and hashes, before the local changes below. Source/header carry the upstream public-domain notice. This copy does not replace the desktop's bundled allocator.

The 1.4 small-span model is an intentional prototype choice: 64 KiB spans, configured 1 MiB backing batches. It avoids assuming that Horizon permits the 256 MiB reserve/commit model of the other bundled version. This is not a claim that 1.4.5 is the newest upstream release.

Local changes in rpmalloc.c are guarded by `__SWITCH2__`:

- Obtain thread identity from `armGetTls()` rather than Linux's `tpidr_el0` convention.
- Use the explicit 4096-byte SDK page granularity.
- Exclude unavailable mmap headers and disable the default OS map/unmap implementations. The candidate supplies both callbacks.
- Bound spin waits: every 1024 spins calls `svcSleepThread(1)` so a preempted lower-priority lock holder can make progress. Other spins use the ARM yield hint. This is a prototype platform adaptation, not a complete backport of upstream 2.x synchronization changes.
- Three deferred-free sentinel wait loops also use that bounded spin helper on Switch 2, rather than retrying indefinitely without scheduler relief.

Initialize explicitly on main before workers and finalize only after they join. This avoids concurrent lazy initialization. Global overrides/preloading are OFF; argument checks and internal assertions are ON.

`StarSwitch2RpmallocCandidate.cpp` obtains aligned backing blocks through SDK memalign/free (Windows host: aligned_malloc/aligned_free). Only complete original mappings are released. Partial decommit retains the whole backing block. A fixed registry validates releases and tracks a 128 MiB charged-request budget including alignment allowance. This budget is for the standalone test, not a production game memory policy or a measurement of process resident memory.

SDK allocation remains involved in coarse backing requests and test-runtime allocations. It is avoided for most individual rpmalloc object allocations. The metadata mutex runs only on backing operations/snapshots, not every object allocation/free. Registry size, memory policy and initialization/ownership routing must be reconsidered before game integration.

Native console testing is required. Host tests and AddressSanitizer help validate the provider and lifecycle, but ASan cannot identify every invalid access within an allocator's larger backing blocks; full-byte pattern and ownership tests supplement it.
