#!/usr/bin/env python3
"""Compare candidate checker verdicts and diagnostics in EST off/shadow."""
import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[4]
COMPILER=(ROOT/'moss').resolve()
OUT=ROOT/'tests/phase27/adversarial/cp-e-55994899/candidate-mode-matrix.jsonl'
listed=subprocess.check_output(['git','ls-files','*.moss'],cwd=ROOT,text=True).splitlines()
inputs=sorted(p for p in listed if (ROOT/p).is_file())
missing=sorted(p for p in listed if not (ROOT/p).is_file())
def one(path):
    cells=[]
    for mode in ('off','shadow'):
        env=os.environ.copy(); env['MOSS_EST']=mode; env['MOSS_PHASE26_C1_SCOPE_TRACE']='1'
        try:
            r=subprocess.run([str(COMPILER),'check',str(ROOT/path),'--json'],cwd=ROOT,env=env,
              text=True,capture_output=True,timeout=60)
            try: p=json.loads(r.stdout)
            except json.JSONDecodeError: p={}
            e=p.get('error') or {}
            cells.append({'rc':r.returncode,'ok':p.get('ok'),'code':e.get('code'),
                          'message':e.get('message'),'line':e.get('line'),'column':e.get('column'),
                          'json':bool(p),
                          'trace_complete':'MOSS_C1_SCOPE_TRACE_COMPLETE|' in r.stderr,
                          'checked':sorted(row.split('|',3)[3] for row in r.stderr.splitlines() if row.startswith('MOSS_C1_SCOPE_ID|')),
                          'edges':sorted(row.split('|',3)[1:] for row in r.stderr.splitlines() if row.startswith('MOSS_C1_SCOPE_EDGE|'))})
        except subprocess.TimeoutExpired:
            cells.append({'timeout':True})
    return {'source':path,'off':cells[0],'shadow':cells[1]}
with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 4,8)) as pool:
    rows=list(pool.map(one,inputs))
OUT.write_text(''.join(json.dumps(x,sort_keys=True)+'\n' for x in rows))
def diagnostic(x):
    return tuple(x.get(key) for key in ('rc','ok','code','message','line','column','json'))
verdict=[r for r in rows if diagnostic(r['off'])[:2]!=diagnostic(r['shadow'])[:2]]
diagnostics=[r for r in rows if diagnostic(r['off'])!=diagnostic(r['shadow'])]
closures=[r for r in rows if r['off'].get('checked')!=r['shadow'].get('checked')]
edges=[r for r in rows if r['off'].get('edges')!=r['shadow'].get('edges')]
topology=[r for r in rows if {tuple(x[:2]) for x in r['off'].get('edges',[])} !=
          {tuple(x[:2]) for x in r['shadow'].get('edges',[])}]
summary={'listed':len(listed),'available':len(inputs),'missing':len(missing),
         'verdict_differences':len(verdict),'diagnostic_differences':len(diagnostics),
         'checked_id_closure_differences':len(closures),
         'checked_id_closure_difference_sources':[r['source'] for r in closures],
         'edge_target_label_difference_sources':len(edges),
         'edge_owner_kind_topology_differences':len(topology),
         'complete_traces_off':sum(bool(r['off'].get('trace_complete')) for r in rows),
         'complete_traces_shadow':sum(bool(r['shadow'].get('trace_complete')) for r in rows),
         'available_sources_sha256':hashlib.sha256('\n'.join(inputs).encode()).hexdigest(),
         'matrix_sha256':hashlib.sha256(OUT.read_bytes()).hexdigest(),
         'missing_paths_sha256':hashlib.sha256('\n'.join(missing).encode()).hexdigest(),
         'note':'candidate EST off compared with candidate EST shadow'}
(ROOT/'docs/phase27/adversarial/cp-e-55994899/candidate-mode-matrix-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,sort_keys=True))
