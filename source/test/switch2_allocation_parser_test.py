from pathlib import Path
import importlib.util
import sys
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("allocation_parser",root/"scripts/switch2/analyze_allocation_callers.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
data=module.analyze(Path(sys.argv[1]))
assert data["metadata"]["frames"]=="2" and data["metadata"]["detailed"]=="1"
api={row["kind"]:row for row in data["api_totals"]}
assert api["new"]["calls"]==10002 and api["delete"]["calls"]==10000
assert api["malloc"]["calls"]==10002 and api["free"]["calls"]==10003
assert api["malloc"]["total_ms"]>=2 and api["malloc"]["max_ms"]>=1
assert api["new"]["size_arguments"]==2**64-1
assert data["callers"] and data["clock_calibration"] and data["slow_samples"]
assert any(row["name"]=="worker metadata" for row in data["thread_affinity"])
assert all(row["stage"]=="render" for row in data["api_totals"])
assert all(row["scene"]==3 for row in data["api_totals"]+data["callers"])
assert len(data["allocation_windows"])==1
window=data["allocation_windows"][0]
assert window["scene"]==3 and window["frames"]==2 and window["detailed_frames"]==1
assert window["calls"]==sum(row["calls"] for row in data["api_totals"])
assert abs(window["total_ms"]-sum(row["total_ms"] for row in data["api_totals"]))<0.00002
if len(sys.argv)>2:
    elf=Path(sys.argv[2]);prefix="C:/devkitPro/devkitA64/bin/aarch64-none-elf-"
    anchor,decoded=module.symbols(elf,{4},prefix)
    assert decoded[4] and "starAllocationProfileAnchor" in decoded[4][0]["function"]
print("PASS: exact API parsing, full service time, byte saturation, slow calls, thread metadata and optional ARM64 source symbolization.")
