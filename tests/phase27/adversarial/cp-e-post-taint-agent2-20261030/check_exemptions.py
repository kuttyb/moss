#!/usr/bin/env python3
"""Verify all three type reader exemptions are function+API fail-closed."""
import importlib.util, json
from pathlib import Path
root=Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('reader_lint',root/'tests/tooling/check_phase27_string_debt.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
original=m.read_source; path='src/semantic_checker.cpp'; source=original(path)
functions=('phase20_is_nontransferable_capability','builtin_equality_type','contains_float_type')
results=[]
for function in functions:
 def remove(relative,commit=None):
  value=original(relative,commit)
  return value.replace(function+'(const string& type)', 'renamed_'+function+'(const string& type)', 1) if relative==path else value
 m.read_source=remove
 try:m.sites(); outcome='NOT_REJECTED'
 except SystemExit as e:outcome=str(e)
 results.append({'function':function,'case':'removed','actual':outcome,'pass':'missing exemption function' in outcome})
 ranges=m.function_ranges(source,{function}); start,end=ranges[function]
 lines=source.splitlines(keepends=True)
 lines.insert(end-1, "  moss::split_top_level(\"x\", ',');\n")
 mutated=''.join(lines)
 def duplicate(relative,commit=None): return mutated if relative==path else original(relative,commit)
 m.read_source=duplicate
 try:m.sites(); outcome='NOT_REJECTED'
 except SystemExit as e:outcome=str(e)
 results.append({'function':function,'case':'second reader site','actual':outcome,'pass':'multiple reader sites' in outcome})
m.read_source=original
print(json.dumps(results,indent=2))
raise SystemExit(0 if all(x['pass'] for x in results) else 1)
