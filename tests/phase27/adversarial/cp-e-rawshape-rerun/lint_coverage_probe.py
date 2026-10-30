#!/usr/bin/env python3
"""Reproduce the direct-field versus local-alias blind spot in CP-E lint."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location(
    "debt", root / "tests/tooling/check_phase27_string_debt.py")
debt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debt)

cases = {
    "direct_dot_search": ("auto p = statement.a.find('[');", True),
    "direct_trimmed_prefix": ("if (starts_with(trim(statement.b), \"[\")) {}", True),
    "direct_first_char": ("if (statement.text.front() == '[') {}", True),
    "local_alias_dot_search": ("auto text = statement.a; auto p = text.find('[');", False),
    "local_alias_prefix": ("auto value = statement.b; if (starts_with(value, \"[\")) {}", False),
    "local_alias_first_char": ("auto expression = statement.text; if (expression.front() == '[') {}", False),
}
for name, (line, expected) in cases.items():
    actual = bool(debt.RAW_FIELD_SHAPE.search(line) or
                  debt.RAW_FIELD_PREFIX.search(line) or
                  debt.RAW_FIELD_FRONT_DISPATCH.search(line))
    print(f"{name}\texpected={expected}\tactual={actual}")
    if actual != expected:
        raise SystemExit(f"unexpected raw-shape lint behavior for {name}")
