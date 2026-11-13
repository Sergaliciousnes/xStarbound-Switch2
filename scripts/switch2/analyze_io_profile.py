#!/usr/bin/env python3
"""Read buffered engine-I/O evidence; no changes to the capture or game files."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def fields(line):
    return dict(re.findall(r"([\w-]+)=([^ ]+)", line))


def stats(values):
    return {"calls": int(values["calls"]), "requested_bytes": int(values["requested"]),
            "actual_bytes": int(values["bytes"]), "total_ms": float(values["total-ms"]),
            "cursor_ms": float(values["cursor-ms"]), "work_ms": float(values["work-ms"]),
            "max_ms": float(values["max-ms"]), "errors": int(values["errors"])}


def traffic(values):
    return {k.replace("-", "_"): (float(values[k]) if k.endswith("-ms") else int(values[k]))
            for k in ["read-calls", "read-bytes", "read-ms", "write-calls", "write-bytes", "write-ms", "sync-calls", "sync-ms"] if k in values}


def analyze(path):
    raw = path.read_bytes()
    text = raw.decode("utf-8", errors="replace")
    if not text.startswith("io-profile version=1"):
        raise ValueError("Not a version 1 buffered I/O report")
    paths, threads = {}, {}
    frame_windows, windows, files, assets, caches, slow, frames, sizes = [], [], [], [], [], [], [], []
    for line in text.splitlines():
        v = fields(line)
        if line.startswith("[path]"):
            paths[int(v["file"])] = line.split(" name=", 1)[1]
        elif line.startswith("[thread]"):
            threads[int(v["id"])] = {"name": line.split(" name=", 1)[1].rsplit(" overflow=", 1)[0],
                                     "included": bool(int(v["included"])), "overflow": int(v["overflow"])}
        elif line.startswith("[frame-window]"):
            s = stats(v)
            s.update(traffic(v))
            count = int(v["frames"])
            s.update(start_s=float(v["start"][:-1]), frames=count, io_frames=int(v["io-frames"]),
                     scene_mask=int(v["scenes"], 16), frame_average_ms=float(v["frame-average-ms"]),
                     main_io_average_ms=s["total_ms"]/count, main_cursor_average_ms=s["cursor_ms"]/count,
                     calls_per_frame=s["calls"]/count)
            frame_windows.append(s)
        elif line.startswith("[io-window]"):
            windows.append({"thread": int(v["thread"]), "start_s": float(v["start"][:-1]),
                            "operation": v["op"], **stats(v)})
        elif line.startswith("[file-total]"):
            files.append({"thread": int(v["thread"]), "file": int(v["file"]),
                          "operation": v["op"], **stats(v)})
        elif line.startswith("[asset-window]"):
            assets.append({"thread": int(v["thread"]), "start_s": float(v["start"][:-1]),
                           "category": v["category"], "calls": int(v["calls"]),
                           "bytes": int(v["bytes"]), "total_ms": float(v["total-ms"])})
        elif line.startswith("[cache-window]"):
            caches.append({"thread": int(v["thread"]), "start_s": float(v["start"][:-1]),
                           "kind": v["kind"], "calls": int(v["calls"])})
        elif line.startswith("[read-sizes]"):
            sizes.append({"thread": int(v["thread"]), "start_s": float(v["start"][:-1]),
                          **{k: int(v[k]) for k in ["zero", "le256", "le4096", "le65536", "larger"]}})
        elif line.startswith("[slow-io]"):
            slow.append({"thread": int(v["thread"]), "at_s": float(v["at"][:-1]),
                         "frame": None if int(v["frame"]) == 2**64-1 else int(v["frame"]),
                         "file": int(v["file"]), "operation": v["op"], "offset": int(v["offset"]),
                         "asset": line.split(" asset=", 1)[1], **stats(v)})
        elif line.startswith("[io-frame]"):
            frames.append({"at_s": float(v["at"][:-1]), "frame": int(v["frame"]),
                           "scene": int(v["scene"]), "elapsed_ms": float(v["elapsed-ms"]), **stats(v), **traffic(v)})
    for row in files+slow:
        row["path"] = paths.get(row["file"], "<unknown>")
    totals = {}
    for row in files:
        dst = totals.setdefault(row["path"], {"calls": 0, "actual_bytes": 0, "total_ms": 0, "cursor_ms": 0})
        for key in dst:
            dst[key] += row[key]
    return {"capture": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
            "warning": "API wall times, not physical media/cache misses. Background and nested totals overlap. Main-frame data covers every captured frame; wait-profile detail remains sampled. Raw stdio/fstream and RAM Buffer operations excluded.",
            "threads": threads, "frame_windows": frame_windows, "io_windows": windows,
            "top_files": sorted(({"path": k, **v} for k, v in totals.items()), key=lambda r: r["total_ms"], reverse=True),
            "file_operations": files, "asset_windows": assets, "cache_windows": caches, "read_sizes": sizes,
            "worst_calls": sorted(slow, key=lambda r: r["total_ms"], reverse=True),
            "worst_io_frames": sorted(frames, key=lambda r: r["total_ms"], reverse=True),
            "capacity": next((line for line in text.splitlines() if line.startswith("dropped-threads=")), "missing")}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.capture), indent=2))
