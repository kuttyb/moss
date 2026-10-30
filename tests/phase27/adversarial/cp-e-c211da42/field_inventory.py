#!/usr/bin/env python3
"""Line inventory of expression-capable Stmt fields in checker/emitter consumers."""
import pathlib,re,csv
ROOT=pathlib.Path(__file__).resolve().parents[4]
FILES=['src/semantic_checker.cpp','src/fileio_semantics.inc','src/file_chunk_lowering.inc','src/executor_invoke_lowering.inc','src/static_bounds.inc','src/native_generator.cpp','src/file_chunk_codegen.inc','src/borrowed_views.inc','src/handler_lowering.inc']
FIELD=re.compile(r'\b(statement|stmt|s|prior)\.(text|a|b|args|result_expression)\b')
rows=[]
for rel in FILES:
 p=ROOT/rel
 if not p.exists(): continue
 consumer='checker' if rel in FILES[:5] else 'emitter'
 for line_no,line in enumerate(p.read_text().splitlines(),1):
  for m in FIELD.finditer(line): rows.append((consumer,rel,line_no,m.group(1)+'.'+m.group(2),line.strip()))
with (ROOT/'tests/phase27/adversarial/cp-e-c211da42/field_inventory.tsv').open('w') as f:
 w=csv.writer(f,delimiter='\t'); w.writerow(['consumer','path','line','field_reference','source_line']); w.writerows(rows)
print(f'field reference occurrences: {len(rows)}')
