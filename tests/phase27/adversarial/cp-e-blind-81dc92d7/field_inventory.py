#!/usr/bin/env python3
import collections,json,pathlib,re
root=pathlib.Path(__file__).resolve().parents[4];out=pathlib.Path(__file__).resolve().parent
files=['semantic_checker.cpp','native_generator.cpp','fileio_semantics.inc','file_chunk_lowering.inc','executor_invoke_lowering.inc','static_bounds.inc','file_chunk_codegen.inc','borrowed_views.inc','handler_lowering.inc']
rows=[]
for name in files:
 p=root/'src'/name
 if not p.exists():continue
 text=p.read_text()
 for field in ('text','a','b','args'):
  count=len(re.findall(r'\b(?:s|stmt|statement|prior)\.'+field+r'\b',text))
  rows.append({'file':'src/'+name,'field':'Stmt.'+field,'direct_reads':count})
(out/'field-inventory.json').write_text(json.dumps({'fields':['Stmt.text','Stmt.a','Stmt.b','Stmt.args'],'reads':rows,'total':sum(r['direct_reads'] for r in rows),'tree_expression_text_reads':0,'note':'Counts are direct member references under checker/emitter consumer files; each field has kind-dependent meaning.'},indent=2)+'\n')
print(json.dumps({'by_field':dict(collections.Counter({f:sum(r['direct_reads'] for r in rows if r['field']==f) for f in ['Stmt.text','Stmt.a','Stmt.b','Stmt.args']})),'by_file':rows,'total':sum(r['direct_reads'] for r in rows)},indent=2))
