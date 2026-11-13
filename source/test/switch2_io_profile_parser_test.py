"""Check attribution against output from switch2_io_profile_host.cpp."""
import importlib.util
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("io_parser",root/"scripts/switch2/analyze_io_profile.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
result=module.analyze(Path(sys.argv[1]))
assert len(result["frame_windows"])==1
frame=result["frame_windows"][0]
assert frame["frames"]==frame["io_frames"]==1
assert frame["calls"]==5 and frame["actual_bytes"]==278 and frame["errors"]==1
assert frame["read_calls"]==4 and frame["read_bytes"]==271
assert frame["write_calls"]==1 and frame["write_bytes"]==7 and frame["sync_calls"]==0
assert result["worst_io_frames"][0]["frame"]==42
assert result["worst_io_frames"][0]["scene"]==3
assert sum(r["calls"] for r in result["cache_windows"] if r["kind"]=="block-hit")==4000
assert sum(r["calls"] for r in result["cache_windows"] if r["kind"]=="script-hit")==1
assert sum(r["calls"] for r in result["cache_windows"] if r["kind"]=="script-miss")==1
assert sum(r["bytes"] for r in result["asset_windows"] if r["category"]=="script")==261
assert any(not thread["included"] for thread in result["threads"].values())
assert len([r for r in result["file_operations"] if r["operation"]=="pread" and r["calls"]==1000])==4
assert sum(r["calls"] for r in result["file_operations"] if r["operation"]=="pread")==4003
assert any(r["asset"]=="/scripts/test.lua" and r["frame"]==42 for r in result["worst_calls"])
print("PASS: parsed exact bytes/counts, main/background distinction, cache/category/frame attribution and omitted in-flight slot.")
