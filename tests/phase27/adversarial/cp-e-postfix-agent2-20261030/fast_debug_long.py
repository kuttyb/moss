#!/usr/bin/env python3
import hashlib,json,os,pathlib,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[4]
BASE_ROOT=ROOT/'tmp/cp-e-postfix-baseline-archive'; CAND_ROOT=ROOT/'tmp/cp-e-postfix-candidate-archive'
configs=[('baseline',BASE_ROOT,pathlib.Path(sys.argv[1]).resolve(),'off'),('candidate',CAND_ROOT,pathlib.Path(sys.argv[2]).resolve(),'shadow')]
fixtures=['examples/functional_pipeline.moss','examples/frontend_syntax.moss']
out=ROOT/'tests/phase27/adversarial/cp-e-postfix-agent2-20261030/fast-debug-long.json'
rows=[]
for fixture in fixtures:
  source=(BASE_ROOT/fixture).read_bytes()
  same=(CAND_ROOT/fixture).read_bytes()
  if source!=same: raise SystemExit(f'fixture content differs across roots: {fixture}')
  for label,cwd,binary,mode in configs:
    env=os.environ.copy(); env['MOSS_EST']=mode
    start=time.monotonic()
    try:
      p=subprocess.run([str(binary),'debug',fixture],cwd=cwd,env=env,text=True,capture_output=True,timeout=600)
      row={'fixture':fixture,'content_sha256':hashlib.sha256(source).hexdigest(),'compiler':label,'mode':mode,
           'outcome':'FINISHED','returncode':p.returncode,'elapsed_seconds':round(time.monotonic()-start,2),
           'stdout':p.stdout,'stderr':p.stderr,'stdout_sha256':hashlib.sha256(p.stdout.encode()).hexdigest(),
           'stderr_sha256':hashlib.sha256(p.stderr.encode()).hexdigest()}
    except subprocess.TimeoutExpired as e:
      row={'fixture':fixture,'content_sha256':hashlib.sha256(source).hexdigest(),'compiler':label,'mode':mode,
           'outcome':'TIMEOUT_600','elapsed_seconds':round(time.monotonic()-start,2),
           'stdout_tail':(e.stdout or b'').decode(errors='replace')[-2000:] if isinstance(e.stdout,bytes) else (e.stdout or '')[-2000:],
           'stderr_tail':(e.stderr or b'').decode(errors='replace')[-2000:] if isinstance(e.stderr,bytes) else (e.stderr or '')[-2000:]}
    rows.append(row); out.write_text(json.dumps(rows,indent=2)+'\n')
    print(json.dumps({k:row[k] for k in ('fixture','compiler','mode','outcome','elapsed_seconds')}),flush=True)
for fixture in fixtures:
  pair=[r for r in rows if r['fixture']==fixture]
  if len(pair)==2 and all(r['outcome']=='TIMEOUT_600' for r in pair):
    for r in pair: r['classification']='BOTH-TIMEOUT; excluded; no completed output on either side'
  elif len(pair)==2 and all(r['outcome']=='FINISHED' for r in pair):
    same=(pair[0]['stdout']==pair[1]['stdout'] and pair[0]['stderr']==pair[1]['stderr'])
    for r in pair: r['classification']='MATCH' if same else 'OUTPUT_DIFFERENCE'
out.write_text(json.dumps(rows,indent=2)+'\n')
