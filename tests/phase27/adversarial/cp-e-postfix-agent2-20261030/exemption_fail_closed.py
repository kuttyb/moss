#!/usr/bin/env python3
import importlib.util,pathlib,re
root=pathlib.Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('debt',root/'tests/tooling/check_phase27_string_debt.py')
debt=importlib.util.module_from_spec(spec); spec.loader.exec_module(debt)
real_paths,real_read=debt.paths,debt.read_source
base=real_read('src/semantic_checker.cpp')
exemptions=debt.load_exemptions(); results=[]
for path,function,api in sorted(exemptions):
 for mutation in ('function_disappears','second_reader_site'):
  source=base
  if mutation=='function_disappears':
   lines=source.splitlines(); start,_=debt.function_ranges(source,{function})[function]
   lines[start-1]=re.sub(r'\b'+re.escape(function)+r'(?=\s*\()', 'removed_exemption_target', lines[start-1], count=1)
   source='\n'.join(lines)+'\n'
   expected='missing exemption function'
  else:
   start,end=debt.function_ranges(source,{function})[function]
   lines=source.splitlines(); insert_at=end-1
   lines.insert(insert_at,f"  (void){api}(\"Vector[Int]\", ','); // synthetic extra site")
   source='\n'.join(lines)+'\n'; expected='gained multiple reader sites'
  debt.paths=lambda commit=None:['src/semantic_checker.cpp']
  debt.read_source=lambda rel,commit=None:source
  try:
   debt.sites()
  except SystemExit as exc:
   message=str(exc)
   if expected not in message: raise
   results.append({'function':function,'api':api,'mutation':mutation,'fails_closed':True,'message':message})
  else: raise SystemExit(f'exemption did not fail closed: {function} {mutation}')
debt.paths, debt.read_source=real_paths,real_read
(root/'tests/phase27/adversarial/cp-e-postfix-agent2-20261030/exemption-fail-closed.json').write_text(__import__('json').dumps(results,indent=2)+'\n')
print(f'{len(results)} fail-closed mutation checks passed across {len(exemptions)} exemptions')
