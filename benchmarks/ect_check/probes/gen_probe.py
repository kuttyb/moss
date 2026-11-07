#!/usr/bin/env python3
"""Minimal reproducers for compiler behavior found while building the DAG
fixtures (agent-feedback/phase28-5/ect-bench/generic-dag, record 001).

  gen_probe.py var    DEPTH   chain whose node binds the child result: `var c = n(x)` / `return c`
  gen_probe.py varplus DEPTH  same, `return c + 1`
  gen_probe.py plus   DEPTH   `return n(x) + 1` (fixture shape, fast)
  gen_probe.py direct DEPTH   `return n(x)` (fast)

Usage: python3 gen_probe.py var 8 > /path/p.moss && moss check /path/p.moss
Measured on moss-opt, quiet machine, wall seconds at depth 6/8/9/10:
  var      0.18 / 3.2 / 13.5 / 57.7   (x4 per level)
  varplus  0.07 / 0.60 / 1.9 / 5.7
  plus, direct: ~0.01 at every depth
The shared-callee diamond (x2 per level) is the committed fixture family
fixtures/dag_diamond (D=6/8/10/12: see the report), not a separate probe.
Committed samples: var_chain_d008.moss, varplus_chain_d008.moss.
"""
import sys

style, depth = sys.argv[1], int(sys.argv[2])
out = ["type T:", "  v: Int", "", "  fn score() -> Int:", "    return v + 1", "",
       "fn n0(x):", "  return x.score()", ""]
for i in range(1, depth):
    body = {"var": ["  var c = n%d(x)" % (i - 1), "  return c"],
            "varplus": ["  var c = n%d(x)" % (i - 1), "  return c + 1"],
            "plus": ["  return n%d(x) + 1" % (i - 1)],
            "direct": ["  return n%d(x)" % (i - 1)]}[style]
    out += [f"fn n{i}(x):", *body, ""]
out += ["fn main():", f"  echo n{depth - 1}(T(v: 1))", ""]
print("\n".join(out))
