#!/usr/bin/env python3
"""Summarize bounded server simulation timing; nested wall times are not CPU-only."""
import argparse
import hashlib
import json
import re
from pathlib import Path

def fields(line):
    return dict(re.findall(r"([\w-]+)=([^ |]+)", line.split(" label=", 1)[0]))

SAMPLED = {"entity-npc", "entity-object", "entity-other", "npc-update", "npc-script",
           "npc-movement", "npc-status", "npc-shared", "path-find", "path-explore"}

def analyze(path):
    raw = path.read_bytes()
    lines = raw.decode("utf8", errors="replace").splitlines()
    if not lines or not lines[0].startswith("simulation-profile version=1"):
        raise ValueError("Expected simulation-profile version=1")
    worlds, windows, phases, names = {}, [], [], []
    for line in lines:
        f = fields(line)
        if line.startswith("[world]"):
            worlds[int(f["slot"])] = {"label": line.split(" label=", 1)[1],
                "overflow_ticks": int(f["overflow-ticks"]),
                "dropped_named_samples": int(f["dropped-named-samples"]),
                "target_hz": float(f["target-hz"]),
                "thread_queried": bool(int(f["thread-queried"])),
                "thread_query_errors": int(f["thread-query-errors"]),
                "thread_id": int(f["thread-id"]), "priority": int(f["priority"]),
                "core_mask": int(f["core-mask"], 16), "preferred_core": int(f["preferred"])}
        elif line.startswith("[server-window]"):
            row = {k.replace("-", "_"): int(v) if k in ["slot", "ticks", "detailed", "advanced-ticks", "npc-updates", "object-updates", "other-updates", "fidelity-min", "fidelity-low", "fidelity-medium", "fidelity-high"] else float(v[:-1]) if k == "start" else float(v) for k,v in f.items()}
            row["start_s"] = row.pop("start")
            row["mean_simulated_step_ms"] = row["simulated_ms"] / row["advanced_ticks"] if row["advanced_ticks"] else None
            row["nonadvancing_ticks"] = row["ticks"] - row["advanced_ticks"]
            windows.append(row)
        elif line.startswith("[server-phase]"):
            phases.append({"slot": int(f["slot"]), "start_s": float(f["start"][:-1]),
                "kind": f["kind"], "calls": int(f["calls"]),
                "total_ms": float(f["total-ms"]), "max_ms": float(f["max-ms"])})
        elif line.startswith("[server-name]"):
            names.append({"slot": int(f["slot"]), "kind": f["kind"], "calls": int(f["calls"]),
                "total_ms": float(f["total-ms"]), "max_ms": float(f["max-ms"]),
                "label": line.split(" label=", 1)[1]})
    lookup = {(w["slot"], w["start_s"]): w for w in windows}
    for phase in phases:
        w = lookup[phase["slot"], phase["start_s"]]
        phase["sampled"] = phase["kind"] in SAMPLED
        denominator = w["detailed"] if phase["sampled"] else w["ticks"]
        phase["recorded_ticks"] = denominator
        phase["mean_ms_per_recorded_tick"] = phase["total_ms"] / denominator if denominator else None
        phase["mean_ms_per_call"] = phase["total_ms"] / phase["calls"] if phase["calls"] else None
    return {"capture": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
            "header": lines[:3], "capacity": fields(lines[1]), "worlds": worlds,
            "warning": "Independent authoritative server cycles. Fine phases sample one tick in eight; nested times overlap. Simulation rate includes pause/timescale. Named NPC labels classify actors, not specific Lua functions. Truncated labels and bounded/drop counters limit attribution. No CPU-only timing or FPS projection.",
            "windows": windows, "phases": phases,
            "named_totals": sorted(names, key=lambda x:x["total_ms"], reverse=True)}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.capture), indent=2))
