# Built-in Pluto bootstrap bytecode cache

The Switch 2 build defaults `STAR_SWITCH2_BOOTSTRAP_CACHE=ON`. Desktop defaults OFF; pass the option explicitly to compare either path. No SDK modification or JIT is involved. The game retains rpmalloc and RAM-backed temporary ship storage.

`pluto_assert`, `vector3`, and `scheduler` embed Pluto source in their library entry points. Previously every new VM parsed these literals. Each entry point now compiles its literal on the first use, dumps the compiled chunk into a fixed process-local slot, and subsequently loads that bytecode. Every VM executes the initializer again against its own globals. No Lua state, closure, table, metatable, callback or environment is shared or retained by the cache. The existing asset-script cache is separate and unchanged.

The three 128 KiB slots reserve 384 KiB plus atomic metadata in BSS; the tested bootstrap payloads are 8,705 / 5,790 / 1,928 bytes. The slots belong to immutable embedded source literals in the current executable and disappear when it exits. Asset reload does not alter those literals. There are no cache files or disk writes. This does not cache initialization side effects.

An acquire/release publication state exposes only complete immutable bytecode. Concurrent cold callers compile their own source without waiting for a publisher; only one publishes. A bounded dump writer falls back to the already compiled closure if the payload does not fit, leaving the slot available for retry. Source compile errors propagate normally and never publish an entry. A publication guard releases a failed claim. Bytecode load errors propagate normally rather than executing a potentially partial result.

## Profiling

The light exploration capture adds `bootstrap-compile`, `bootstrap-dump`, `bootstrap-load`, and `bootstrap-execute`, plus hit/miss/published/bypass/byte/error counters. Compilation, loading and execution are timed independently; the existing VM-library timings include these nested stages and must not be added to them. The shared bounded named-scope table records each library's compile/load/execute totals. Profiling compiles out independently of caching.

These captures record main-thread work only and begin after early startup. Background or early VM creation can populate the cache first, so a gameplay capture with hits but no compilation/publication is expected. A miss includes a concurrent cold caller, not necessarily a publication. This build also fixes all-frame NPC/object/other gather counters to count actual render traversal rather than entity removal; comparisons of those counters with older binaries are not like for like.

## Validation and hardware comparison

`source/test/bootstrap_bytecode_test.cpp` exercises the actual modified library entry points: assertion success/failure, vector arithmetic, scheduler execution, independent mutated library/metatable globals, closing one VM while another remains alive, and four simultaneous threads creating fresh VMs. The same tests pass with caching disabled and with deliberately undersized 32-byte slots. Invalid source leaves a slot unpublished and a subsequent valid load succeeds. New helper/library code passes AddressSanitizer; prebuilt host Pluto/Soup libraries are uninstrumented. Capture/parser checks verify hit, publication, overflow and error counts. Native sources also compile with profiling disabled and with both cache and profiling disabled.

Use the packaged matching 720p or 1080p NRO. Repeat the statistics/item-collection and outpost session, then Save and Quit so captures are flushed. Compare VM-library/bootstrap and statistics setup times with `stat_prof_3`, as well as hitch counts and frame times. This change targets repeated VM setup cost; continuous script execution, rendering and initialization side effects remain measurable work. Hardware benefit has not yet been measured.
