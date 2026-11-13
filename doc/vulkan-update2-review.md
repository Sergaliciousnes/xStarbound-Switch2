# Update2 renderer review and next performance targets

## Scope and verification

Reviewed the current atlas allocator, Vulkan preparation/counters, and the
Switch update2 exported events/frames (74-90s, 104-112s, 132-149s). Production
source was not changed during this review. Standalone review sources and output
are in `cmake-build-windows-x64/update2-review/`.

RelWithDebInfo xclient and rendering_tests builds succeeded. Five Vulkan smoke
modes (world, atlas, textures, geometry, single-texture) passed: 850 presented
frames and 7,149 pixel checks in total, with zero validation errors and
synchronization validation requested via VK_LAYER_VALIDATE_SYNC=1. This is
Windows/RTX 4070 Ti SUPER coverage, not a new Switch or OpenGL runtime test.
The rendering_tests binary was built; the asset-dependent integration runner
was not executed in this review.

Additional isolated allocator checks passed: full-page skipping with a one-miss
budget, fallback from a fragmented page to a viable sparse page, reclaiming an
empty page without changing a live placement, keeping/reusing the last page,
and preserving locked placements during compression. The earlier actual Vulkan
CPU-renderer reproduction now gives one page/one batch for all fresh, emptied,
and sparse travel histories, both immediate and cached, at both 256 and 1024
page sizes (3,072 identical vertices).

## Review findings

The principal packing/reclamation changes are sound in the exercised cases.
No live placements move on free. Queued Vulkan sources own immutable tiles, so
releasing an empty allocator page does not invalidate queued pixel snapshots.
OpenGL buffer-held textures keep placements live and lock them for compression.

Three implementation refinements are advisable:

1. `StarRenderer_vulkan.cpp:327` reserves exactly size + next-buffer batch count
   on every cached-buffer replay. This can defeat vector's geometric growth:
   as a new high-water frame grows, each buffer can cause another allocation
   and move all previous draws. It is harmless once capacity covers the frame,
   but the claim that it eliminates reallocations is too strong. Prefer normal
   amortized push_back growth or a capacity check with bounded geometric growth.
2. `StarTextureAtlas.hpp:198` says the fallback is tried only if not already
   tried, but does not record that fact. If the main loop reaches the least-full
   page and it fails, fallback repeats the same rectangle search. This is a
   bounded wasted search, not a placement correctness defect. Track whether the
   fallback candidate was already attempted. The public budget documentation
   should also mention the extra fallback attempt.
3. `VulkanRenderer::finishFrame()` performs the weak-group walk and fill scans
   even when TRACY_ENABLE is disabled; plot macros alone become no-ops. Guard
   telemetry collection and group registration for profiling builds if these
   figures are not used elsewhere. Current fill is a group-weighted average,
   not aggregate occupied-cell percentage. Active-page count means allocated
   pages in live groups, including retained empty pages, not visible pages.

Terminology corrections: free-cell rejection and empty-page coordinate
selection are O(1), but whole insertion is not (candidate scan, cell marking,
pixel copies, sorting remain). Reciprocals are calculated once per primitive
in slot(), not once per page; textured quads replace twelve source-level vertex
divisions with two reciprocals and multiplications. Compiler transformations
and hardware determine the actual speedup. Redundant RTTI removal is valid.
The three new batch counters have the stated semantics: replay does not recount
historical texture-limit splits; first-time cached preparation and the composite
draw are not cached-replay or immediate batches. Separate medians need not sum.

## Highest-priority common target: redundant pause synchronization

ClientApplication::updateRunning calls UniverseServer::setPause on every local
client update inside the broad Steam/Discord connection-handling zone.
setPause takes m_clientsLock before checking whether the atomic pause flag is
already equal to the request. Therefore even setPause(false) while unpaused
waits behind world-management operations holding the same lock.

The first large update2 stall starts at 95.524372047s and lasts 3.212339113s.
Overlapping server events include:

- warpPlayers: 95.516716370s, 2.655664217s.
- shutdownInactiveWorlds: 98.172415119s, 0.564278437s.
- packetsReceived on another thread: 95.524042620s, 3.212677446s.
- Several synchronous generateSectorToLevel calls on the universe thread,
  including 712ms, 588ms, and 421ms events within the warp.

warpPlayers and shutdownInactiveWorlds acquire m_clientsLock and call operations
that acquire world locks, generate/spawn world content, or run shutdown work.
packetsReceived also starts by acquiring m_clientsLock. The aligned delays plus
the source establish a strong lock-contention explanation. The capture does not
separately time the pause lock; the broad client zone also contains a P2P-service
mutex, so attribution of its entire duration remains an inference.

Next small change: under the current single-caller ownership, return before
locking specifically when the requested state is false and the atomic state is
already false. Preserve the locked path and recheck for actual transitions.
Do not unconditionally fast-return for true == true: multiplayer client count
can require forcing unpause. Add separate zones for P2P acceptance, pause lock
acquisition, and pause transition. Verify pause/resume and multiplayer joins.

This avoids unnecessarily freezing the client behind server travel work; it does
not eliminate world generation or guarantee that the destination is ready sooner.
Reducing lock scope in world management is a larger follow-up requiring lifetime
and ownership review, rather than simply unlocking around world calls.

## Busy-world simulation target: appearance and network preparation

At 104-112s (78 rendered frames), client entity updates cost 35.622ms/render over
three updates/render, about 11.87ms/update. Server entity update spans total
80.121ms/render over about 3.14 server ticks/render, about 25.54ms/tick. Server
Lua invoke spans total about 45.05ms/render on that thread. These are inclusive
wall-time zones; threads and nested zones must not be summed.

A concrete narrower candidate is ArmorWearer:

- setupHumanoidClothingDrawables: 4.658ms/render on the client (130 calls/render),
  and 6.309ms/render on the active world-server thread (160 calls/render).
- setUpArmourItemNetworking: 2.146ms/render on the server (1,053 calls/render),
  included in broader packet/network work rather than an additional total cost.

The clothing method builds identity-tag maps (including formatted offsets and
colors) before determining all dirty conditions, and runs both cosmetic-resolution
passes even when most stack writes are skipped. Network storage regenerates
three identity-tag maps and processes eight armor descriptors per invocation.
Investigate caching derived identity/directive data and versioning those results,
with invalidation for equipment/instance parameters, facing, base/visual/network
identity, species/gender, scripted overrides, extended cosmetics, and reloads.
A simplistic early return based only on existing equipment dirty flags is unsafe.

EntityMap's traversal/sort/spatial-maintenance overhead is not the primary client
lead: total entity-update zone is 35.622ms/render versus 34.962ms/render inside
individual entity callbacks, leaving roughly 0.66ms/render for surrounding work.
Keep simulation semantics and update rate intact; skipping arbitrary entity ticks
would change gameplay. More detailed Lua callback/entity-type breakdown is needed
before choosing broader script changes. The existing annotations.tsv export only
contains narrow earlier time windows and cannot supply that breakdown directly.

## Remaining Vulkan targets

Initial versus returned ship in update2:

| Metric | Initial | Returned |
|---|---:|---:|
| Median vertices | 57,906 | 57,732 |
| Cached-buffer calls/render | 95.21 | 97.00 |
| Median cached replay batches | 163 | 453 |
| Median immediate batches | 62 | 57 |
| Allocated atlas pages in live groups | 33 | 44 |
| Atlas textures | 1,441 | 1,091 |
| Group-average fill percentage | 55 | 36 |

This is consistent with residual sparse live placements and more splits inside
cached buffers, not a large increase in geometry or buffer calls. The allocator
fix prevents the earlier spreading pattern and reclaims EMPTY pages; it does not
consolidate sparsely occupied pages or repack existing cached texture placements.
Per-group/per-buffer batch distributions are still needed to identify the exact
resource group and texture sequences responsible.

Low-retention next experiments: fixed-capacity four-texture storage in draw
packets; deduplicate identical material/state descriptors within a frame; and
merge adjacent draws only when their texture-slot mapping, effect state, transform,
scissor, target and vertex ranges are compatible. Preserve painter order. Measure
mergeable batches before committing to a broader batching rewrite.

Returned Vulkan costs include 1.232ms/render cached replay, 1.201ms/render vertex
upload, 0.765ms/render descriptors and 0.199ms/render recording. Persistent GPU
geometry remains a larger later option, with memory tradeoffs. None of these
numbers alone establishes GPU time or predicts parity with the separate OpenGL
recording. Prioritize pause no-op contention for stalls, appearance/network data
reuse for simulation, and targeted per-batch work reduction for rendering.
