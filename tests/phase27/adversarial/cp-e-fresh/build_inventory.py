#!/usr/bin/env python3
"""Emit exhaustive line/field inventory for checker/emitter Stmt text slots."""
import collections, pathlib, re, sys
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tests/tooling'))
import check_phase27_string_debt as lint
OUT=ROOT/'docs/phase27/adversarial/cp_e_fresh_inventory.tsv'
FIELD=re.compile(r'\b([A-Za-z_]\w*)\.(a|b|text|args|result_expression)\b')
NAME=re.compile(r'\b(?:s|prior|statement|stmt)\.(?:a|b|text)\s*(?:==|!=)\s*"([^"\n]*)"')
BIND=re.compile(r'\b[A-Za-z_]\w*(?:\([^()]*\))?(?:\.[A-Za-z_]\w*|\[[^]]+\])*\.(a|b|text)\s*(?:==|!=)\s*[A-Za-z_]\w*(?:\.[A-Za-z_]\w*|\[[^]]+\])*\b')
READ=re.compile(r'\b(?:'+ '|'.join(map(re.escape,lint.APIS)) + r')\s*\(')
counts=collections.Counter(); rows=[]
for path in [p for p in lint.paths() if lint.consumer_for(p) in ('checker','emitter')]:
  for no,line in enumerate(lint.read_source(path).splitlines(),1):
    matches=list(FIELD.finditer(line))
    if not matches: continue
    spans=[]
    for match in re.finditer(r"\b(?:tree_expressions_structurally_equal|" +
                             "|".join(map(re.escape,lint.APIS)) +
                             r"|trim)\s*\(",line):
      depth=0; quote=None; escaped=False; close=None
      for i in range(line.find("(",match.start()),len(line)):
        ch=line[i]
        if quote:
          if escaped: escaped=False
          elif ch=="\\": escaped=True
          elif ch==quote: quote=None
          continue
        if ch in ("\"", "'"): quote=ch
        elif ch=="(": depth+=1
        elif ch==")":
          depth-=1
          if depth==0: close=i+1; break
      if close is not None:
        kind="structural" if "tree_expressions_structurally_equal" in match.group() else "reader"
        spans.append((match.start(),close,kind))
    name_spans=[m.span() for pattern in (NAME,BIND) for m in pattern.finditer(line)]
    for m in matches:
      if any(a<=m.start()<b for a,b,k in spans if k=="structural"):
        cls,why='structural AST equality','This exact field occurrence is an operand of tree_expressions_structurally_equal.'
      elif any(a<=m.start()<b for a,b in name_spans):
        cls,why='allowed NAME/keyword slot','This exact field occurrence participates in a raw name/binding comparison; see the matching expression shape in the lint rules.'
      elif any(a<=m.start()<b for a,b,k in spans if k=="reader"):
        cls,why='expression reader','This exact field occurrence is an argument of a registered text parser/classifier or trim call.'
      else:
        cls,why='other','This occurrence is field propagation, selection, serialization, control/metadata, or use through another API; see the exact source line.'
      rows.append((path,no,m.group(1)+'.'+m.group(2),cls,why,line.strip()))
      counts[cls]+=1
with OUT.open('w') as f:
  f.write('path\tline\tfield\tclassification\trationale\tsource\n')
  for row in rows: f.write('\t'.join(str(x).replace('\t',' ') for x in row)+'\n')
print('field occurrence counts:',dict(counts))
print('line-field occurrences:',len(rows))
print('output:',OUT.relative_to(ROOT))
