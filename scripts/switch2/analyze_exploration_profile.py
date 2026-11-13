"""Parse the bounded exploration capture; phase times overlap and are not CPU-only."""
import argparse,hashlib,json,re
from pathlib import Path

def analyze(path):
    raw=path.read_bytes();lines=raw.decode('utf8',errors='replace').splitlines()
    phases=[];counters=[];slow=[];frames=[];named=[]
    for line in lines:
        values=dict(re.findall(r'([\w-]+)=([^ |]+)',line.split(' label=',1)[0]))
        if line.startswith('[stats-name]'):
            named.append({'kind':values['kind'],'calls':int(values['calls']),'total_ms':float(values['total-ms']),'max_ms':float(values['max-ms']),'label':line.split(' label=',1)[1]})
        elif line.startswith('[phase]'):
            phases.append({'start_s':float(values['start'][:-1]),'frames':int(values['frames']),'scene_mask':int(values['scenes'],16),'kind':values['kind'],'calls':int(values['calls']),'total_ms':float(values['total-ms']),'max_ms':float(values['max-ms'])})
        elif line.startswith('[counter]'):
            counters.append({'start_s':float(values['start'][:-1]),'kind':values['kind'],'total':int(values['total'])})
        elif line.startswith('[slow]'):
            slow.append({'at_s':float(values['at'][:-1]),'frame':int(values['frame']),'scene':int(values['scene']),'kind':values['kind'],'elapsed_ms':float(values['elapsed-ms']),'detail':int(values['detail']),'label':line.split(' label=',1)[1]})
        elif line.startswith('[frame]'):
            parts=line.split(' | ');metrics={}
            for part in parts[1:]:
                name,value=part.split('=',1);ms,calls=value.split('ms/');metrics[name]={'total_ms':float(ms),'calls':int(calls)}
            frames.append({'at_s':float(values['at'][:-1]),'frame':int(values['frame']),'scene':int(values['scene']),'elapsed_ms':float(values['elapsed-ms']),'phases':metrics})
    totals={}
    for row in phases:
        t=totals.setdefault(row['kind'],{'calls':0,'total_ms':0,'max_ms':0})
        t['calls']+=row['calls'];t['total_ms']+=row['total_ms'];t['max_ms']=max(t['max_ms'],row['max_ms'])
    return {'capture':str(path.resolve()),'sha256':hashlib.sha256(raw).hexdigest(),'header':lines[:3],
            'warning':'Main-thread wall times; nested phase totals overlap. Tile hashes sampled; other tagged scopes all frames. Slow records retain the largest calls, not every call. Match wait/I/O captures by frame ID.',
            'phase_totals':totals,'phase_windows':phases,'counter_windows':counters,
            'named_scope_totals':sorted(named,key=lambda x:x['total_ms'],reverse=True),'named_statistics':sorted([r for r in named if r['kind'].startswith('stats-')],key=lambda x:x['total_ms'],reverse=True),'named_statistics_capacity':next((x for x in lines if x.startswith('stats-name-capacity=')),None),
            'slow_calls':sorted(slow,key=lambda x:x['elapsed_ms'],reverse=True),
            'worst_frames':sorted(frames,key=lambda x:x['elapsed_ms'],reverse=True)}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);args=p.parse_args()
    print(json.dumps(analyze(args.capture),indent=2))
