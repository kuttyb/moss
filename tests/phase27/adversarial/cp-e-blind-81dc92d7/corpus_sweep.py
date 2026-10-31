#!/usr/bin/env python3
import concurrent.futures, json, os, pathlib, subprocess, sys
root = pathlib.Path(__file__).resolve().parents[4]
candidate = root / 'build/compiler/moss-opt'
baseline = root.parent / 'cp-e-baseline/build/compiler/moss-opt'
out = pathlib.Path(__file__).resolve().parent
exclude = ('tests/phase27/adversarial/', 'tests/tooling/phase20_review/', 'build/', 'target/')
paths = []
for top in ('tests', 'examples', 'benchmarks', 'sdk'):
    base = root / top
    if base.exists():
        paths += [p for p in base.rglob('*.moss') if not any(x in p.relative_to(root).as_posix() for x in exclude)]
paths = sorted(set(paths))
def check(p, compiler, mode):
    env = dict(os.environ, MOSS_EST=mode)
    r = subprocess.run([str(compiler), 'check', str(p), '--json'], cwd=root,
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True, timeout=12)
    def scrub(s):
        return s.replace(str(root / 'tmp/cp-e-blind-81dc92d7'), '<WORKTREE>').replace(str(root / 'tmp/cp-e-baseline'), '<WORKTREE>')
    try: data = json.loads(r.stdout)
    except Exception: data = {'parse_error': True, 'stdout': scrub(r.stdout), 'stderr': scrub(r.stderr), 'rc': r.returncode}
    return {'rc':r.returncode,'json':data,'stderr':scrub(r.stderr)}
def one(p):
    res = {'source':p.relative_to(root).as_posix()}
    for key, comp, mode in [('baseline-off',baseline,'off'),('baseline-shadow',baseline,'shadow'),('candidate-off',candidate,'off'),('candidate-shadow',candidate,'shadow')]:
        try: res[key] = check(p,comp,mode)
        except subprocess.TimeoutExpired: res[key] = {'timeout':True}
    return res
rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for row in pool.map(one, paths): rows.append(row)
def verdict(x):
    j=x.get('json',{})
    err=j.get('error')
    if err:
        return {'ok':False,'diagnostic':{k:err.get(k) for k in ('code','line','column','message','semantic_identity')}}
    return {'ok':j.get('ok',x.get('rc')==0),'diagnostics':j.get('diagnostics',[]),'parse_error':j.get('parse_error')}
diffs=[]; timeouts=0; accepted=0
for r in rows:
    modes={k:verdict(r[k]) for k in ('baseline-off','baseline-shadow','candidate-off','candidate-shadow')}
    if modes['baseline-off'].get('ok'): accepted += 1
    if any(r[k].get('timeout') for k in ('baseline-off','baseline-shadow','candidate-off','candidate-shadow')): timeouts+=1
    if len({json.dumps(v,sort_keys=True) for v in modes.values()}) > 1:
        diffs.append({'source':r['source'],'verdicts':modes})
    r['normalized']=modes
(out/'corpus-raw.jsonl').write_text(''.join(json.dumps(r,sort_keys=True)+'\n' for r in rows))
(out/'corpus-summary.json').write_text(json.dumps({'candidate':str(candidate),'baseline':str(baseline),'sources':len(rows),'baseline_accepted':accepted,'timeouts':timeouts,'differences':diffs},indent=2)+'\n')
print(json.dumps({'sources':len(rows),'baseline_accepted':accepted,'timeouts':timeouts,'differences':len(diffs)}))
