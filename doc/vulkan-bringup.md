# Vulkan rendering bring-up

The Vulkan backend implements the existing `Renderer` interface, including
standalone textures, four-texture batching, texture groups/atlases, built-in
interface/world effects, lighting, and world/UI composition. Separate modes run
asset-free diagnostics, menu previews, or experimental gameplay. Atlas
compression and custom shader effects remain unsupported. OpenGL is the default
when compiled in; Vulkan-only builds start Vulkan gameplay by default.

## Build on Windows

Install a Vulkan SDK, then enable the optional backend in the existing build:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' -S . -B cmake-build-windows-x64 -DSTAR_ENABLE_VULKAN=ON
& 'C:/Program Files/CMake/bin/cmake.exe' --build --preset windows-x64-release --target xclient
```

The option defaults to OFF. With vcpkg, enabling it also requests SDL2's `vulkan`
feature. An older SDL2 DLL built without this feature cannot create a Vulkan
window, even when the GPU and Vulkan SDK support Vulkan.

Vulkan function pointers are loaded through SDL rather than linked into the
executable's imports. OpenGL startup does not require loading a Vulkan driver.
The build also requires `glslangValidator` (included in the Vulkan SDK) and
compiles the flat vertex/fragment shaders to embedded SPIR-V. No runtime shader
compiler or extra shader files need to be deployed.

## Run

From the executable directory:

```powershell
.\xclient.exe --renderer=vulkan
```

Controls: **V** toggles VSync, **F11** toggles fullscreen desktop, **Escape** quits.
Also test manual resizing and minimize/restore. The scene should retain these
features, scaled to the window:

- Top row: a sharp four-color image, a smoothly filtered copy, a repeated
  pattern, and a clamped pattern with extended UVs.
- Middle row: four transparency levels over gray, a tinted image, RGB/BGR/BGRA
  conversion panels, and a yellow temporary-texture panel.
- Two small red/green markers verify cached textured-buffer snapshots.
- Bottom row: twelve colored tiles cross four-texture batch boundaries.
- A clipped magenta strip at the bottom checks textured scissor behavior.

Add `--vulkan-geometry` for the earlier untextured shapes scene. That scene now
uses the same sampled-image pipeline with a white fallback texture.

`vulkan-diagnostic.log` is written beside the
executable, independently of `xsbinit.config` and the game's storage directory.
Validation is enabled automatically when the Khronos validation layer is
installed; warnings/errors are logged. Check the log's validation-enabled line
before treating a run as validation coverage.

For a bounded automatic test:

```powershell
.\xclient.exe --renderer=vulkan --vulkan-smoke-test
```

It exercises resize, VSync off/on, fullscreen/windowed, minimize/restore, then
destroys the Vulkan resources and exits. After each swapchain recreation it
reads back the GPU image and checks 39 pixel samples covering orientation,
nearest/linear filtering and edge clamping, negative/wrapped UVs, alpha and tint,
RGB/BGR/BGRA conversion with odd-width inputs, source-image copies, released
texture handles, cached buffers, all four texture slots and batch boundaries,
and textured clipping. A temporary texture is created and released each frame;
the test rejects growth beyond the scene's 15 live GPU textures. It saves the
latest readback as `vulkan-textures.png` beside the executable.
With `--vulkan-geometry`, the original 13 geometry pixel checks run instead and
capture `vulkan-geometry.png`. Pixel verification
requires a transfer-readable RGBA8/BGRA8 UNORM surface; unsupported surfaces
report an error in this test mode rather than silently skipping verification.
Interactive rendering does not require readback support. The log records
presented frames, verified frames, and validation errors, including teardown.
Exit code 0 indicates success; failures
and validation errors return 1. Closing a bounded test early also returns 1.
The smoke test runs 180 event-loop iterations, including ten minimized
iterations; it normally presents 170 frames. Window-system behavior may change
the exact count. A shorter check is available with
`--vulkan-frames=30` (1 to 100000 event-loop iterations, with a 30-second test
deadline checked between frames).
Add `--vulkan-verify` to enable GPU checks and PNG capture without the automatic
window transitions, for example `--renderer=vulkan --vulkan-frames=30 --vulkan-verify`.
Add `--vulkan-single-texture` to verify the fallback that limits each batch to
one texture; its rendered pixels must match the four-texture path.

Launch without the Vulkan option, or with `--renderer=opengl`, for normal gameplay.
A build with Vulkan disabled reports that the experimental option is unavailable.

## Atlas diagnostic

```powershell
.\xclient.exe --renderer=vulkan --vulkan-atlas --vulkan-smoke-test
```

This runs 21 GPU pixel checks after swapchain recreation, covering multiple
atlas pages, nonzero cell offsets, nearest/linear filtering, replicated borders,
oversized-image fallback, cached atlas UVs, freed/reused regions after queued
draws, and pending draws whose texture group was destroyed. The test forces
page overflow with 270 small images and allocates/frees textures every frame.
The captured output is `vulkan-atlas.png`. `--vulkan-atlas` without the smoke
flag shows the interactive atlas scene.

## Vulkan menus

From an installation containing the normal base assets and xStarbound assets:

```powershell
.\xclient.exe --renderer=vulkan-menus
```

This uses the existing SDL application loop, real splash/title rendering,
backgrounds, font/UI texture groups, and menu input. Check title text, buttons,
Options/Graphics, clipped scrolling panels, resize/fullscreen, and shutdown.
Its logs go to the normal storage directory configured by `xsbinit.config`.
The original `--renderer=vulkan` continues to run asset-free diagnostics.

The Vulkan interface effect is embedded SPIR-V; the client does not load the
OpenGL GLSL or framebuffer/blit configuration for this backend. Custom OpenGL
shader modifications are not applied in Vulkan preview mode.

World entry is blocked in `ClientApplication::changeState` before player/world
setup, server creation, or connection establishment. Attempting to start/join a
world returns to the main title menu with an explanatory message. Character
selection/creation and ordinary menu settings still use the configured storage;
for isolated testing supply a separate `-bootconfig` and `-noworkshop`.

## Updating an existing test installation

Copy the new `xclient.exe` **and the new Vulkan-capable `SDL2.dll`** from the same
build output. Keep the installation's existing `xsbinit.config`. If it needs
replacement, copy `xsbinit.config.install` and rename it to `xsbinit.config`.
The active config beside the build executable points to source-tree resources
and is unsuitable for copying into an installation.

## Implementation and scope

- SDL owns the desktop window and loads the Vulkan driver.
- The diagnostic owns the instance, optional debug messenger, surface, device,
  graphics/presentation queues, swapchain, views, render pass, graphics pipeline,
  framebuffers, command pool/buffer, semaphores, and frame fence.
- `VulkanRenderer` triangulates primitives and snapshots ordered draw packets.
  Cached buffers currently hold CPU primitives; each draw copies them into the
  frame's vertex stream and retains immutable texture sources, so later
  mutation/destruction cannot change old draws.
  Persistent GPU caching is a later improvement.
- The pipeline carries `RenderVertex` fields plus a per-vertex texture index, with
  normalized byte colors, no depth test or culling, and the OpenGL blend factors
  for both color and alpha. Pixel UVs are normalized using the selected texture's
  size during batching. The world shader uses `param1` to select lighting behavior.
- Each batch contains at most four distinct sampled textures, without sorting
  primitives. Four fixed sampler bindings avoid optional descriptor indexing.
  Unused bindings receive a white fallback; texture derivatives are calculated
  before selecting the sampler in the fragment shader.
- Images are copied to immutable RGBA8 sources, staged into optimal-tiled GPU
  images, and transitioned from undefined to transfer destination to shader
  read layout. Nearest/linear and clamp/repeat samplers use one mip level.
- Descriptor pools reset and released textures retire only after the frame
  fence. GPU cache entries hold weak source references; draw packets and cached
  buffers keep live sources alive. Samplers, views, images, and memory are freed
  on retirement or shutdown. There is no per-texture device-idle wait.
- Texture groups reuse `TextureAtlasSet`, including one-pixel replicated
  borders. Page sizes are conservatively 256/512/1024 pixels for Small/Medium/
  Large; oversized images use standalone textures. The size-limit setting
  reduces Large to Medium.
- Atlas writes invalidate a lazy immutable tile-version snapshot. Queued draws retain
  old tiles while later draws use the new version, so immediate frees
  and region reuse do not overwrite GPU-visible pixels. Cached primitives hold
  grouped textures and resolve their page offsets when queued. Groups outlive
  their textures; queued snapshots can outlive groups.
- Compression/relocation is disabled. Versions share immutable 64x64 tiles;
  writes copy only affected shared tiles. Fence-safe GPU image reuse uploads
  only tiles that differ from that image's last contents. First use of an image
  allocation still initializes the full page. See the atlas update section.
- Each draw pushes explicit padded matrix rows and screen dimensions (64 bytes).
  The shader and scissor conversion preserve the engine's bottom-left origin.
- One mapped host-visible vertex allocation is reused only after the frame
  fence completes. Noncoherent memory is flushed/invalidated explicitly.
- A single CPU frame slot simplifies initial synchronization. Presentation
  completion semaphores are allocated per swapchain image, not per frame slot.
- Separate graphics/presentation queue families use concurrent image sharing.
- FIFO is used for VSync. With VSync disabled, immediate or mailbox mode is
  selected when available; FIFO remains the fallback.
- Resize and out-of-date/suboptimal results trigger recreation after device
  idle. Minimized/zero-extent windows skip rendering while events continue.
- Device loss and surface loss produce logged failures; recovery is not yet
  implemented. This is desktop bring-up, not a console implementation.

Windows/NVIDIA is the initial tested platform. Linux, macOS/portability devices,
and separate presentation queues still require hardware/platform verification.

Verified on Windows/NVIDIA: texture smoke test presented 170 frames, with pixel
checks at 960x540, 800x600, and 2560x1440 and zero reported validation errors.
The single-texture fallback and original geometry checks also passed through
the sampled-image pipeline. Readback counts can differ by window system.
Atlas smoke test: 170 presented frames, 21 checks at each of 11 readbacks, zero
reported validation errors. The real title screen and Options panel were also
visually inspected using isolated storage and Workshop disabled, with a clean
shutdown and no logged Vulkan validation errors.

## Experimental world rendering

Launch the real game with world entry enabled:

```powershell
.\xclient.exe --renderer=vulkan-gameplay
```

When compiled in, OpenGL remains the default. `--renderer=vulkan-menus` still blocks world entry.
Use separate storage through `-bootconfig` when testing gameplay.

The embedded world shaders port bicubic lightmap sampling, premultiplied tile
lighting, emissive pixels, brightness, contrast, desaturation, and RGB adjustment.
Each draw snapshots its effect parameters and immutable lightmap sources. A
144-byte std140 uniform record is allocated at the device's required alignment.
Empty startup lightmaps use white lighting and transparent tile-light fallbacks.

World draws render into a sampled offscreen target. RGB-only writes preserve an
opaque alpha channel, matching the OpenGL RGB framebuffer. The first switch to
the interface effect composites that target once per frame; interface draws then
overlay it. Explicit render-pass dependencies synchronize writes and sampling.
The target is recreated with the swapchain on resize.

The asset-free diagnostic is available separately:

```powershell
.\xclient.exe --renderer=vulkan --vulkan-world --vulkan-smoke-test
```

It checks lighting modes, map transforms, bicubic sampling, parameter changes,
emissive and translucent pixels, empty maps, and world/interface composition.
The final Windows/NVIDIA run presented 170 frames and passed 17 pixel checks at
each of 11 readbacks, with zero reported validation errors, including teardown.
Texture (39 checks), geometry (13), and atlas (21) regressions also passed.
A real character's ship, celestial background, and quest UI were visually
inspected with isolated storage; no Vulkan validation errors were logged.

Gameplay remains experimental. Planet terrain, caves, liquids, particles,
inventory overlays, multiplayer, and long sessions need further testing.
Custom OpenGL shader mods are unsupported. Atlas allocation misses still require
full-page initialization, and platform verification beyond Windows/NVIDIA is pending.
This does not yet establish support for any console platform.

## Building without OpenGL

`STAR_ENABLE_OPENGL` defaults to ON. Set it OFF with `STAR_ENABLE_VULKAN=ON`
to exclude the OpenGL renderer, renderer factory, SDL context/swap calls, and
OpenGL/GLEW package discovery and linking. Client configurations with both
backends disabled fail during configuration; headless builds need neither.
The vcpkg `gui` feature supplies SDL, while `opengl` independently supplies GLEW.
CMake selects manifest features explicitly so the manifest defaults cannot
silently reintroduce GLEW into a Vulkan-only build.

For the existing Windows build directory:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' --preset windows-x64 `
  -DSTAR_INSTALL_VCPKG=ON -DSTAR_ENABLE_OPENGL=OFF -DSTAR_ENABLE_VULKAN=ON
& 'C:/Program Files/CMake/bin/cmake.exe' --build --preset windows-x64-release --target xclient
```

Vulkan SDK headers and `glslangValidator` are required to build this variant.
The Vulkan loader is still loaded through SDL at runtime. Copy the executable
and matching SDL2 DLL as described above; preserve the installation config.
Launching without a renderer flag selects Vulkan gameplay when OpenGL is not
built. `--renderer=opengl` reports that the backend is unavailable. Builds with
OpenGL enabled retain OpenGL as their default; Vulkan diagnostic/menu/gameplay
flags keep their existing meanings. Conflicting renderer flags are rejected.

To restore both backends, configure with `-DSTAR_ENABLE_OPENGL=ON` and
`-DSTAR_ENABLE_VULKAN=ON`. An OpenGL-only build uses Vulkan OFF.

Verification on Windows/NVIDIA:

- Configured and built with `CMAKE_DISABLE_FIND_PACKAGE_OpenGL=TRUE` and
  `CMAKE_DISABLE_FIND_PACKAGE_GLEW=TRUE`. These are verification flags only;
  remove them with `-UCMAKE_DISABLE_FIND_PACKAGE_OpenGL` and
  `-UCMAKE_DISABLE_FIND_PACKAGE_GLEW` before restoring OpenGL.
- Generated client/application projects contain no OpenGL/GLEW link entries;
  the application archive contains no OpenGL renderer/factory symbols.
- PE import inspection found no OpenGL/GLEW dependencies or `SDL_GL_*` imports
  in xclient. Its matching SDL DLL also has no OpenGL/GLEW DLL dependency.
- The Vulkan-only world smoke test passed 187 pixel checks over 170 presented
  frames and 11 readbacks, with zero Vulkan validation errors.
- Default startup selected experimental Vulkan gameplay and shut down cleanly
  using isolated storage. Re-enabling both backends built successfully and
  restored default OpenGL initialization; the final output was rebuilt Vulkan-only.
- Final Vulkan startup also succeeded with `SDL_OPENGL_LIBRARY` pointing to an
  intentionally nonexistent DLL, confirming SDL's OpenGL loader was unnecessary.

The process module list on this machine still includes `opengl32.dll` and
`GLU32.dll`, including with implicit Vulkan layers disabled. The exact load
origin has not been established. This is distinct from the game's removed
compile/link/API dependency: vendor drivers, overlays, and other runtime
components can load additional libraries. SDL itself still ships optional
OpenGL support; this change does not rebuild SDL without those capabilities.
Platforms without OpenGL still require verification with their SDL/driver build.

## Profiling loading and frame pacing

Enable `XSB_ENABLE_TRACY=ON` and use the profiler built from the bundled Tracy
source. The current local viewer is `cmake-build-tracy-profiler/Release/tracy-profiler.exe`;
connect to `127.0.0.1:8086`. Keep using the optimized RelWithDebInfo game build.

Set validation explicitly for comparable performance captures:

```powershell
$env:XSB_VULKAN_VALIDATION = 'off'
.\xclient.exe
```

For correctness checks use `'on'`; this requires the Khronos validation layer
and fails clearly if it is missing. `'auto'` (or an unset variable) preserves
the prior behavior of enabling the layer when available. The startup log reports
the actual state. These variables apply to diagnostics as well as gameplay.
Externally forced Vulkan layers may still be injected by loader configuration;
the toggle controls the layer explicitly requested by this backend.

New CPU zones cover parallax layers (with source paths), asset texture cache
misses, synchronous asset loading/waiting, renderer texture creation, atlas
region writes/page snapshots, draw batching, vertex uploads, host-buffer growth,
GPU texture creation/destruction, staging/transfer recording, descriptor/uniform
preparation, command recording, frame fences, acquisition, submit, and present.
Existing `Assets::image` annotations include image paths/subimages.

Tracy plots report per-frame texture cache misses, atlas snapshot count/bytes,
atlas region-write bytes, draw batches/vertices, uploaded texture count/bytes,
retired texture count, and live GPU texture-cache count. CPU counters reset at
Vulkan `startFrame`; GPU counters are sampled during presentation. Startup work
outside a frame is visible in zones but is not included in these frame counters.
Byte counters represent logical RGBA payload, not driver allocation sizes.
The atlas CPU copy plot is now `Vulkan atlas CPU copy bytes/frame`: it counts
copied 64x64 tile payload, replacing the former full-page snapshot-byte plot.
These are CPU wall-time measurements, including blocking calls, not GPU timestamps.

Record a repeatable route from the populated area through the parallax boundary
and back. Run it twice to distinguish first-load/cache effects. Save a 30–60s
trace and note approximate times of each boundary and populated-area departure
or return. Keep mods, resolution, VSync, and validation state the same for any
comparison capture. This change adds observability; it does not optimize uploads
or eliminate the observed pauses.

## Parallax texture optimization

Vulkan parallax layers now use a separate standalone texture group. This avoids
packing large backgrounds into atlases, constructing per-pixel atlas borders,
and snapshotting/uploading whole atlas pages for them. Filtering remains nearest
and addressing remains clamped; source pixels and the existing tiling, UVs,
lighting flags, and directives are preserved. OpenGL retains its prior path.

The Vulkan parallax asset group retains up to 64MiB of logical RGBA payload in
unused textures, selecting the most recently used ones. Background textures used
in the current frame are protected independently of this idle budget. Multiple
asset aliases to the same texture count once. Cleanup drops map and image
deduplication references for evictions; queued draw sources and GPU frame-fence
retirement keep in-flight resources safe. Root reload invalidates the cache.
UI, terrain, stars, and other environment textures keep their existing policy.

This is not a 64MiB cap on total process/GPU memory: the current working set,
retained CPU images, driver allocation overhead, and in-flight uploads also
consume memory. One oversized idle texture is evicted if it cannot fit.
Tracy plots `Parallax idle retained bytes` and `Parallax idle evictions/frame`
show the policy in operation.

The focused `rendering_tests` integration runner checks idle retention, budget
eviction, working-set protection, least-recently-used selection, unchanged normal
timeout behavior, and reload invalidation. With Vulkan enabled it also renders
real parallax assets through the CPU draw recorder, compares their standalone
pixels to the source, and verifies texture identity is reused after other
backgrounds are visited. Invoke it with an isolated `-bootconfig` containing
installed base assets; it is not an asset-free CTest entry.

For the next capture, keep validation off and repeat the previous route,
including an immediate return and a longer revisit. Expect parallax-driven
atlas page snapshots to disappear and revisits within the idle budget to reuse
GPU sources. First-time synchronous image processing can still cause a pause;
its elimination requires separate preloading/asset-loading work. Other sprite
atlas mutations use the tile update path below. Improvements on the actual
walking route must be established by a new trace rather than assumed.

## Atlas tile updates and image reuse

Atlas contents are partitioned into immutable 64x64 RGBA tiles. A new CPU version
copies a small vector of tile references; region writes copy only shared tiles
they touch. Multiple writes before another snapshot share the writable tile.
Queued versions retain old tiles across frees, region reuse, and group teardown.
Rows are copied directly when the input is already RGBA, avoiding an additional
format-conversion copy. The allocator's existing replicated borders are retained.

After the frame fence, the backend first collects every source version required
by the current draws. An atlas GPU image can be reused only if its old version
is absent from that set, and it belongs to the same atlas page. The image tracks
its immutable CPU tile identities; only differing tiles are staged and copied,
with all copies for one destination emitted together. Required same-frame
versions always have distinct images. Reused images transition from shader-read
to transfer-destination and back; new images initialize all tiles from undefined.

Unused atlas allocations may be retained up to 64MiB of logical RGBA payload,
in addition to the current required images. A weak page-owner token releases
allocations when their page dies. This budget is separate from the parallax
idle-texture budget and excludes driver padding, current working images and
staging/CPU data. Slots can be rehydrated from immutable tiles if a retained CPU
version is needed after its former GPU slot was reassigned.

New Tracy plots show `Vulkan reused atlas images/frame` and
`Vulkan spare atlas payload bytes`. The existing upload-byte counter reports
actual tile staging payload rather than the logical size of destination pages.
On the atlas mutation diagnostic, a short capture recorded median CPU tile
copies of 48KiB/frame and 288KiB/frame of uploads (including a deliberately new
256KiB orphan page each frame), with two reused GPU images per frame. This is
synthetic verification, not a prediction of the walking-route improvement.

The atlas diagnostic changes region colors every frame and, with verification
enabled, checks all frames to detect stale reused images. PNG evidence is saved
only on swapchain changes. A 170-frame smoke run passed 3,570 GPU pixel checks
with synchronization validation enabled and zero validation errors. World,
texture, geometry and focused cache/parallax tests also passed. A CPU test
replaces a bordered 70x70 region crossing four tiles, verifies the older red
version remains intact, and verifies only those four tile references change.

Testing also exposed an intermittent Windows startup collision between Tracy
and engine DbgHelp symbol initialization. The engine now uses a duplicated
process handle for its own symbol session, and the engine and Tracy share a
process-lifetime initialization-safe DbgHelp lock. This follows Microsoft's
[symbol-handler initialization guidance](https://learn.microsoft.com/en-us/windows/win32/debug/initializing-the-symbol-handler).

## Prepared render-buffer geometry

Vulkan render buffers now retain triangulated vertices and texture-batch topology
after their first draw. Subsequent draws copy prepared vertices into the frame
stream instead of repeating primitive traversal, texture-slot selection and UV
normalization. `set()` invalidates preparation, as does a change to the renderer's
multitexturing setting. Immediate geometry still uses the existing preparation path.
Live atlas placements and dimensions are stable because relocation is disabled;
enabling relocation later requires invalidating or updating cached UVs.

Each draw resolves current immutable texture-page versions from live handles and
applies current transforms, scissor and world/lighting state. Cached buffers do
not retain old atlas snapshots. Prepared CPU vertices add memory proportional to
live render buffers; GPU vertex uploads still occur every frame.

Tracy separates `Vulkan rebuild cached geometry`, `Vulkan reuse cached geometry`
and `Vulkan prepare immediate geometry`. Compare these zones and total rendering
time in a new Switch ship capture before claiming an FPS improvement.

Validation: rendering_tests passes atlas-version refresh between cached draws,
unchanged UV/index reuse, current scissor state and replacement with an empty
buffer, alongside the existing atlas/parallax checks. Windows atlas smoke passes
170 presented/verified frames and zero validation errors. ARM performance remains
to be measured on device.

References: [SDL2 Vulkan surface creation](https://wiki.libsdl.org/SDL2/SDL_Vulkan_CreateSurface),
[Khronos swapchain recreation](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/04_Swap_chain_recreation.html),
[Khronos presentation semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).


## Bounded renderer work reuse

The Vulkan backend now avoids redundant pipeline/scissor commands within each
render pass. Tracking is local to a command-buffer recording and reset at pass
boundaries. The first world pass clears its attachment directly; later world
passes load it. If composition occurs without world draws, a deferred clear-only
pass still supplies opaque black. Frames that never use the world target need
no world clear. Lightmap coordinates are now calculated per vertex, matching the
OpenGL placement of the affine transform, with binding 6 visible to both shader
stages. Bicubic sampling and lighting quality are unchanged.

Texture-upload records and regions, buffer/image copy records, and descriptor
layout arrays reuse CPU storage. Upload regions are flattened into one array,
removing per-image region-vector allocations. Scratch arrays release capacity
above 1 MiB at cleanup; draw-descriptor handle storage is trimmed at the next
frame preparation. Scratch storage does not retain texture pixel sources.

Streaming host buffers grow by 50%, capped at 1 MiB of speculative growth, or
straight to the required size if larger. After 120 consecutive frames using at
most half the capacity and leaving more than 1 MiB unused, they shrink to the
current requirement (minimum 4 KiB). Vertex, texture-staging and effect-uniform
buffers are considered even on frames with no corresponding work. Descriptor
pools use the same policy in set counts, with a 128-set minimum and at most
1,024 sets of growth headroom. These limits bound spare capacity, not necessary
working storage. The existing atlas/parallax retention budgets are unchanged.

Effect-lighting sources remain immutable CPU snapshots, but their GPU images,
allocations, views and samplers can be reassigned after the frame fence. Reuse
requires matching dimensions and an old version absent from every draw in the
current frame. All such images are RGBA8, linear-filtered, clamped, and single-mip.
Same-frame versions retain separate allocations. Unclaimed lighting images are
released immediately after assignment: there is no idle lighting pool. Size
changes and extra same-frame versions can still require allocations, and pixel
uploads are not skipped. Tracy reports new and reused lighting images per frame;
the world diagnostic also logs aggregate allocation/reuse counts.

Verification on Windows/NVIDIA RTX 4070 Ti SUPER:

- RelWithDebInfo client and rendering_tests builds succeeded.
- World smoke test: 170 presented/verified frames, 2,578 pixel checks, zero
  validation errors with synchronization validation enabled. It alternates
  lighting colors and dimensions, checks same-frame snapshots, exercises empty
  world composition and scissor restoration, and verifies consecutive matching
  updates allocate no new lighting images. This mutation-heavy run recorded 520
  lighting allocations and 648 reuses; it deliberately changes sizes and drops
  all lighting use on empty-world frames.
- Atlas, texture, single-texture and geometry smoke tests each presented 170
  frames with zero validation errors. Atlas verified every frame; the other
  modes verified each swapchain transition.
- Renderer cache/parallax integration tests passed against isolated storage and
  base assets plus the source-tree assets.

These tests establish rendering correctness and exercise allocation reuse, not
an FPS improvement or lower process residency. Compare gameplay frame-time
percentiles and memory use with validation disabled before claiming performance
parity. For correctness runs, enable XSB_VULKAN_VALIDATION=on and, with current
Khronos layers, VK_LAYER_VALIDATE_SYNC=1.
