from pathlib import Path
import re,json
root=Path(__file__).resolve().parents[4]
files=['src/semantic_checker.cpp','src/fileio_semantics.inc','src/static_bounds.inc','src/native_generator.cpp']
pattern=re.compile(r'\b(?:const\s+)?(?:string|auto)\s+(\w+)\s*=\s*trim\(\s*(\w+\.(?:a|b|text))\s*\)')
rows=[]
for rel in files:
 path=root/rel
 if not path.exists():continue
 lines=path.read_text().splitlines()
 for i,line in enumerate(lines):
  match=pattern.search(line)
  if not match:continue
  name,slot=match.groups(); uses=[]
  for j in range(i+1,min(len(lines),i+101)):
   if re.search(r'\b'+re.escape(name)+r'\b',lines[j]):uses.append({'line':j+1,'text':lines[j].strip()})
  rows.append({'path':rel,'line':i+1,'local':name,'source_slot':slot,'uses_within_100_lines':uses})
out={'alias_count':len(rows),'aliases':rows}
(root/'tests/phase27/adversarial/cp-e-c2f51b/bounded-taint-direct.json').write_text(json.dumps(out,indent=2)+'\n')
print('trimmed expression-text aliases:',len(rows))
