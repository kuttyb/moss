#!/usr/bin/env python3
import importlib.util,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[4]
path=root/'tests/tooling/check_phase27_string_debt.py'
spec=importlib.util.spec_from_file_location('debt_lint',path); mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
checks=[]
def expect(label, loader, fragment):
 mod.load_exemptions=loader
 try:mod.sites()
 except SystemExit as error:
  message=str(error); ok=fragment in message; checks.append({'case':label,'expected_fragment':fragment,'error':message,'passed':ok})
 else:checks.append({'case':label,'expected_fragment':fragment,'error':None,'passed':False})
expect('missing function',lambda:{('src/semantic_checker.cpp','missing_cp_e_function','split_top_level'):'probe'},'missing exemption function')
expect('stale API key',lambda:{('src/semantic_checker.cpp','phase20_is_nontransferable_capability','parse_simple_call'):'probe'},'stale expression-reader exemptions')
mod.load_exemptions=lambda: {('src/semantic_checker.cpp','phase20_is_nontransferable_capability','split_top_level'):'probe'}
original=mod.read_source
def duplicate(relative,commit=None):
 source=original(relative,commit)
 if relative!='src/semantic_checker.cpp' or commit:return source
 ranges=mod.function_ranges(source,{'phase20_is_nontransferable_capability'})
 start,end=ranges['phase20_is_nontransferable_capability']; lines=source.splitlines(keepends=True)
 offset=sum(len(line) for line in lines[:start])
 return source[:offset]+'  split_top_level("probe");\n'+source[offset:]
mod.read_source=duplicate
try:mod.sites()
except SystemExit as error:
 message=str(error); checks.append({'case':'multiple sites under one exemption','expected_fragment':'gained multiple reader sites','error':message,'passed':'gained multiple reader sites' in message})
else:checks.append({'case':'multiple sites under one exemption','error':None,'passed':False})
(root/'tests/phase27/adversarial/cp-e-blind-81dc92d7/exemption-fail-closed.json').write_text(__import__('json').dumps(checks,indent=2)+'\n')
print(__import__('json').dumps(checks,indent=2))
if not all(row['passed'] for row in checks):raise SystemExit(1)
