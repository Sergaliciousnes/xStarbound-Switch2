"""Read-only SBAsset6 metadata/index/bounds audit; does not execute mod scripts."""
import argparse,hashlib,json,struct
from pathlib import Path

def audit(path):
    size=path.stat().st_size
    with path.open('rb') as f:
        digest=hashlib.file_digest(f,'sha256').hexdigest();f.seek(0)
        def read(n):
            if n>size-f.tell():raise ValueError('Read exceeds archive bounds')
            b=f.read(n)
            if len(b)!=n:raise ValueError('Truncated read')
            return b
        def vlq():
            n=0
            for _ in range(10):
                b=read(1)[0];n=(n<<7)|(b&127)
                if not b&128:return n
            raise ValueError('Invalid VLQ')
        def text():return read(vlq()).decode('utf8')
        def obj(depth):
            result={}
            for _ in range(vlq()):
                k=text();result[k]=value(depth+1)
            return result
        def value(depth):
            if depth>128:raise ValueError('Metadata nesting exceeds audit limit')
            t=max(0,read(1)[0]-1)
            if t==0:return None
            if t==1:return struct.unpack('>d',read(8))[0]
            if t==2:return bool(read(1)[0])
            if t==3:
                n=vlq();return -(n>>1)-1 if n&1 else n>>1
            if t==4:return text()
            if t==5:return [value(depth+1) for _ in range(vlq())]
            if t==6:return obj(depth+1)
            raise ValueError('Unknown metadata type')
        if read(8)!=b'SBAsset6':raise ValueError('Invalid SBAsset6 header')
        start=struct.unpack('>Q',read(8))[0]
        if not 16<=start<=size-5:raise ValueError('Invalid index offset')
        f.seek(start)
        if read(5)!=b'INDEX':raise ValueError('Invalid INDEX header')
        metadata=obj(0);metadata_end=f.tell();entries=vlq();duplicates=0;keys=set()
        for _ in range(entries):
            name=text();offset,length=struct.unpack('>QQ',read(16))
            if not 16<=offset<=start or length>start-offset:
                raise ValueError('Entry out of bounds: '+name)
            if name in keys:duplicates+=1
            keys.add(name)
        return {'file':path.name,'bytes':size,'sha256':digest,'index_start':start,
                'index_bytes':size-start,'metadata_bytes':metadata_end-start,
                'name':metadata.get('name'),'entries':entries,'duplicate_paths':duplicates,
                'trailing_bytes':size-f.tell(),'verified':True}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    rows=[]
    for path in sorted(a.directory.glob('*.pak')):
        try:rows.append(audit(path))
        except Exception as e:rows.append({'file':path.name,'verified':False,'error':str(e)})
    report={'archive_count':len(rows),'total_bytes':sum(r.get('bytes',0) for r in rows),
            'total_index_entries':sum(r.get('entries',0) for r in rows),'failed':sum(not r['verified'] for r in rows),'archives':rows}
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='archives'},indent=2))
    raise SystemExit(bool(report['failed']))
