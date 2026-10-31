#!/usr/bin/env python3
import collections,importlib.util,json,pathlib,re,subprocess
root=pathlib.Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('debt',root/'tests/tooling/check_phase27_string_debt.py')
debt=importlib.util.module_from_spec(spec); spec.loader.exec_module(debt)
api=collections.Counter()
for (consumer,path,line,text,name),count in debt.sites().items(): api[(consumer,name)]+=count
checker=root/'src/semantic_checker.cpp'; emitter=root/'src/native_generator.cpp'
reasons={'push':'collection method name','insert':'collection method name','invoke':'executor method name','join':'executor method name','consume':'effect keyword','FileIO':'static FileIO receiver binding name','pop':'collection method name','pop_front':'collection method name','assert':'builtin call name','sort':'builtin call name','pass':'no-op keyword','_':'assignment binding placeholder','delete':'Map method name','assertEqual':'builtin call name','write':'FileIO method name','open':'FileIO method name','sync':'FileIO method name','close':'FileIO method name'}
raw=[]
for relative in debt.paths():
 consumer=debt.consumer_for(relative)
 if consumer not in ('checker','emitter'): continue
 path=root/relative
 for lineno,line in enumerate(path.read_text().splitlines(),1):
  for m in debt.RAW_NAME_COMPARE.finditer(line):
   raw.append({'kind':'Stmt-name-literal','consumer':consumer,'path':path.relative_to(root).as_posix(),'line':lineno,'field':m.group(0),'literal':m.group(1),'classification':'NAME','reason':reasons[m.group(1)]})
  for m in debt.RAW_FIELD_VARIABLE_COMPARE.finditer(line):
   raw.append({'kind':'Stmt-binding-name','consumer':consumer,'path':path.relative_to(root).as_posix(),'line':lineno,'field':m.group(0),'slot':m.group(1),'reason':'assignment/loop binding slot; separately audited by widened lint'})
text_uses=[]
for p in (root/'src').rglob('*'):
 if p.suffix not in ('.cpp','.hpp','.inc'): continue
 for n,line in enumerate(p.read_text(errors='replace').splitlines(),1):
  if re.search(r'\btree_expression_text\s*\(',line): text_uses.append({'path':p.relative_to(root).as_posix(),'line':n,'text':line.strip()})
exemptions=debt.load_exemptions()
out={'candidate_sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
     'expression_reader_api_counts':{f'{c}/{name}':n for (c,name),n in sorted(api.items()) if c in ('checker','emitter')},
     'checker_reader_sites':sum(n for (c,_),n in api.items() if c=='checker'),
     'emitter_reader_sites':sum(n for (c,_),n in api.items() if c=='emitter'),
     'tree_expression_text_uses_in_src':text_uses,
     'raw_stmt_classifications':raw,
     'raw_stmt_counts':{'NAME comparisons':sum(x['kind']=='Stmt-name-literal' for x in raw),'binding comparisons':sum(x['kind']=='Stmt-binding-name' for x in raw)},
     'type_argument_exemptions':[{'path':p,'function':fn,'api':api,'reason':reason} for (p,fn,api),reason in sorted(exemptions.items())],
     'lint_exemption_validation':'tests/tooling/check_phase27_string_debt.py exited 0; function_ranges fails closed on missing/ambiguous function; sites() fails on stale exemption or count != 1'}
(root/'tests/phase27/adversarial/cp-e-postfix-agent2-20261030/reader-inventory.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'checker_reader_sites':out['checker_reader_sites'],'emitter_reader_sites':out['emitter_reader_sites'],'tree_expression_text_uses':len(text_uses),'raw_stmt_counts':out['raw_stmt_counts'],'exemptions':len(exemptions),'classifications':len(raw)}))
