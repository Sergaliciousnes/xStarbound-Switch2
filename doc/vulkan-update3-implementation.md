# Vulkan update 3 implementation and validation

Implemented the three cleanup fixes, conservative adjacent draw merging, within-frame descriptor reuse, and bounded identity-tag caching.

## Renderer changes

- Removed the per-buffer exact-size `m_draws.reserve()` so ordinary vector growth is amortized.
- Atlas fallback no longer searches the least-full page again when the main search already tried it. Existing packing order, search budget, and empty-page reclamation remain intact.
- Atlas profiling collection and weak group registration compile out with Tracy disabled.
- Adjacent draws merge only when their vertex ranges are contiguous and their transform, scissor, target, lighting state, and ordered immutable texture sources match. Composite draws are excluded. Geometry and blending order are unchanged; cached topology is captured before frame-final merging.
- Descriptor sets and uniform payloads are shared within a frame using all six ordered texture-version IDs, a composite-target marker, and the exact world-uniform bytes. Transform and scissor are command state and do not prevent descriptor reuse. Interface uniforms are zero because the interface shader does not consume them. Pool reset remains after the previous frame's fence; no descriptor or texture-version cache survives into the next frame.
- Descriptor scratch arrays use the existing 1 MiB retention cap per array. GPU descriptor and uniform capacity follows unique descriptors rather than draw count, retaining the existing shrink policy. The temporary descriptor lookup is destroyed each frame.

Tracy now records stable buffer IDs, set generations, vertex counts, batch counts, and stored texture-limit split counts in cached preparation/replay zones. The current builder's only internal batch split reason is texture-slot exhaustion. Replay annotations report stored boundaries, not new preparation events. Additional plots report prepared batches, merged draws, merge blockers (range/state/texture), and unique/reused descriptors. These allow attribution of post-travel batches to individual buffers without changing texture placement.

## Shared appearance preparation

`ArmorWearer` retains one identity snapshot and tag map for each of base, visual, and networking identity: three bounded entries, replaced on change. This does add a small per-entity memory cost; it is not a zero-memory optimization. No historical identities accumulate.

The cache compares every input used by the 21 substitution tags, including exact directive strings, personality offsets (including signed zero), color, gender, species, and image path. The identity name is intentionally excluded because no tag uses it. Visual/network tags are still obtained after first-pass override resolution; the first pass still starts with empty override maps. Clothing updates, equipment processing, facing logic, scripted overrides, and networking continue running. This caches derived data only, not whole update functions or final armor directives.

## Validation

- Windows RelWithDebInfo client and rendering test targets build successfully. Existing linker/unused-variable warnings remain.
- Renderer and appearance source compile with Tracy disabled as an independent object check.
- Asset-free unit tests cover identity dependency changes and image-path fallback, cached draw merging and replay, transforms, scissor, lighting changes, ordered texture slots, and immutable atlas versions.
- Full rendering/cache integration tests pass with isolated base-game assets and this repository's assets, including asset reload and standalone parallax reuse.
- Five Vulkan smoke modes (world, atlas, textures, geometry, single-texture) passed: 850 presented frames, 373 verified frames, 7,149 GPU pixel checks, zero validation errors. World and geometry were also rerun with descriptor totals enabled: geometry used 170 unique descriptors and reused 1,360 across 170 frames (one set instead of nine per frame); world used 2,384 unique and reused 194. These are diagnostic scenes, not a gameplay benchmark.

Build/test logs and the diff against the pre-task files are under `cmake-build-windows-x64/update3-validation/`. No new route capture has been collected. FPS parity, net CPU savings from the descriptor lookup, appearance-cache hit rates in populated worlds, and Switch behavior remain to be measured. Compare the same ship/world/returned-ship route with prepared versus replayed batches, merged draws, unique descriptor counts, descriptor CPU time, and `Armor identity tag cache miss` zones. Existing pause/connection stalls are outside this patch.
