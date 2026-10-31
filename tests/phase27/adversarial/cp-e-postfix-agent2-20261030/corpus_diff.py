#!/usr/bin/env python3
import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[4]
BASE=pathlib.Path(sys.argv[1]).resolve()
CAND=pathlib.Path(sys.argv[2]).resolve()
OUT=pathlib.Path(sys.argv[3]).resolve()
paths=subprocess.check_output(['git','ls-files','-z','*.moss'],cwd=ROOT).decode().split('\0')
paths=sorted(p for p in paths if p and not p.startswith('tests/phase27/adversarial/') and (ROOT/p).is_file())
work=[]
for rel in paths:
    source=ROOT/rel
    for mode in ('off','shadow'):
        for label,compiler in (('baseline',BASE),('candidate',CAND)):
            work.append((label,mode,rel,str(source),str(compiler)))
def run(row):
    label,mode,rel,source,compiler=row
    env=os.environ.copy(); env['MOSS_EST']=mode
    try:
        r=subprocess.run([compiler,'check',source,'--json'],cwd=ROOT,env=env,text=True,
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
        try:
            payload=json.loads(r.stdout)
            canonical=json.dumps(payload,sort_keys=True,separators=(',',':'))
            error=payload.get('error',{})
            code=error.get('code','') if isinstance(error,dict) else ''
            message=error.get('message','') if isinstance(error,dict) else str(error)
            loc=[error.get(k) for k in ('line','column','end_line','end_column')] if isinstance(error,dict) else []
            sig=f'{r.returncode}\0{canonical}'
            err=''
        except Exception as e:
            payload=None; code='NON_JSON'; message=''; loc=[]; sig=f'{r.returncode}\0{r.stdout}\0{r.stderr}'; err=repr(e)
        digest=hashlib.sha256(sig.encode()).hexdigest()
        return [label,mode,rel,str(r.returncode),digest,code,message,*loc,err, r.stderr[:3000] if code=='NON_JSON' else '']
    except subprocess.TimeoutExpired:
        return [label,mode,rel,'TIMEOUT','','TIMEOUT','','','','','','','']
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    rows=list(pool.map(run,work))
headers=['compiler','mode','path','returncode','sha256','diagnostic_code','diagnostic_message','line','column','end_line','end_column','parse_error','stderr']
with OUT.open('w') as f:
    f.write('\t'.join(headers)+'\n')
    for row in rows:
        f.write('\t'.join(str(v).replace('\t',' ').replace('\n','\\n') for v in row)+'\n')
idx={(r[0],r[1],r[2]):r for r in rows}
diffs=[]
for mode in ('off','shadow'):
  for rel in paths:
    b=idx['baseline',mode,rel]; c=idx['candidate',mode,rel]
    if b[3:5] != c[3:5]: diffs.append({'mode':mode,'path':rel,'baseline':b,'candidate':c})
(OUT.with_suffix('.diffs.json')).write_text(json.dumps({'files':len(paths),'checks':len(rows),'differences':diffs},indent=2)+'\n')
print(json.dumps({'files':len(paths),'checks':len(rows),'differences':len(diffs),'timeout_or_nonjson':sum(r[3]=='TIMEOUT' or r[5]=='NON_JSON' for r in rows)},sort_keys=True))
