#!/usr/bin/env python3
import concurrent.futures, importlib.util, json, os, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'tests/phase27/adversarial/cp-e-phase27-3-db0ce620'
CANDIDATE=ROOT/'build/compiler/moss-dev'
BASELINE=ROOT.parent/'cpe-base-04be1a/build/compiler/moss-opt'
REV='db0ce6203197726d3cd8eca01a0366d1d8283295'
spec=importlib.util.spec_from_file_location('reach',ROOT/'tests/tooling/check_phase27_reach_differential.py')
reach=importlib.util.module_from_spec(spec); spec.loader.exec_module(reach)

def as_rows(values): return [list(x) for x in sorted(values)]
def norm_target(value): return value.rsplit('__',1)[-1]
def main():
    sources=subprocess.check_output(['git','ls-tree','-r','--name-only',REV],cwd=ROOT,text=True).splitlines()
    sources=[s for s in sources if s.endswith('.moss')]
    def check(source):
        before=reach.trace(BASELINE,'off',ROOT,source,120)
        after=reach.trace(CANDIDATE,'shadow',ROOT,source,120)
        missing=before['checked']-after['checked']; added=after['checked']-before['checked']
        edge_missing=before['edges']-after['edges']; edge_added=after['edges']-before['edges']
        normal=lambda edges: sorted((owner,kind,norm_target(target)) for owner,kind,target in edges)
        norm_missing=sorted(normal(before['edges']) if False else [])
        # Multiset comparison is needed because unrelated same-leaf targets can coexist.
        from collections import Counter
        nm=Counter(normal(before['edges']))-Counter(normal(after['edges']))
        na=Counter(normal(after['edges']))-Counter(normal(before['edges']))
        return {'entry':source,'legacy_complete':before['trace_complete'],'tree_complete':after['trace_complete'],
          'legacy_verdict':before['verdict'],'tree_verdict':after['verdict'],
          'checked_missing':as_rows(missing),'checked_added':as_rows(added),
          'edge_missing':as_rows(edge_missing),'edge_added':as_rows(edge_added),
          'edge_missing_after_leaf_normalization':list(nm.elements()),
          'edge_added_after_leaf_normalization':list(na.elements()),
          'legacy_checked':as_rows(before['checked']),'tree_checked':as_rows(after['checked'])}
    output=OUT/'closure.jsonl'; rows=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool, output.open('w') as f:
        futures={pool.submit(check,s):s for s in sources}
        for i,future in enumerate(concurrent.futures.as_completed(futures),1):
            row=future.result(); rows.append(row)
            f.write(json.dumps(row,separators=(',',':'))+'\n'); f.flush()
            if i%100==0: print(f'completed {i}/{len(sources)}',file=sys.stderr,flush=True)
    summary={'candidate':REV,'baseline':'04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4','source_count':len(rows),
       'accepted_baseline':sum(r['legacy_verdict'][0] is True for r in rows),
       'accepted_candidate':sum(r['tree_verdict'][0] is True for r in rows),
       'incomplete_traces':[r['entry'] for r in rows if not r['legacy_complete'] or not r['tree_complete']],
       'verdict_diffs':[r['entry'] for r in rows if r['legacy_verdict']!=r['tree_verdict']],
       'checked_legacy_only':[{'entry':r['entry'],'values':r['checked_missing']} for r in rows if r['checked_missing']],
       'checked_tree_only':[{'entry':r['entry'],'values':r['checked_added']} for r in rows if r['checked_added']],
       'edge_diff_entries':[r['entry'] for r in rows if r['edge_missing'] or r['edge_added']],
       'normalized_edge_diff_entries':[r['entry'] for r in rows if r['edge_missing_after_leaf_normalization'] or r['edge_added_after_leaf_normalization']]}
    (OUT/'closure-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:v for k,v in summary.items() if k not in ('checked_legacy_only','checked_tree_only')},indent=2))
    print('checked_legacy_only=',json.dumps(summary['checked_legacy_only']))
    print('checked_tree_only=',json.dumps(summary['checked_tree_only']))
if __name__=='__main__': raise SystemExit(main())
