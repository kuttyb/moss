#!/usr/bin/env python3
"""Baseline-off vs frozen candidate-off checker and closure matrix."""
import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[4]
BASELINE=pathlib.Path(sys.argv[1]).resolve()
CANDIDATE=(ROOT/'moss').resolve()
EXPECTED='4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286'
actual=hashlib.sha256(BASELINE.read_bytes()).hexdigest()
if actual!=EXPECTED: raise SystemExit(f'baseline SHA mismatch: {actual}')
OUT=ROOT/'tests/phase27/adversarial/cp-e-55994899/baseline-candidate-matrix.jsonl'
listed=subprocess.check_output(['git','ls-files','*.moss'],cwd=ROOT,text=True).splitlines()
inputs=sorted(p for p in listed if (ROOT/p).is_file())
missing=sorted(p for p in listed if not (ROOT/p).is_file())
def run(binary,path):
    env=os.environ.copy(); env.update({'MOSS_EST':'off','MOSS_PHASE26_C1_SCOPE_TRACE':'1'})
    try:
        r=subprocess.run([str(binary),'check',str(ROOT/path),'--json'],cwd=ROOT,env=env,
                         text=True,capture_output=True,timeout=60)
        try: p=json.loads(r.stdout)
        except json.JSONDecodeError: p={}
        e=p.get('error') or {}
        return {'rc':r.returncode,'ok':p.get('ok'),'code':e.get('code'),
                'message':e.get('message'),'line':e.get('line'),'column':e.get('column'),
                'json':bool(p),'trace_complete':'MOSS_C1_SCOPE_TRACE_COMPLETE|' in r.stderr,
                'checked':sorted(row.split('|',3)[3] for row in r.stderr.splitlines() if row.startswith('MOSS_C1_SCOPE_ID|'))}
    except subprocess.TimeoutExpired:
        return {'timeout':True}
def one(path):
    return {'source':path,'baseline':run(BASELINE,path),'candidate':run(CANDIDATE,path)}
with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 4,8)) as pool:
    rows=list(pool.map(one,inputs))
OUT.write_text(''.join(json.dumps(r,sort_keys=True)+'\n' for r in rows))
def signature(x): return tuple(x.get(k) for k in ('rc','ok','code','message','line','column','json'))
verdict=[r for r in rows if signature(r['baseline'])[:2]!=signature(r['candidate'])[:2]]
diag=[r for r in rows if signature(r['baseline'])!=signature(r['candidate'])]
closure=[r for r in rows if r['baseline'].get('checked')!=r['candidate'].get('checked')]
trace=[r for r in rows if r['baseline'].get('trace_complete')!=r['candidate'].get('trace_complete')]
summary={'baseline_sha256':actual,'baseline_commit':'04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4',
 'candidate_commit':'55994899539532c763d6489b16b859f419b12205','listed_sources':len(listed),
 'available_sources':len(inputs),'missing_sources':len(missing),'verdict_differences':len(verdict),
 'diagnostic_differences':len(diag),'checked_id_closure_differences':len(closure),
 'trace_completion_differences':len(trace),'baseline_complete_traces':sum(r['baseline'].get('trace_complete',False) for r in rows),
 'candidate_complete_traces':sum(r['candidate'].get('trace_complete',False) for r in rows),
 'available_sources_sha256':hashlib.sha256('\n'.join(inputs).encode()).hexdigest(),
 'matrix_sha256':hashlib.sha256(OUT.read_bytes()).hexdigest(),
 'verdict_difference_sources':[r['source'] for r in verdict],
 'diagnostic_difference_sources':[r['source'] for r in diag],
 'closure_difference_sources':[r['source'] for r in closure],
 'trace_completion_difference_sources':[r['source'] for r in trace]}
(ROOT/'docs/phase27/adversarial/cp-e-55994899/baseline-candidate-matrix-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,sort_keys=True))
