#!/usr/bin/env python3
"""Broad line/field census plus direct expression-reader classification."""
import collections, pathlib, re, sys
root=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(root/'tests/tooling'))
import check_phase27_string_debt as debt
out=pathlib.Path(sys.argv[1]).resolve(); out.parent.mkdir(parents=True,exist_ok=True)
field=re.compile(r'\b([A-Za-z_]\w*)\.(a|b|text|args|expression_handles|est_pattern|message_result)\b')
reader=re.compile(r'\b(?:'+ '|'.join(map(re.escape,debt.APIS)) + r')\s*\(')
rows=[]; count=collections.Counter()
for path in debt.paths():
 if debt.consumer_for(path) not in ('checker','emitter'): continue
 source=debt.read_source(path)
 for no,line in enumerate(source.splitlines(),1):
  found=list(field.finditer(line))
  if not found: continue
  for m in found:
   field_name=m.group(2)
   context=line.strip()
   if field_name in ('expression_handles','est_pattern','message_result'):
    cls='EST handle/pattern or binding metadata'
   elif reader.search(line):
    cls='registered expression text parser/classifier callsite'
   elif re.search(r'\b(?:tree_|est::|expression_tree|expression_root|expression_slot)',line):
    cls='EST projection/query or tree structure'
   elif re.search(r'(?:==|!=)\s*"|"[^"\n]*"\s*(?:==|!=)',line):
    cls='raw literal/name/keyword comparison; audit by statement kind'
   elif re.search(r'(?:==|!=)\s*[A-Za-z_]\w*',line):
    cls='raw binding/name comparison; audit by statement kind'
   elif field_name != 'args' and re.search(r'\b'+re.escape(m.group(1))+r'\.'+field_name+r'\s*\.\s*(?:find|rfind|front|back|starts_with|ends_with)\s*\(',line):
    cls='raw string shape operation; taint review required'
   else:
    cls='propagation, diagnostics, metadata, or other field use; bounded taint review'
   rows.append((path,no,m.group(1),field_name,cls,context))
   count[(field_name,cls)]+=1
with out.open('w') as f:
 f.write('path\tline\treceiver\tfield\tclassification\tsource\n')
 for row in rows: f.write('\t'.join(str(x).replace('\t',' ') for x in row)+'\n')
print('line/field reads',len(rows))
for (field_name,cls),n in sorted(count.items()): print(f'{field_name}\t{cls}\t{n}')
print('output',out)
