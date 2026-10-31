import collections,json,pathlib,re
root=pathlib.Path(__file__).resolve().parents[2]
files=['src/semantic_checker.cpp','src/native_generator.cpp','src/fileio_semantics.inc','src/file_chunk_codegen.inc','src/file_chunk_lowering.inc','src/executor_invoke_lowering.inc','src/static_bounds.inc','src/borrowed_views.inc','src/handler_lowering.inc']
field=re.compile(r'\b([A-Za-z_]\w*)\.(a|b|text)\b')
reader=re.compile(r'\b(tree_[A-Za-z_0-9]+|inferred_expr_type|generated_expr_type|expression_tree|est::|check_ownership_expression|analyze_effect_expression|resolve_expression|expression_uses)')
rows=[];cats=collections.Counter(); byfile=collections.Counter()
for rel in files:
 p=root/rel
 for n,line in enumerate(p.read_text().splitlines(),1):
  for m in field.finditer(line):
   # Keep fields that enter a tree/fact/semantic query on this line or are carried into a named source variable.
   cat='tree_or_semantic_consumer' if reader.search(line) else 'Stmt-kind name/presence/flow/forwarding'
   if re.search(r'\b(?:==|!=)\s*"[^\"]+"',line): cat='stmt_name_keyword_or_binding_identity'
   rows.append((rel,n,m.group(1),m.group(2),cat,line.strip()))
   cats[cat]+=1;byfile[rel]+=1
out=root/'tmp/cp-e-evidence/stmt-slot-reader-inventory.tsv'
out.write_text('path\tline\tbase\tfield\tclassification\tsource\n'+''.join('\t'.join(map(str,r))+'\n' for r in rows))
summary={'files_scanned':len(files),'slot_reads':len(rows),'by_classification':dict(cats),'by_file':dict(byfile),'tree_expression_text_hits':sum(1 for rel in files for line in (root/rel).read_text().splitlines() if re.search(r'\btree_expression_text\s*\(',line))}
(root/'tmp/cp-e-evidence/reader-audit-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
