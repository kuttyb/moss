import json, os, pathlib, subprocess
root=pathlib.Path.cwd()
compilers={
 'baseline': root/'tmp/CP_E_PHASE27_3_75957ED1_CLEAN_BASE/moss',
 'candidate': root/'tmp/CP_E_PHASE27_3_75957ED1_CLEAN/moss',
}
files=[
 'tests/phase27/est/repro/incomplete_unary_not.moss',
 'tests/phase27/est/repro/unclosed_vector_expression.moss',
 'tests/phase27/est/repro/trailing_member_expression.moss',
 'tests/phase27/est/repro/invalid_operator_adjacency.moss',
 'tests/phase27/est/repro/unsupported_exponent.moss',
 'tests/phase27/est/repro/unsupported_operators.moss',
 'tests/phase27/est/repro/membership_operator.moss',
 'tests/phase27/est/repro/symbolic_boolean.moss',
 'tests/phase27/est/repro/split_fragment.moss',
 'tests/phase27/est/repro/incomplete_expression.moss',
 'tests/phase27/est/repro/unresolved_collection_result.moss',
 'tests/phase27/est/repro/incomplete_vector.moss',
 'tests/phase27/est/repro/trailing_member_after_empty_vector.moss',
 'tests/phase27/est/repro/empty_call_argument.moss',
 'tests/phase27/est/repro/empty_trailing_call_argument.moss',
 'tests/phase27/est/repro/empty_vector_element.moss',
 'tests/phase27/est/repro/juxtaposed_operands.moss',
]
records=[]
for f in files:
 p=root/f
 assert p.is_file(), f
 for comp, exe in compilers.items():
  for mode in ('off','shadow'):
   env=os.environ.copy(); env['MOSS_EST']=mode
   run=subprocess.run([str(exe),'check',str(p),'--json'],cwd=root,env=env,text=True,capture_output=True)
   try: payload=json.loads(run.stdout)
   except Exception: payload={'parse_error':True,'stdout':run.stdout,'stderr':run.stderr}
   e=payload.get('error',{})
   records.append({'file':f,'compiler':comp,'mode':mode,'rc':run.returncode,'ok':payload.get('ok'),'code':e.get('code'),'message':e.get('message'),'line':e.get('line'),'column':e.get('column'),'payload':payload})
out=root/'tmp/CP_E_PHASE27_3_75957ED1_CLEAN/probes.json'
out.write_text(json.dumps(records,indent=2,sort_keys=True)+'\n')
for f in files:
 rows=[r for r in records if r['file']==f]
 signatures={(r['code'],r['message'],r['line'],r['column'],r['ok']) for r in rows}
 print(f, 'MATCH' if len(signatures)==1 else 'DIFF', 'signatures='+str(len(signatures)))
 if len(signatures)>1:
  for r in rows: print(' ',r['compiler'],r['mode'],r['code'],r['message'],r['line'],r['column'],r['ok'])
