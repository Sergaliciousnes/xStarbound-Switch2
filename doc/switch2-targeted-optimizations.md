# Switch 2 targeted optimization candidate

Implements the three changes selected after stat_prof: statistics bytecode reuse, object-render cleanup and unchanged metadata/chat save detection. Controls/native chat, rpmalloc routing, RAM-backed ship working storage, tile sheet sharing, eight archive handles, save intervals and player snapshot serialization retain their existing behavior.

## Statistics bytecode

Statistics owns a CompiledScriptCache for its lifetime on Switch 2. Every runStatScript still constructs a new LuaRoot and LuaEngine, attaches that shared serialized-code store, initializes the component, invokes the function, uninitializes and destroys the root. No live tables, closures, VM handles, globals, callbacks or loaded-module state are retained between calls. Initializers and require dependencies still execute in each fresh state with normal once-per-context require bookkeeping.

The cache retains at most 128 entries and 8 MiB combined source/bytecode payload. Fixed entry-count and key-length limits bound bookkeeping; actual allocator capacity/metadata is additional to the payload accounting. Source bytes are retained so equality checks are collision-free; source changes cause recompilation. Keys include script path and safeScripts mode. Oversize entries bypass storage. LRU eviction releases the cache's ownership only; active contexts have already loaded independent chunks.

Statistics registers one reload listener that clears its cache. The normal per-root shutdown clears only its private compiled/context cache, preserving the statistics-owned store. Explicit root script load/unload/clear operations invalidate shared entries as appropriate. Other LuaRoots do not use the shared cache by default; desktop statistics behavior is unchanged.

Compilation occurs outside the shared store's mutex. Entries are published complete; failures do not cache a result. A generation check prevents publication after invalidation during compilation. The existing per-root recursive mutex and Lua thread ownership remain.

New exploration phases: stats-bytecode-lookup (includes a miss's compilation), stats-compile, stats-bytecode-load. New counters: stats-bytecode-hit, stats-bytecode-miss and stats-bytecode-evict. Existing stats-init includes module load and top-level execution; nested totals overlap. VM setup is still present and is not prewarmed or pooled in this patch.

## Object rendering

AnimatedPartSet and NetworkedAnimator expose a direct hasParts query. Object::render uses it instead of allocating a list of every part name to test whether there are any parts. The predicate is the same underlying container emptiness, so no cached flag can become stale after configuration changes.

Scripted animator drawable pairs are accessed by const reference rather than copied before adding their drawable to the render callback. The callback still obtains its required owned copy; persistent animator state is not moved from. Chat save serialization also accesses stored log messages by const reference.

No NPC/object culling, script throttling, simulation changes or audio/particle suppression is introduced.

## Unchanged saves

Player metadata and non-scripted chat messages keep a last-successfully-persisted content snapshot on Switch 2. Each save constructs its current content and compares it with that snapshot. If content matches and the destination is still a file, the write is skipped. First save, changed content, cleared content and a missing destination all write normally.

The snapshot is updated only after the writer returns successfully; a failed write leaves the previous snapshot in place for retry. Metadata still uses its staged write, sync, close and rename protocol for actual changes. Chat uses its existing writer and scripted-chat behavior is unchanged. Player state saves and their dirty detection are unchanged. No asynchronous persistence is added.

This removes redundant filesystem work, not all serialization: chat still builds its serializable array for comparison, and metadata still builds the player-order array. The destination existence check adds one filesystem check on equal-content saves so deleting a file cannot permanently suppress recreation.

New counters: metadata-written, metadata-skipped, chat-written, chat-skipped. Writes outside an active main-thread profile frame are not counted.

## Validation

The host cache test compiles and loads real bytecode with the bundled Pluto library. It checks independent VM globals/config values, module require once per VM, changed-source recompilation, reload/clear, retained live-code lifetime, syntax-error retry, entry/payload limits, LRU, explicit unload and invalidation during compile. The test uses the shared cache implementation compiled into the game, but not the full game asset/reload integration, which needs console testing.

A temporary-file snapshot test checks first/changed/unchanged saves, failed-writer acknowledgement/retry, missing-file recreation and persistence of cleared content. Cache and snapshot helpers also pass AddressSanitizer; linked prebuilt Pluto/Soup libraries are uninstrumented and require matching disabled STL container annotations. Normal memory red-zone checks remain enabled in the test code.

Recorder tests retain exact phase/counter accounting, bounded named totals, overflow reporting and disabled argument exclusion. Native syntax checks cover Statistics.cpp, LuaRoot.cpp, PlayerStorage.cpp and Chat.cpp with exploration profiling disabled. Native 720p and 1080p builds, symbol checks and package validation are required before delivery.

## Console comparison

Use xclient-targeted-opt-1080p.nro to match stat_prof. Keep the same mods, character, route, zoom and settings. Explore and collect items to trigger the item statistics events, then visit the crowded outpost sections. Observe object animation, particles/sounds and any new errors. Wait through at least two autosave intervals; send chat, then allow another save and restart to confirm persistence. Test clearing chat history if you normally use that feature.

Use in-game Quit, fully close the host title and retrieve xstarbound-exploration.log, xstarbound-waits.log, xstarbound-io.log, xstarbound-rpmalloc.log and xclient.log. There is no additional log file. Compare repeated stats-init/compile costs, cache hit rate, event hitch frames, metadata/chat skipped counts, player-save timings and outpost gathering. First-use misses are expected.

Previous comparison and standard binaries are preserved. Performance benefit and full game integration need hardware verification; host tests do not establish an FPS gain.
