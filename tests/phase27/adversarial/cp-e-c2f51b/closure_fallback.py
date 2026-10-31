import pathlib,re,json
root=pathlib.Path(__file__).resolve().parents[4]
data=[json.loads(l) for l in (root/'tests/phase27/adversarial/cp-e-c2f51b/closure.jsonl').read_text().splitlines()]
failed={r['path'] for r in data if not r['candidate_off']['ok']}
pat=re.compile(r'^\s*(fn|proc|domain|enum|type)\s+([A-Za-z_]\w*)')
rows=[]
for rel in sorted(failed):
 decl=[]
 for n,line in enumerate((root/rel).read_text().splitlines(),1):
  m=pat.match(line)
  if m: decl.append({'line':n,'kind':m.group(1),'name':m.group(2)})
 rows.append({'source':rel,'baseline_symbols_error':next(r['baseline_off']['code'] for r in data if r['path']==rel),'candidate_symbols_error':next(r['candidate_off']['code'] for r in data if r['path']==rel),'candidate_off_check_ok':next(r['candidate_off']['ok'] for r in data if r['path']==rel),'candidate_shadow_check_ok':next(r['candidate_shadow']['ok'] for r in data if r['path']==rel),'source_declaration_fallback':decl})
(root/'tests/phase27/adversarial/cp-e-c2f51b/closure-query-fallback.json').write_text(json.dumps({'affected_source_count':len(rows),'declaration_fallback_count':sum(len(r['source_declaration_fallback']) for r in rows),'sources':rows},indent=2)+'\n')
print('fallback files',len(rows),'declarations',sum(len(r['source_declaration_fallback']) for r in rows))
