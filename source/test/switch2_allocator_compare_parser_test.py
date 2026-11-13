"""Deterministic report validation; fixtures are synthetic, never console results."""
from pathlib import Path
import importlib.util
import tempfile
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("parser", root / "scripts/switch2/analyze_allocator_compare.py")
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)

def capture(baseline):
    before = baseline + "-before"
    lines = ["allocator-compare version=1 synthetic-fixture",
             f"[allocator-build] sdk=ounce-sdk-test cpu=-mcpu=cortex-a78c+crypto baseline={before}"]
    for allocator in [before, "rpmalloc-1.4.5-sdk-backed", baseline + "-after"]:
        for workload in ["allocate-free", "mixed-realloc"]:
            for threads in [1, 2, 4]:
                for trial in [1, 2, 3]:
                    lines.append(f"[allocator-concurrency] allocator={allocator} workload={workload} threads={threads} trial={trial} operations=100 checks=50 wall-ms=200 aggregate-Mop/s=1 main-Mop/s=1 verified=true")
    lines += ["[correctness] verified=true", "[remote-stress] verified=true",
              "[correctness] verified=true", "[remote-stress] verified=true", "[failure-tests] verified=true",
              "[backing] phase=finalized charged-bytes=0 active=0 errors=0", "finished status=0"]
    return "\n".join(lines) + "\n"

with tempfile.TemporaryDirectory() as folder:
    newlib = Path(folder) / "newlib.log"
    mimalloc = Path(folder) / "mimalloc.log"
    newlib.write_text(capture("newlib-dlmalloc"))
    mimalloc.write_text(capture("sdk-mimalloc"))
    assert parser.compare([newlib, mimalloc])["complete_and_verified"]
    assert not parser.compare([newlib])["complete_and_verified"]
    assert not parser.compare([newlib, newlib])["complete_and_verified"]
    for old, new in [("finished status=0", "finished status=1"),
                     ("verified=true", "verified=false"),
                     ("cortex-a78c", "cortex-a57"),
                     ("ounce-sdk-test", "ounce-sdk-other"),
                     ("trial=3", "trial=2")]:
        mimalloc.write_text(capture("sdk-mimalloc").replace(old, new))
        assert not parser.compare([newlib, mimalloc])["complete_and_verified"], (old, new)
    mimalloc.write_text(capture("sdk-mimalloc").replace("aggregate-Mop/s=1", "aggregate-Mop/s=2", 3))
    assert any(row["over_15_percent"] for row in parser.compare([newlib, mimalloc])["baseline_drift"])
print("Allocator suite parser passed: valid pair; missing, duplicate, failed, mismatched and malformed trials; drift reporting")
