import json,os,pathlib,subprocess,sys
root=pathlib.Path(sys.argv[1]).resolve(); cases={'empty_call_args':'consume(1,,2)','empty_first_arg':'consume(,2)','trailing_call_arg':'consume(1,)','repeated_collection_commas':'[1,,2]','repeated_nested_collection_commas':'[1, [2,,3], 4]','incomplete_binary':'1 +','incomplete_unary':'not','unclosed_collection':'[1, 2','invalid_operator_adjacency':'1 + * 2','incomplete_parenthesized':'(1 +)','trailing_member':'item.','incomplete_index':'items['}
out=[]
for name,expr in cases.items():
 p=root/'tmp'/f'cpe-probe-{name}.moss'; p.write_text('fn consume(a: Int, b: Int, c: Int):\n  pass\n\nfn probe():\n  value = '+expr+'\n')
 for revision,base in [('baseline',root/'baseline'),('candidate',root)]:
  for mode in ('off','shadow'):
   env=os.environ.copy();env['MOSS_EST']=mode
   q=subprocess.run([str(base/'moss'),'check',str(p),'--json'],cwd=base,env=env,capture_output=True,text=True)
   try:
    d=json.loads(q.stdout); err=d.get('error'); diag=d.get('result',{}).get('diagnostics',[])
    outcome={'ok':d.get('ok'),'codes':([err['code']] if err else [x.get('code') for x in diag]),'messages':([err['message']] if err else [x.get('message') for x in diag])}
   except Exception: outcome={'ok':False,'codes':['PROCESS_ABORT' if q.returncode<0 or q.returncode>=128 else 'NON_JSON'],'messages':[q.stderr.strip()[-600:]]}
   out.append({'case':name,'expression':expr,'revision':revision,'mode':mode,'returncode':q.returncode,**outcome})
json.dump(out,open(root/'tmp/cpe-probe-results.json','w'),indent=2)
for row in out: print(row['case'],row['revision'],row['mode'],row['returncode'],row['codes'])
