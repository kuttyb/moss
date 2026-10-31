#!/usr/bin/env python3
import concurrent.futures, hashlib, json, os, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
CANDIDATE=ROOT/'build/compiler/moss-dev'
BASELINE=ROOT.parent/'cpe-base-04be1a/build/compiler/moss-opt'
OUT=ROOT/'tests/phase27/adversarial/cp-e-phase27-3-db0ce620'
REV='db0ce6203197726d3cd8eca01a0366d1d8283295'

def run(binary, mode, source):
    env=os.environ.copy(); env['MOSS_EST']=mode
    try:
        p=subprocess.run([str(binary),'check',source,'--json'],cwd=ROOT,env=env,
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=120)
    except subprocess.TimeoutExpired as e:
        return {'verdict':('TIMEOUT',None,None),'returncode':None,
                'stderr':str(e),'stdout_hash':None}
    try:
        result=json.loads(p.stdout); err=result.get('error') or {}
        verdict=(result.get('ok'),err.get('code'),err.get('line'))
    except Exception:
        verdict=('NOJSON',p.returncode,None)
    return {'verdict':verdict,'returncode':p.returncode,
            'stderr':p.stderr if verdict[0]=='NOJSON' else '',
            'stdout_hash':hashlib.sha256(p.stdout.encode()).hexdigest()}

def main():
    sources=subprocess.check_output(['git','ls-tree','-r','--name-only',REV],cwd=ROOT,text=True).splitlines()
    sources=[s for s in sources if s.endswith('.moss')]
    def check(source):
        before=run(BASELINE,'off',source)
        off=run(CANDIDATE,'off',source)
        shadow=run(CANDIDATE,'shadow',source)
        return {'source':source,'baseline_off':before,'candidate_off':off,'candidate_shadow':shadow}
    rows=[]
    output=OUT/'corpus.jsonl'
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool, output.open('w') as f:
        futures={pool.submit(check,source):source for source in sources}
        for i,future in enumerate(concurrent.futures.as_completed(futures),1):
            row=future.result(); rows.append(row)
            f.write(json.dumps(row,separators=(',',':'))+'\n'); f.flush()
            if i % 100 == 0: print(f'completed {i}/{len(sources)}',file=sys.stderr,flush=True)
    summary={'candidate':REV,'source_count':len(rows),
      'baseline_accepted':sum(r['baseline_off']['verdict'][0] is True for r in rows),
      'candidate_accepted':sum(r['candidate_off']['verdict'][0] is True for r in rows),
      'candidate_shadow_accepted':sum(r['candidate_shadow']['verdict'][0] is True for r in rows),
      'off_diffs':[r for r in rows if r['baseline_off']['verdict']!=r['candidate_off']['verdict']],
      'shadow_diffs':[r for r in rows if r['candidate_off']['verdict']!=r['candidate_shadow']['verdict']],
      'timeouts':sum(1 for r in rows if any(r[k]['verdict'][0]=='TIMEOUT' for k in ('baseline_off','candidate_off','candidate_shadow')))}
    (OUT/'corpus-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f"sources={len(rows)} baseline accepted={summary['baseline_accepted']} candidate off accepted={summary['candidate_accepted']} shadow accepted={summary['candidate_shadow_accepted']} off_diffs={len(summary['off_diffs'])} shadow_diffs={len(summary['shadow_diffs'])}")
    for mode in ('off_diffs','shadow_diffs'):
        for row in summary[mode]:
            print(mode,row['source'],row['baseline_off']['verdict'],row['candidate_off']['verdict'],row['candidate_shadow']['verdict'])
    return 0
if __name__=='__main__': raise SystemExit(main())
