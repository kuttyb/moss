import json, os, pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[2]
base=root.parent/'cp-e-baseline-04be1a1'
cand=root
files=[]
for pattern in ('tests/**/*.moss','examples/**/*.moss','benchmarks/**/*.moss','sdk/**/*.moss'):
    files.extend(root.glob(pattern))
files=sorted(set(p.resolve() for p in files if not any(part in ('adversarial','tmp') for part in p.relative_to(root).parts) and (root.parent/'cp-e-baseline-04be1a1'/p.relative_to(root)).is_file()))
rows=[]
def check(tree, source, mode):
    env=os.environ.copy(); env['MOSS_EST']=mode
    bin=tree/'build/compiler/moss-opt'
    rel=source.relative_to(root)
    run=subprocess.run([str(bin),'check',str(tree/rel),'--json'],cwd=tree,env=env,text=True,capture_output=True)
    try: payload=json.loads(run.stdout)
    except Exception: payload={'ok':False,'error':{'code':'NON_JSON','message':run.stderr[:500]}}
    err=payload.get('error') or {}
    return {'ok':bool(payload.get('ok')),'code':err.get('code'),'message':err.get('message'),'line':err.get('line'),'column':err.get('column'),'returncode':run.returncode}
for source in files:
    row={'path':source.relative_to(root).as_posix()}
    for label,tree,mode in [('baseline_off',base,'off'),('candidate_off',cand,'off'),('candidate_shadow',cand,'shadow')]:
        row[label]=check(tree,source,mode)
    rows.append(row)
path=root/'tmp/cp-e-evidence/corpus.jsonl'
path.write_text(''.join(json.dumps(x,sort_keys=True)+'\n' for x in rows))
summary={'total':len(rows),'baseline_candidate_off_diffs':sum(r['baseline_off']!=r['candidate_off'] for r in rows),'candidate_off_shadow_diffs':sum(r['candidate_off']!=r['candidate_shadow'] for r in rows),'failures':sum(not r['candidate_off']['ok'] for r in rows)}
(root/'tmp/cp-e-evidence/corpus-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary))
