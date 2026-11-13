#!/usr/bin/env python3
"""Read a buffered wait report; print compact JSON without modifying the capture."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def analyze(path):
    raw = path.read_bytes()
    text = raw.decode("utf-8", errors="replace")
    if not text.startswith("wait-profile version=1"):
        raise ValueError("Not a version 1 buffered wait profile")
    windows, hitches, background = [], [], []
    cpu_scopes, heap_samples, work_counts = [], [], []
    heap_scopes=[]
    for line in text.splitlines():
        if line.startswith("[window]"):
            values = dict(re.findall(r"([\w-]+)=([^ |]+)", line.split(" | ", 1)[0]))
            frame_ms = float(values["frame-avg"].removesuffix("ms"))
            cpu_ms = float(values["cpu-running"].removesuffix("ms"))
            wall_ms = float(values["scheduler-wall"].removesuffix("ms"))
            metrics = {name: {"average_ms": float(avg), "max_span_ms": float(peak), "calls_per_frame": float(calls)}
                       for name, avg, peak, calls in re.findall(r"\| ([\w-]+) avg=([\d.]+)ms max-span=([\d.]+)ms calls/frame=([\d.]+)", line)}
            windows.append({"start_s": float(values["start"].removesuffix("s")),
                            "frames": int(values["frames"]), "sampled_frames": int(values["detailed"]),
                            "scene_mask": int(values["scenes"], 16), "frame_average_ms": frame_ms,
                            "fps_from_average_interval": 1000/frame_ms if frame_ms else None,
                            "frame_max_ms": float(values["frame-max"].removesuffix("ms")),
                            "scheduled_percent_approx": 100*cpu_ms/wall_ms if cpu_ms >= 0 and wall_ms > 0 else None,
                            "metrics": metrics})
            rng = re.search(r"rng-lock-sparse calls/detailed-frame=([\d.]+) timed=(\d+) contended=(\d+) measured-total=([\d.]+)ms max-span=([\d.]+)ms", line)
            if rng:
                calls, timed, contended, total, peak = rng.groups()
                windows[-1]["sparse_rng_lock"] = {"calls_per_detailed_frame":float(calls),"timed_calls":int(timed),"observed_contentions":int(contended),"measured_total_ms":float(total),"maximum_sampled_wait_ms":float(peak)}
        elif line.startswith("[cpu-scope]"):
            v=dict(re.findall(r"([\w-]+)=([^ |]+)",line))
            wall=float(v["wall-total-ms"]);running=float(v["running-total-ms"])
            cpu_scopes.append({"start_s":float(v["start"][:-1]),"metric":v["metric"],"calls":int(v["calls"]),
                               "wall_total_ms":wall,"running_total_ms":running,
                               "off_cpu_ms_approx":max(0,wall-running),
                               "running_percent_approx":100*running/wall if wall>0 else None,
                               "wall_per_call_ms":float(v["wall-per-call-ms"]),
                               "running_per_call_ms":float(v["running-per-call-ms"])})
        elif line.startswith("[heap-scope]"):
            v=dict(re.findall(r"([\w-]+)=([^ |]+)",line))
            heap_scopes.append({"start_s":float(v["start"][:-1]),"metric":v["metric"],"calls":int(v["calls"]),
                                "acquisitions":int(v["acquisitions"]),"per_call":float(v["per-call"]),
                                "per_detailed_frame":float(v["per-detailed-frame"])})
        elif line.startswith("[heap-lock]"):
            v=dict(re.findall(r"([\w-]+)=([^ |]+)",line))
            heap_samples.append({"start_s":float(v["start"][:-1]),"detailed_frames":int(v["detailed"]),
                                 "calls_on_detailed_frames":int(v["calls"]),"timed":int(v["timed"]),
                                 "measured_total_ms":float(v["measured-total-ms"]),"maximum_sample_ms":float(v["max-ms"]),"stride":int(v["stride"])})
        elif line.startswith("[work-count]"):
            v=dict(re.findall(r"([\w-]+)=([^ |]+)",line))
            work_counts.append({"start_s":float(v["start"][:-1]),"detailed_frames":int(v["detailed"]),"kind":v["kind"],
                                "total":int(v["total"]),"per_detailed_frame":float(v["per-detailed-frame"])})
        elif line.startswith("[hitch]"):
            m = re.match(r"\[hitch\] at=([\d.]+)s frame=(\d+) scene=(\d+) detailed=(\d+) elapsed=([\d.]+)ms", line)
            if m:
                at, number, scene, detailed, elapsed = m.groups()
                hitches.append({"at_s":float(at),"frame":int(number),"scene":int(scene),
                                "sampled":bool(int(detailed)),"elapsed_ms":float(elapsed)})
        elif line.startswith("[background]"):
            m = re.match(r"\[background\] at=([\d.]+)s duration=([\d.]+)ms label=(.*)",line)
            if m:
                at, duration, label = m.groups()
                background.append({"at_s":float(at),"duration_ms":float(duration),"label":label})
    overlaps=[]
    for h in hitches:
        for b in background:
            if "cleanup-held" in b["label"] and b["at_s"] < h["at_s"]+h["elapsed_ms"]/1000 and h["at_s"] < b["at_s"]+b["duration_ms"]/1000:
                overlaps.append({"hitch":h,"cleanup":b})
    return {"capture":str(path.resolve()),"sha256":hashlib.sha256(raw).hexdigest(),
            "warning":"Wall times overlap; detailed metrics are sampled. Cleanup/hitch overlap is correlation, not proof of blocking. Scheduler windows are approximate.",
            "windows":windows,"worst_hitches":sorted(hitches,key=lambda h:h["elapsed_ms"],reverse=True)[:10],
            "cpu_scopes":cpu_scopes,"heap_lock_samples":heap_samples,"heap_scopes":heap_scopes,"work_counts":work_counts,
            "cpu_query_calibration":next((line for line in text.splitlines() if line.startswith("cpu-query-calibration")),None),
            "longest_background_spans":sorted(background,key=lambda b:b["duration_ms"],reverse=True)[:10],
            "cleanup_hitch_overlaps":sorted(overlaps,key=lambda x:x["hitch"]["elapsed_ms"],reverse=True)[:10],
            "overflow":next((line for line in text.splitlines() if line.startswith("overflow-frames=")),None)}


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture",type=Path)
    args=parser.parse_args()
    print(json.dumps(analyze(args.capture),indent=2))
