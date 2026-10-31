#!/usr/bin/env python3
"""Mutation-test each Phase 27.3 function/API keyed reader exemption."""
from pathlib import Path
import importlib.util

ROOT=Path(__file__).resolve().parents[4]
PROBE=ROOT/'tmp/cp-e-exemption-probe'
script=PROBE/'tests/tooling/check_phase27_string_debt.py'
spec=importlib.util.spec_from_file_location('string_debt_lint',script)
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
doc=module.EXEMPTIONS; original_doc=doc.read_text()
source=PROBE/'src/semantic_checker.cpp'; original_source=source.read_text()
keys=(('phase20_is_nontransferable_capability','split_top_level'),
      ('builtin_equality_type','split_top_level'),
      ('contains_float_type','split_top_level'))
results=[]
def must_fail(label):
    try: module.sites()
    except SystemExit as error: results.append((label,str(error)))
    else: raise SystemExit(f'{label} unexpectedly passed')
try:
  for function,api in keys:
    entry=f'src/semantic_checker.cpp | {function} | {api}'
    for case,replacement in (
      ('missing-target',f'src/semantic_checker.cpp | missing_{function} | {api}'),
      ('stale-key',f'src/semantic_checker.cpp | absent_{function} | {api}')):
      doc.write_text(original_doc.replace(entry,replacement,1))
      must_fail(f'{function}/{case}')
    doc.write_text(original_doc)
    line_start,line_end=module.function_ranges(original_source,{function})[function]
    lines=original_source.splitlines(keepends=True)
    start=sum(map(len,lines[:line_start-1])); end=sum(map(len,lines[:line_end]))
    body=original_source[start:end]; close=body.rfind('}')
    body=body[:close]+"  (void)moss::split_top_level(\"T\", ',');\n"+body[close:]
    source.write_text(original_source[:start]+body+original_source[end:])
    must_fail(f'{function}/duplicate-reader')
    source.write_text(original_source)
finally:
  doc.write_text(original_doc); source.write_text(original_source)
print(f'{len(results)} fail-closed mutations rejected across {len(keys)} exemptions')
for result in results: print(*result,sep=': ')
