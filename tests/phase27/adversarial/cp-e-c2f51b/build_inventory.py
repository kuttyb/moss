from pathlib import Path
import re,json
root=Path(__file__).resolve().parents[4]
paths=['src/semantic_checker.cpp','src/fileio_semantics.inc','src/file_chunk_lowering.inc','src/executor_invoke_lowering.inc','src/static_bounds.inc','src/native_generator.cpp','src/file_chunk_codegen.inc','src/borrowed_views.inc','src/handler_lowering.inc']
# Include named expression-bearing Stmt slots plus AST projection calls on them.
field=re.compile(r'\b([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\.(a|b|text|args)\b')
rows=[]
for rel in paths:
 p=root/rel
 if not p.exists(): continue
 lines=p.read_text(errors='replace').splitlines()
 for n,line in enumerate(lines,1):
  for m in field.finditer(line):
   recv,slot=m.groups(); low=line.lower()
   if slot=='args': role='call argument values; verify call/message AST projection'
   elif re.search(r'(?:==|!=)\s*"[^"\n]*"|"[^"\n]*"\s*(?:==|!=)',line): role='literal comparison: NAME/keyword only if statement-kind selected; inventory separately audited'
   elif re.search(r'(?:==|!=)\s*[A-Za-z_]\w*',line): role='binding comparison: allowed only when slot denotes binding/root name'
   elif any(x in low for x in ('tree_', 'est::', 'expression_handle','check_expression','expr(')): role='expression-bearing slot consumed via EST/tree/checker/emitter path'
   elif re.search(r'\b(?:trim|starts_with|find|rfind|front|back|substr|size)\s*\(',line): role='text/shape helper use; bounded taint review required'
   else: role='direct slot read; inspect statement kind for name vs expression'
   rows.append((rel,n,recv+'.'+slot,role,line.strip()))
out=root/'tests/phase27/adversarial/cp-e-c2f51b/stmt-slot-inventory.tsv'
out.write_text('path\tline\tslot\tclassification\tsource\n'+''.join('\t'.join(map(str,r))+'\n' for r in rows))
api=[]
for p in (root/'src').rglob('*'):
 if p.suffix not in ('.cpp','.hpp','.inc'): continue
 for n,line in enumerate(p.read_text(errors='replace').splitlines(),1):
  if 'tree_expression_text(' in line: api.append(f'{p.relative_to(root)}:{n}:{line.strip()}')
summary={'inventory_files':paths,'slot_access_occurrences':len(rows),'tree_expression_text_calls_in_src':len(api),'tree_expression_text_sites':api,'field_counts':{},'classifications':{}}
for _,_,slot,role,_ in rows:
 summary['field_counts'][slot]=summary['field_counts'].get(slot,0)+1
 summary['classifications'][role]=summary['classifications'].get(role,0)+1
(root/'tests/phase27/adversarial/cp-e-c2f51b/slot-inventory-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
