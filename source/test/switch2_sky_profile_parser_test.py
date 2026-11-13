import importlib.util
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("wait_parser",root/"scripts/switch2/analyze_wait_profile.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
data=module.analyze(Path(sys.argv[1]))
heap=data["heap_lock_samples"][0]
assert heap["calls_on_detailed_frames"]==256 and heap["timed"]==16 and heap["stride"]==16
heap_update=next(s for s in data["heap_scopes"] if s["metric"]=="update")
assert heap_update["calls"]==2 and heap_update["acquisitions"]==64 and heap_update["per_call"]==32
heap_immediate=next(s for s in data["heap_scopes"] if s["metric"]=="renderer-immediate")
assert heap_immediate["calls"]==32 and heap_immediate["acquisitions"]==64
update=next(s for s in data["cpu_scopes"] if s["metric"]=="update")
assert update["calls"]==2 and update["running_total_ms"]==.020
immediate=next(s for s in data["cpu_scopes"] if s["metric"]=="renderer-immediate")
assert immediate["calls"]==16
assert data["work_counts"][0]["total"]==22
old=module.analyze(root/"dist-switch2/io_bench_1/xstarbound-waits.log")
assert old["windows"] and not old["cpu_scopes"] and not old["heap_lock_samples"]
print("PASS: CPU/heap/work parsing, bounded CPU samples and old-report compatibility.")
