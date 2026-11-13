# VM / gather / interface profiling candidate

Adds visibility to the remaining issues found in stat_prof_2. The statistics bytecode cache, unchanged-save helpers, object cleanup, allocator routing and controls are retained. These probes do not introduce VM pooling, culling, script throttling, changed event order or asynchronous saves.

## VM creation

Every main-thread LuaEngine::create call records:

- vm-create: whole engine creation, labelled safe/unsafe.
- vm-state: engine allocation, lua_newstate and seed/time retrieval.
- vm-registry: handle thread, error handler, registry and metatable setup.
- vm-base: base library opening and safe-mode whitelist filtering.
- vm-os: OS library opening and safe-mode filtering.
- vm-library: each library opened through loadBaseLibrary, labelled by library name, including optional unsafe-mode libraries.
- vm-finalize: default environment snapshot and remaining global helpers.
- vm-root-config: LuaRoot safe-mode/config access before creation and limits/configuration after creation.
- vm-root-bookkeeping: LuaRoot reload-listener registration and storage-path setup.

VM probes cover all main-thread callers, not only statistics. Use the enclosing stats-vm-setup, script-component-init, packet/entity or UI phases and frame IDs to establish context. Library named totals include every recorded call, even calls below the individual 2 ms retention threshold. Unlabelled small gaps between stages remain; parent/child times are not exclusive.

## World gathering

The coarse stages gather-lights, gather-lighting, gather-highlights, gather-entities, gather-tiles and gather-environment cover every render frame. They split traversal/light-source collection, lighting preparation/old-map access, interaction/highlight setup, entity callback/assembly work, tile copying/preview application and environment/parallax data gathering.

Per-entity callback timings run only on the existing detailed one-frame-in-eight sample:

- gather-npc-render / gather-object-render / gather-other-render.
- gather-npc-lights / gather-object-lights / gather-other-lights.
- gather-npc-directives / gather-object-directives / gather-other-directives. The directives stages include post-render assembly, highlights and effect collection, not only directive parsing.

Slow retained entity records carry entity ID in detail and type in the label. Type-label lookup is avoided on unsampled frames. Other includes players, monsters, plants and remaining types. A single unsampled entity spike will appear in the coarse stage but may not have a per-entity record.

Work counters gather-npcs, gather-objects, gather-other, gather-light-calls, gather-light-sources and gather-drawables cover all frames. These are callback/work executions, not guaranteed unique visible entities. Normalize sampled timing by its own calls, not by the all-frame counter without accounting for sampling. No geometry, audio, particles, lighting behavior or entity traversal order is changed.

## Interface and script creation

ui-input, ui-update, ui-render and ui-interact cover every MainInterface call, closing the previous one-frame-in-eight gap for large interface-related pauses. ui-inventory-update isolates updateItems.

PaneManager records total and per-active-pane update/render timing; pane names identify slow pane calls. The parent manager includes children, tooltip creation, positioning and other housekeeping. An empty pane name combines anonymous panes in named totals.

ui-pane-build labels inventory, crafting and base scripted-pane construction. ui-widget-tree covers root GUI-tree construction, including child attachment/sorting. ui-widget-build labels types (or single-widget for the makeSingle helper), and ui-widgets counts construction work. Include/parent widget scopes may enclose child scopes. ui-tooltip labels the resolved tooltip configuration for its main construction work.

Generic LuaBaseComponent probes split script-component-init, script-context creation and script-init-function execution, labelled by the first script path. These can include NPC/statistics scripts as well as UI scripts. Use enclosing UI/entity phases to identify the caller. Component setup and other small work remain within script-component-init, so subtracting children can guide narrower follow-up timing.

Native chat keyboard time is intentionally included by ui-input and the frame record while its applet blocks. Exclude that deliberate interaction when comparing gameplay FPS; the client log records keyboard opening/closing. Text/message contents are not added to profiling labels.

## Recording and overhead

All probes reuse xstarbound-exploration.log, buffered in fixed RAM tables until normal quit. No recording-time file logging or extra recorder mutex is added. Timing reads use the existing tick clock rather than per-probe CPU-query syscalls. Heavy allocation profiling stays OFF.

The fixed named table remains 512 entries. Its historical [stats-name] rows / stats-name-capacity header now also include VM libraries, pane/widget/tooltip categories, per-pane update/render and generic script initialization. dropped-named-samples reports saturation; five-second phase totals remain independent. Named labels are copied/truncated to 191 bytes. Named totals combine scenes over the capture; use phase windows/slow frame records for scene-specific attribution.

The parser exposes named_scope_totals for all labelled categories and retains named_statistics as a statistics-only filtered view. Existing older logs remain readable. All times are wall time, may include waits/preemption and overlap with parent scopes. The wait profile still supplies sampled running-time context for overall gathering/rendering.

## Verification and console capture

Recorder/parser tests cover exact new VM-library/widget named totals, sampled entity exclusion, counters, bounded capacity/overflow and disabled argument evaluation. All eleven newly instrumented C++ sources have the opt-in source definition and pass native syntax checks with profiling disabled. Both resolutions must compile/link/package successfully before delivery.

Use xclient-vm-gather-ui-1080p.nro to match the previous captures. Repeat item collection to trigger statistics, then walk through the same crowded outpost areas at the same zoom. Open and close inventory/crafting; hover items long enough to trigger tooltips. Note approximately when these interactions occur. Avoid mixing long keyboard/menu pauses into the walking comparison.

Quit normally and fully close before FTP. Return xstarbound-exploration.log, xstarbound-waits.log, xstarbound-io.log, xstarbound-rpmalloc.log and xclient.log. No additional log is expected. Previous optimization binaries and the standard build remain preserved.
