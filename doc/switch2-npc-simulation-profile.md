# Server simulation and expensive entity profiling

This adds instrumentation only under STAR_SWITCH2_EXPLORATION_PROFILE. It retains SDK 20261112b, A78C compilation, rpmalloc ownership routing, serialized Pluto bootstrap caching, RAM-backed temporary ship storage, existing controls and render behavior. The comparison NRO uses 1920x1080. No update rate, fidelity policy, priority, affinity, script cadence, time scale, particle/audio queue handling or simulation threading is changed.

## Server capture: xstarbound-simulation.log

Recording starts with the existing light profiler. WorldServerThread registers a bounded slot once per thread/world, retaining its world ID and initial thread ID/priority/allowed core mask/preferred core/query status. Sixteen world slots, 720 five-second windows per slot, 128 named rows per slot and 64 temporary named rows per sampled tick are statically bounded. A slot is not recycled during a process: later worlds beyond the cap are counted as dropped. Restart for a fresh capture if needed. Static storage is about 7 MiB; per-thread temporary storage is additional. There are no per-tick heap allocations or disk writes. Actual external label evaluation can touch existing String UTF8 caches; profiling-off macros remove argument evaluation entirely.

Every completed server loop records cycle wall duration, peak, actual simulation dt advanced, number of advancing ticks, NPC/object/other update counts and fidelity distribution. The observed tick rate divides completed ticks by the span from the first captured tick start to the last captured tick end in that window, including inter-tick gaps. The simulated rate divides advanced game time by that same span. Values below 1 can reflect real slowdown, pausing or configured timescale; use advancing tick counts, target rate and mean simulated dt together. A stopped world/partial first or last window is not five seconds of active simulation. The target Hz comes from ServerGlobalTimestep, not a hardcoded assumption. Boundary cycles belong to their start window. Recording publication after the last tick is outside that window's end point.

All-tick coarse scopes cover work (including update lock acquisition), world lock acquisition, actual WorldServer::update, inbound/outbound packets, message queue/handling, update action, periodic storage sync and requested sleep. These are nested wall times, not exclusive CPU times. In particular a thread descheduled during Lua appears as wall time inside Lua. Thread metadata and waits help interpretation but do not prove scheduler preemption.

Fine scopes sample the first tick of each eight independently for each world: server entity updates by NPC/object/other; full Npc::update, its script-component update, master movement, status and shared work; PathController::findPath and both main/bridge A* explore calls. NPC type names identify script/movement/path costs through an actor context that restores on exit. Path operations outside an NPC context remain in aggregate path totals. These scopes do not identify individual Lua functions, and script-component update includes dispatch and cadence checks, not only Lua execution. The NPC phases do not exhaustively partition Npc::update.

Hot scopes write only thread-local tick statistics. Completed ticks publish under their own world's short mutex, merging phase/name summaries. Worlds do not share a per-entity lock. Dump disables capture, uses registration/publisher barriers, and then writes immutable published data. A server may still be running: incomplete ticks are omitted rather than waited on. The in-flight value is a snapshot at dump time, not a complete count of omitted events. Failed writes can retry to the storage fallback without losing the frozen published data. The recorder does not shut down or pause game threads.

Read the capture with:

```
python scripts/switch2/analyze_simulation_profile.py xstarbound-simulation.log
```

The parser normalizes fine phase totals by detailed ticks and coarse totals by all ticks, preserving calls, peaks, named totals, world identity and capacity/drop counters. Named tables have fixed capacities and truncate labels; report drops before interpreting type attribution as complete. Recorder clocks use the same monotonic source as exploration; startup origins are printed and differ slightly.

## Main-thread capture: xstarbound-exploration.log

Client entity updates now have separate sampled NPC/object/other phases with entity IDs in retained slow calls. This measures WorldClient entity updates, distinct from authoritative server updates.

Base Object::render records its object configuration name and sampled total plus effects (initial renderParticles/renderSounds), image-tag updates, animated drawables, orientation/static drawables and scripted drawables/effects stages. Dynamic animator queue draining sits between tag and drawable stages and is included in the full callback. Derived render overrides can have extra work outside base Object::render. Names describe configured object types, not mod ownership; resolving a type to a mod may require the asset source data. The existing bounded main-thread named table is shared with UI/statistics scopes, and reports dropped samples if full. All operations and their ordering are preserved.

## Validation and useful test route

The deterministic host recorder test verifies exact 50 Hz / 0.5 simulated-rate math, sampled versus coarse normalization, entity/fidelity counters, pause/no-advance accounting, inherited NPC labels, copied label lifetime, simultaneous publishers, bounded worlds/names/windows, dump with a live tick, failed-open retry and foreign-thread dump rejection. AddressSanitizer and compiled-out argument-evaluation tests pass. Existing exploration recorder tests still pass. Native profiling-off syntax checks cover all seven changed probe files. Build/link checks and runtime hardware testing are separate; actual console capture is still needed.

Run the same heavy-NPC world. Spend about 30 seconds at a location where FPS is high but NPC motion looks slow, then 30 seconds in the crowded center, then move away for another 30 seconds. Save and Quit. Return xstarbound-simulation.log, xstarbound-exploration.log, xstarbound-waits.log, xstarbound-io.log and xclient.log. The new simulation capture is written to the SD root, with storage fallback on write failure. The previous working NRO is preserved. This build is a measurement tool, not a performance fix.
