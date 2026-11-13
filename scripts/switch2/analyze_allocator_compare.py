#!/usr/bin/env python3
"""Summarize SDK allocator/rpmalloc comparisons; no FPS projection."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import statistics

def fields(line):
    return dict(re.findall(r'([\w/-]+)=([^ ]+)',line))

def analyze(path):
    raw=path.read_bytes();text=raw.decode('utf-8',errors='replace')
    if not text.startswith('allocator-compare version=1'):raise ValueError('Expected allocator-compare version=1')
    rows=[];backing=[];checks=[];threads=[];build={};runtime={}
    for line in text.splitlines():
        f=fields(line)
        if line.startswith('[allocator-concurrency]') and 'aggregate-Mop/s' in f:
            rows.append({'allocator':f['allocator'],'workload':f['workload'],'threads':int(f['threads']),'trial':int(f['trial']),
                         'operations':int(f['operations']),'checks':int(f['checks']),'wall_ms':float(f['wall-ms']),
                         'aggregate_Mop_s':float(f['aggregate-Mop/s']),'main_Mop_s':float(f['main-Mop/s']),'verified':f['verified']=='true'})
        elif line.startswith('[backing]'):
            backing.append({k:v if k=='phase' else int(v) for k,v in f.items()})
        elif line.startswith(('[correctness]','[failure-tests]','[remote-stress]')):checks.append(line)
        elif line.startswith('[allocator-thread]'):threads.append(f)
        elif line.startswith('[allocator-build]'):build=f
        elif line.startswith('[allocator-runtime]'):runtime=f
    groups=collections.defaultdict(list)
    for r in rows:groups[r['allocator'],r['workload'],r['threads']].append(r)
    summary=[]
    for (allocator,workload,count),g in groups.items():
        summary.append({'allocator':allocator,'workload':workload,'threads':count,'trials':len(g),
                        'median_aggregate_Mop_s':statistics.median(r['aggregate_Mop_s'] for r in g),
                        'median_main_Mop_s':statistics.median(r['main_Mop_s'] for r in g)})
    finalized=next((r for r in backing if r['phase']=='finalized'),None)
    complete='finished status=0' in text and len(rows)==54 and all(r['verified'] for r in rows)
    complete&=len(checks)==5 and not any('FAILED' in s or 'verified=false' in s for s in checks)
    complete&=bool(finalized and finalized['charged-bytes']==0 and finalized['active']==0 and finalized['errors']==0)
    complete&=len(groups)==18 and all(len(g)==3 and {r['trial'] for r in g}=={1,2,3} for g in groups.values())
    query_errors=sum(int(t[k],0)!=0 for t in threads for k in ['id-result','priority-result','mask-result'])
    complete&=query_errors==0
    return {'capture':str(path.resolve()),'sha256':hashlib.sha256(raw).hexdigest(),'complete_and_verified':bool(complete),
            'warning':'Synthetic explicit API workloads; operations include allocation/free separately. Host CRT is not a Horizon allocator. rpmalloc backing comes from each process baseline allocator; those backing conditions differ across NROs. Charged backing is not process RSS. No FPS projection. Baseline before/after detects drift.',
            'build_metadata':build,'runtime_metadata':runtime,
            'summary':summary,'backing':backing,'correctness':checks,'thread_query_errors':query_errors,'thread_metadata':threads,'trials':rows}

def compare(paths):
    captures=[analyze(path) for path in paths]
    baselines=[c['build_metadata'].get('baseline') for c in captures]
    matching=len({(c['build_metadata'].get('sdk'),c['build_metadata'].get('cpu')) for c in captures})==1
    matching&=all(c['build_metadata'].get('sdk') and c['build_metadata'].get('cpu') for c in captures)
    complete=(len(captures)==2 and set(baselines)=={'sdk-mimalloc-before','newlib-dlmalloc-before'}
              and matching and all(c['complete_and_verified'] for c in captures))
    drift=[]
    for c in captures:
        baseline=c['build_metadata'].get('baseline','')
        before={(s['workload'],s['threads']):s for s in c['summary'] if s['allocator']==baseline}
        after={(s['workload'],s['threads']):s for s in c['summary'] if s['allocator']==baseline.removesuffix('-before')+'-after'}
        for key,s in before.items():
            if key in after:
                initial=s['median_aggregate_Mop_s'];final=after[key]['median_aggregate_Mop_s']
                change=100*(final/initial-1) if initial else None
                drift.append({'baseline':baseline,'workload':key[0],'threads':key[1],
                              'before_Mop_s':initial,'after_Mop_s':final,'change_percent':change,
                              'over_15_percent':change is not None and abs(change)>15})
    return {'complete_and_verified':bool(complete),'matching_sdk_and_cpu':bool(matching),
            'warning':'Run both NROs in the same power mode and similar thermal conditions. Before/after drift is diagnostic, not a complete control for clock, temperature or scheduling differences. Compare per-workload results; no universal allocator winner or FPS projection.',
            'baseline_drift':drift,'captures':captures}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path,nargs='+');args=p.parse_args()
    print(json.dumps(analyze(args.capture[0]) if len(args.capture)==1 else compare(args.capture),indent=2))
