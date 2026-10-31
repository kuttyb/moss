#!/usr/bin/env python3
"""Compare baseline/candidate compilers on the same frozen sdk/tables source."""
from pathlib import Path
import json,os,subprocess,hashlib,re
root=Path(__file__).resolve().parents[4]; base=root.parent/'cp-e-base'; out=root/'tests/phase27/adversarial/cp-e-c2f51b'
source_tree=root; margo=source_tree/'margo'; cwd=source_tree/'sdk/tables'
rows=[]
for label,compiler in [('baseline',base/'build/compiler/moss-dev'),('candidate',root/'build/compiler/moss-dev')]:
 env=os.environ.copy(); env['MOSS']=str(compiler); env['MARGO_HOME']=str(root/'tmp/cp-e-c2f51b'/f'margo-tables-{label}-same-source')
 p=subprocess.run([str(margo),'build','--json'],cwd=cwd,env=env,text=True,capture_output=True)
 generated=(cwd/'build/debug/tables.rs')
 generated_rows=[]
 if generated.exists():
  generated_rows=[{'line':n,'definition':bool(re.search(r'\bfn\s+__moss_parse_(?:int|float)\b',line)),'text':line.strip()} for n,line in enumerate(generated.read_text(errors='replace').splitlines(),1) if '__moss_parse_int' in line or '__moss_parse_float' in line]
 rows.append({'label':label,'generated_rust':str(generated),'generated_numeric_helper_lines':generated_rows,'source_sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=source_tree,text=True).strip(),'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'source_tree':str(source_tree),'cwd':'sdk/tables','command':[str(margo),'build','--json'],'MOSS':str(compiler),'MARGO_HOME':env['MARGO_HOME'],'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
(out/'native-build-reproduction.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps([{'label':r['label'],'exit':r['exit'],'compiler_sha256':r['compiler_sha256']} for r in rows],indent=2))
if rows[0]['exit'] != 0 or rows[1]['exit'] == 0: raise SystemExit(1)
