#!/usr/bin/env python3
"""Decode sampled allocation caller PCs using the EXACT corresponding ARM64 ELF."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path


def fields(line):
    return dict(re.findall(r"([\w-]+)=([^ ]+)",line))


def tool(name,prefix):
    candidate=Path(str(prefix)+name)
    found=shutil.which(str(candidate))
    if found:return found
    if candidate.exists():return str(candidate)
    raise ValueError(f"Missing tool: {candidate}")


def symbols(elf,offsets,prefix):
    with elf.open("rb") as f:header=f.read(20)
    if header[:4]!=b"\x7fELF" or struct.unpack_from("<H",header,18)[0]!=183:
        raise ValueError("Expected AArch64 ELF")
    nm=subprocess.check_output([tool("nm",prefix),"--defined-only",str(elf)],text=True)
    match=re.search(r"^([0-9a-fA-F]+)\s+\w\s+starAllocationProfileAnchor$",nm,re.M)
    if not match:raise ValueError("ELF has no allocation-profile anchor; use the matching diagnostic ELF")
    anchor=int(match[1],16)
    addresses=sorted({anchor+offset-4 for offset in offsets if anchor+offset>=4})
    decoded={}
    for start in range(0,len(addresses),128):
        output=subprocess.check_output([tool("addr2line",prefix),"-a","-f","-C","-i","-e",str(elf),*[hex(a) for a in addresses[start:start+128]]],text=True,errors="replace")
        current=None;lines=[]
        def flush():
            if current is not None:decoded[current]=[{"function":lines[i],"location":lines[i+1]} for i in range(0,len(lines)-1,2)]
        for line in output.splitlines():
            if re.fullmatch(r"0x[0-9a-fA-F]+",line):
                flush();current=int(line,16);lines=[]
            else:lines.append(line)
        flush()
    return anchor,{offset:decoded.get(anchor+offset-4,[]) for offset in offsets}


def analyze(path,elf=None,prefix="aarch64-none-elf-",manifest=None):
    raw=path.read_bytes();text=raw.decode("utf-8",errors="replace")
    if not text.startswith("allocation-callers version=1"):raise ValueError("Not an allocation-callers version 1 report")
    api=[];callers=[];slow=[];threads=[];windows=[]
    meta=fields(text.splitlines()[1])
    for line in text.splitlines():
        v=fields(line)
        if line.startswith("[thread-affinity]"):
            threads.append({"id":int(v["id"]),"mask":v["mask"],"preferred":int(v["preferred"]),"priority":int(v["priority"]),
                            "affinity_result":v["affinity-result"],"priority_result":v["priority-result"],"name":line.split(" name=",1)[1]})
        elif line.startswith("[allocation-window]"):
            windows.append({"start_s":float(v["start"][:-1]),"frames":int(v["frames"]),"detailed_frames":int(v["detailed"]),"scene_mask":int(v["scenes"],16),"scene":int(v["scene"]),"calls":int(v["calls"]),"total_ms":float(v["total-ms"]),"max_ms":float(v["max-ms"])})
        elif line.startswith("[api]"):
            api.append({"scene":int(v.get("scene",0)),"stage":v["stage"],"kind":v["kind"],"calls":int(v["calls"]),"size_arguments":int(v["size-arguments"]),
                        "total_ms":float(v.get("total-ms",0)),"max_ms":float(v.get("max-ms",0)),"null_results":int(v.get("null-results",0))})
        elif line.startswith("[caller]"):
            callers.append({"delta":int(v["delta"]),"scene":int(v.get("scene",0)),"stage":v["stage"],"kind":v["kind"],"samples":int(v["samples"]),
                            "sampled_total_ms":float(v["total-ms"]),"sampled_max_ms":float(v["max-ms"]),
                            "sampled_size_arguments":int(v["size-arguments"]),"sampled_null_results":int(v["null-results"])})
        elif line.startswith("[slow]"):
            slow.append({"frame":int(v["frame"]),"scene":int(v["scene"]),"delta":int(v["delta"]),
                         "stage":v["stage"],"kind":v["kind"],"milliseconds":float(v["milliseconds"]),
                         "size_argument":int(v["size-argument"]),"nonnull_or_void_success":bool(int(v["result-nonnull-or-void-success"]))})
    info=None
    if elf:
        digest=hashlib.sha256(elf.read_bytes()).hexdigest()
        if manifest:
            expected=json.loads(manifest.read_text())["elf_sha256"]
            if expected!=digest:raise ValueError("ELF does not match manifest")
        anchor,resolved=symbols(elf,{r["delta"] for r in callers+slow},prefix)
        for row in callers+slow:row["source_frames"]=resolved.get(row["delta"],[])
        info={"elf":str(elf.resolve()),"sha256":digest,"anchor":hex(anchor)}
    return {"capture":str(path.resolve()),"sha256":hashlib.sha256(raw).hexdigest(),"metadata":meta,"symbols":info,
            "clock_calibration":next((line for line in text.splitlines() if line.startswith("clock-pair-calibration")),None),
            "warning":"Samples identify direct return-address call sites, including inline frames; not complete stacks or CPU-only time. No extrapolation to total frame cost. Root API rows are exclusive by nearest scope. Size arguments are not live memory.",
            "thread_affinity":threads,"allocation_windows":windows,"api_totals":api,"callers":callers,"top_by_samples":sorted(callers,key=lambda r:r["samples"],reverse=True)[:30],
            "top_by_sampled_time":sorted(callers,key=lambda r:r["sampled_total_ms"],reverse=True)[:30],
            "slow_samples":sorted(slow,key=lambda r:r["milliseconds"],reverse=True)}


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture",type=Path);parser.add_argument("--elf",type=Path)
    parser.add_argument("--manifest",type=Path);parser.add_argument("--tool-prefix",default="aarch64-none-elf-")
    args=parser.parse_args()
    print(json.dumps(analyze(args.capture,args.elf,args.tool_prefix,args.manifest),indent=2))
