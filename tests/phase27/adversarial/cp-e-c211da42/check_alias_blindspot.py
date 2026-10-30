#!/usr/bin/env python3
"""Reproduce the checker-lint alias blind spot at the frozen source."""
import pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[4]
src=(root/'src/semantic_checker.cpp').read_text()
start=src.find('string iter_expr = trim(statement.b);')
end=src.find('auto collection_type = inferred_expr_type(iter_expr, env);',start)
if start<0 or end<0 or 'starts_with(iter_expr, "range(")' not in src[start:end]:
 raise SystemExit('expected For iterable alias sink not found')
r=subprocess.run([sys.executable,str(root/'tests/tooling/check_phase27_string_debt.py'),'--require-zero','checker'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
print('checker_lint_exit='+str(r.returncode))
print('raw_iterable_shape_read=found')
print('sink_excerpt='+repr(src[start:end]))
sys.exit(0 if r.returncode==0 else 1)
