# Vulkan post-travel batch fragmentation investigation

Investigation of the updated Switch ship/world/ship recordings and the current source.
No production renderer or allocator changes were made for this investigation.

## Confirmed allocator behavior

`TextureAtlasSet::sortAtlases()` sorts by descending used-cell count (fullest
first). `addTexture()` starts at `size - min(size, textureFitTries)` and scans
toward the end, so it tries the least-full pages. Its adjacent implementation
comment claims the reverse. The public `textureFitTries` comment does describe
least-full behavior, so the code and documentation disagree about the intended
policy; the fragmentation consequence is independently reproducible.

After a group has grown during travel, expiring textures leave sparse/empty
pages. `freeTexture()` clears occupied cells but does not remove empty pages.
Vulkan never runs `compressionPass()`. Successive new textures are therefore
spread among sparse pages as each successful insertion re-sorts the pages.
This is harmful to four-texture batching even when all the new textures could
fit together on one existing page.

OpenGL shares this placement algorithm, but calls `compressionPass(1)` every
frame, which also removes empty pages. Its Large pages are normally 4096 square,
versus Vulkan's 1024 square. Cached OpenGL geometry locks referenced placements,
so its compaction is not universal. These are mitigating differences, not proof
that OpenGL never suffers fragmentation.

## Isolated reproduction using the actual Vulkan CPU renderer

Sources, response files, private executables, and results are in
`cmake-build-windows-x64/batch-investigation/`. The probe is asset-free and does
not create a Vulkan device. It compiles a private copy of the current renderer
against existing libraries; it does not replace the game executable.

For both Small (256) and Large (1024) groups:

1. Fresh case: create 32 small textures, then draw them repeatedly in fixed order.
2. Travel case: first allocate 36 textures occupying four placements per page,
   creating nine pages. Release either all of those textures or all except one
   per page. Then create and draw the exact same 32 small textures as above.
3. Build and replay a cached render buffer with those same primitives.
4. Repeat in a private variant whose only placement-policy change is scanning
   existing pages from fullest to least-full, allowing the full page list.

All cases submit 512 quads / 3,072 vertices with identical draw order and state.
All textures are created BEFORE drawing, so no intra-frame texture mutation or
atlas-version churn is needed to cause this result.

| State | Current pages used / batches | Fullest-first prototype pages used / batches |
|---|---:|---:|
| Fresh | 1 / 1 | 1 / 1 |
| After travel, old textures all released | 7 / 65 | 1 / 1 |
| After travel, one old texture retained per page | 7 / 65 | 1 / 1 |

The numbers are identical for immediate draws, first cached draw, and cached
replay, and for both tested atlas sizes. These are deterministic CPU draw-count
results, not FPS or GPU measurements. The all-pages prototype is a causal
experiment, not a production-ready placement-search policy.

## How this becomes persistent rendering overhead

The Vulkan batcher ends a batch when a fifth distinct texture/page is needed.
Repeated references to the same page consume one slot, so scattering textures
across pages increases batches without increasing geometry.

Prepared render buffers cache those split points. On subsequent draws, they
refresh current texture versions but replay the stored batch topology. They
invalidate on `set()` or a multitexturing-setting change, not because a better
packing arrangement might exist. Rebuilding geometry alone cannot fix scattered
placements. Moving placements would require UV/cache invalidation, so enabling
Vulkan compression wholesale is not a safe fix.

Every extra draw then causes texture-handle resolution, draw-packet work,
descriptor/uniform preparation, and command recording. Vertex bytes depend on
vertex count, not batch count; the growth in the cached-reuse CPU zone includes
per-batch work and must not all be described as geometry memcpy cost.

## Evidence from the actual updated Switch recording

Source: `cmake-build-windows-x64/switch-updated-comparison/events.csv` and
`frames.csv`, exported from `xsb updated switch vulkan ship to world back.tracy`.
Windows below match the user's earlier comparison. Costs are CPU wall-time
zone totals divided by rendered-frame count; plot values are medians.

| Metric | Initial ship, 82-98s | Returned ship, 130-139s |
|---|---:|---:|
| Batches/frame | 296 | 1,482 |
| Vertices/frame | 58,446 | 66,174 |
| Cached-buffer reuse calls/frame | 98.80 | 121.00 |
| Cached-buffer rebuild calls/frame | 0.07 | 0.00 |
| Immediate preparation calls/frame | 30.19 | 28.00 |
| Cached-buffer reuse ms/frame | 0.921 | 2.346 |
| Immediate preparation ms/frame | 1.100 | 1.076 |
| Descriptor/uniform preparation ms/frame | 0.412 | 2.331 |
| Command recording ms/frame | 0.113 | 0.625 |
| Vertex upload ms/frame | 1.266 | 1.406 |
| Frame-fence wait ms/frame | 0.014 | 0.014 |
| Image acquisition ms/frame | 0.305 | 0.331 |
| Queue present ms/frame | 1.224 | 0.220 |
| GPU texture creation + destruction ms/frame | 0.068 | 0.031 |
| Atlas snapshots/frame | 0 | 0 |
| Lighting images reused / newly allocated per frame | 2 / 0 | 2 / 0 |
| Texture upload bytes/frame | 15,048 | 15,048 |
| GPU texture cache entries | 51 | 97 |
| Spare atlas payload bytes | 67,108,864 | 67,108,864 |

Thus batches grow about 5x, cached-buffer call count only 1.22x, and vertices
1.13x. The returned section has no cached-buffer rebuild events. Descriptor
preparation and recording scale approximately with batches. The four disjoint
zones for cached reuse, descriptors, recording, and vertex upload grow by about
4.00 ms/frame combined. Do not add their parent zones again. These figures
compare initial versus returned Vulkan, not Vulkan versus OpenGL.

Steady-state atlas mutation and lighting allocation churn are not supported as
the reason for the persistent returned-ship overhead. Fence waiting is also not
the increased component in these windows. Required-page identity/group and
batch-split reasons are not recorded, so the existing capture cannot establish
which atlas group causes each extra batch or what fraction of the exact
296-to-1,482 change this defect explains. GPU execution and memory/swap behavior
are not directly measured. The reproduction proves a mechanism matching the
observed history dependence, not exact route causality or a promised FPS gain.

## Recommended next change

1. Correct new-placement policy to prefer fuller pages that can plausibly fit.
   Skip pages with insufficient free cells before expensive rectangle searches;
   keep a sensible search budget/fallback. Simply reversing the first three
   candidates without accounting for full pages could create excess new pages.
2. Reclaim completely empty Vulkan atlas pages without moving live placements.
   Preserve immutable snapshots used by queued draws. This can reduce retained
   memory and removes the empty-page reservoir that triggers spreading.
3. Keep page sizes, four-texture batching, and cached UVs unchanged initially.
   Do not turn on relocation without a separate cache invalidation design.
4. Add split-reason and per-group active-page/fill counters to the next capture,
   separating cached replay from immediate draws. Compare the same route after
   restarting: correcting insertion cannot repack existing live placements.

Larger atlases or more retained geometry are not prerequisites for addressing
this defect. A production fix should be checked against the reproduction,
existing atlas-version pixel tests, and the actual Switch route.

## Implementation and Resolution

The recommended changes have been applied to production code and built:

1. **Placement policy (`StarTextureAtlas.hpp`):**
   - Candidate pre-filtering: pages where `totalCells - usedCellCount < requiredCells` are skipped in $O(1)$ without invoking 2D packing checks or consuming search budget.
   - Empty-page fast path: an atlas with `usedCellCount == 0` places immediately at $(0, 0)$ in $O(1)$.
   - Fullest-first candidate scan: candidate pages are evaluated from index 0 (fullest) upwards.
   - Search budget: `m_textureFitTries` bounds expensive 2D packing scans on candidate pages that suffer from fragmentation.
   - Fallback: if all candidate searches fail due to fragmentation, the emptiest atlas (`m_atlases.last()`) is checked before creating a new page.
   - `compressionPass()`: updated to use the same candidate pre-filtering and fullest-first candidate search.

2. **Empty-page reclamation (`StarTextureAtlas.hpp`):**
   - In `freeTexture()`, completely empty trailing pages (`usedCellCount == 0`) are pruned with `m_atlases.removeLast()`.
   - Retains at least 1 page (`m_atlases.size() > 1`) to eliminate allocation thrashing when total textures drop to zero.
   - Live placements are never relocated, preserving stability for cached buffer UV coordinates.
   - Queued draws in Vulkan retain their `VulkanAtlasPage::snapshot`, keeping tiles and `atlasOwner` alive until GPU command retirement.

3. **Performance optimizations (`StarRenderer_vulkan.cpp`):**
   - Reciprocal texture dimensions (`invTextureSize = 1.0f / size`) are precomputed once per primitive in `slot()`, converting 12 floating-point divisions per quad into fast scalar multiplications in `vertex()`.
   - Redundant RTTI `dynamic_cast<VulkanTextureBase const*>` calls were eliminated.
   - Draw vector capacity reservation (`m_draws.reserve`) was added in `renderBuffer()`.

4. **Diagnostic instrumentation (`StarRenderProfiling.hpp`, `StarVulkanDiagnostic.cpp`, `StarRenderer_vulkan.cpp`):**
   - Added Tracy counters plotted per frame:
     - `Vulkan active atlas pages`
     - `Vulkan active atlas textures`
     - `Vulkan average atlas fill %`
     - `Vulkan batch texture-limit splits/frame`
     - `Vulkan immediate batches/frame`
     - `Vulkan cached replay batches/frame`

5. **Build status:**
   - Client executable built cleanly at `cmake-build-windows-x64/source/client/RelWithDebInfo/xclient.exe` (0 warnings, 0 errors).
   - Probe verified at `1 page / 1 batch / 3,072 vertices` across fresh, travel empty, and travel sparse retain scenarios for both Small (256) and Large (1024) pages.
