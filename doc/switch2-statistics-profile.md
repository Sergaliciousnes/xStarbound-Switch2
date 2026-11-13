# Switch 2 statistics and achievement profiling

Enabled in the stats-controls test NROs through STAR_SWITCH2_EXPLORATION_PROFILE=ON, with the normal buffered wait and I/O profiles. Heavy allocation profiling remains OFF. Statistics.cpp gets this definition only when the exploration option is enabled. Disabled macros discard all label/counter argument evaluation.

## Measurements

- stats-event: total Statistics::processEvent call, labelled by event name.
- stats-achievement: total checkAchievement call, labelled by achievement name.
- stats-lookup: event/achievement database lookup.
- stats-config: assets/client configuration access before creating a script VM.
- stats-vm-setup: LuaRoot/engine construction and garbage-collector tuning.
- stats-component-setup: component/script list/callback setup.
- stats-init: component initialization, including script loading.
- stats-invoke: event/check execution.
- stats-uninit: explicit component uninitialization.
- stats-destroy: actual component/root destruction on normal return, including final ownership release rather than just the explicit shared_ptr reset.
- stats-script: the complete runStatScript call, labelled by the first configured script path; detail is the configured script-file count. Inner stages carry the event/achievement name through a thread-local context restored across nested calls.

Counters record event/check/script executions, configured script-file occurrences, caught failures, and achievement checks that find the achievement already unlocked. These counts include nested achievement checks triggered through callbacks. No event fields, chat text or command contents are added to profiling.

Every tagged operation is timed on the main thread; no 1-in-8 sampling for these statistics stages. Scope times are wall time, can include preemption/locks and overlap. Parent/child totals must not be added. Failures retain existing handling and outer timings/counters; stats-destroy specifically covers normal return, not exceptional unwinding.

## Named totals and bounded overhead

The existing recorder retains five-second totals and the largest 512 individual scopes >=2 ms, plus worst frames >=50 ms. A separate fixed table retains 512 (kind, truncated label) pairs for stats-event, stats-achievement and stats-script. Each row records call count, total time and maximum time, even when all calls are shorter than 2 ms. Labels are copied/truncated to 191 bytes; matching truncated labels are combined.

The table is bounded, allocation-free and main-thread-only; no extra mutex or file write is introduced during recording. dropped-named-samples reports calls whose label could not fit, and does not suppress the normal phase totals. Existing recording memory is extended by fixed tables only. The updated parser exposes named_statistics and named_statistics_capacity.

Lua isolation, execution order, garbage-collection policy, achievement logic and save behavior are unchanged. The profiler measures current behavior before choosing an optimization.

## Test and capture

Host tests verify exact timing/counts, labels, nested phases, short-event accumulation, table saturation/overflow accounting, background/disabled exclusion, bounded worst records and failed-write retry. Parser tests check the new rows and preserve support for older captures.

Play the same planet route, perform activities that previously produced the brief pauses, and visit the science outpost. Quit with the in-game button, then fully close before FTP. Return xstarbound-exploration.log, xstarbound-waits.log, xstarbound-io.log, xstarbound-rpmalloc.log and xclient.log. There is no new statistics-specific log file: all new stages and named totals go into xstarbound-exploration.log.
