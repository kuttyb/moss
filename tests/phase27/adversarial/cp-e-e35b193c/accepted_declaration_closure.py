#!/usr/bin/env python3
import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[4]
bins={'baseline':root/'tmp/phase27-3/review-cp-e-e35b193c-baseline/moss','candidate':root/'moss'}
fixtures={'accepted':root/'tests/phase27/adversarial/cp-e-phase27-3-final-cache/accepted_closure.moss','rejected_unreachable':root/'tests/phase27/adversarial/cp-e-phase27-3-final-cache/rejected_unreachable.moss'}
rows=[]
for label,binary in bins.items():
 for mode in ('off','shadow'):
  for case,path in fixtures.items():
   env=os.environ.copy();env['MOSS_EST']=mode
   p=subprocess.run([str(binary),'check',str(path),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=30)
   obj=json.loads(p.stdout)
   row={'compiler':label,'mode':mode,'case':case,'ok':obj.get('ok'),'error':obj.get('error'),'diagnostics':obj.get('result',{}).get('diagnostics')}
   rows.append(row)
expected={('accepted',True),('rejected_unreachable',False)}
for r in rows:
 if (r['case'],r['ok']) not in expected: raise SystemExit('closure expectation failed '+repr(r))
for case in fixtures:
 vals=[(r['ok'],r['error'],r['diagnostics']) for r in rows if r['case']==case]
 if any(v!=vals[0] for v in vals[1:]): raise SystemExit(f'{case}: source-level checked closure result varies: {vals}')
out=root/'tests/phase27/adversarial/cp-e-e35b193c/accepted-declaration-closure.json'
out.write_text(json.dumps({'oracle':'all declarations in the source are checked: an unreachable valid declaration does not prevent success, while an unreachable unknown identifier still rejects','runs':rows},indent=2)+'\n')
print('accepted declaration closure: valid unreachable declaration accepted; invalid unreachable declaration rejected identically in both modes and compilers')
