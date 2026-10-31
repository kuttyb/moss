#!/usr/bin/env python3
import json,os,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[4]
source=root/'tests/phase27/adversarial/cp-e-postfix-agent2-20261030/callable_parameter_named_argument.moss'
baseline=pathlib.Path(sys.argv[1]).resolve(); candidate=pathlib.Path(sys.argv[2]).resolve()
rows=[]
for compiler,binary in [('baseline',baseline),('candidate',candidate)]:
 for mode in ('off','shadow'):
  env=os.environ.copy(); env['MOSS_EST']=mode
  p=subprocess.run([str(binary),'check',str(source),'--json'],cwd=root,env=env,text=True,capture_output=True)
  try: payload=json.loads(p.stdout)
  except Exception as e: payload={'parse_error':repr(e),'stdout':p.stdout}
  error=payload.get('error',{}) if isinstance(payload,dict) else {}
  rows.append({'compiler':compiler,'mode':mode,'returncode':p.returncode,'error':error,'stderr':p.stderr})
summary={'expected_code':'CALL_NAMED_ARGUMENT_UNSUPPORTED','expected_line':5,'results':rows,
         'all_match':all(r['returncode']==1 and r['error'].get('code')=='CALL_NAMED_ARGUMENT_UNSUPPORTED' and r['error'].get('line')==5 and not r['stderr'] for r in rows)}
(root/'tests/phase27/adversarial/cp-e-postfix-agent2-20261030/named-argument-probe.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'all_match':summary['all_match'],'results':[{'compiler':r['compiler'],'mode':r['mode'],'returncode':r['returncode'],'code':r['error'].get('code'),'line':r['error'].get('line'),'stderr':r['stderr']} for r in rows]}))
if not summary['all_match']: sys.exit(1)
