#!/usr/bin/env python3
"""Reproduce the emitter zero-reader false green without editing the linter."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
LINTER = ROOT / "tests/tooling/check_phase27_string_debt.py"
spec = importlib.util.spec_from_file_location("string_debt_linter", LINTER)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def count_emitter_sites():
    return sum(amount for (consumer, _path, _line, _text, _api), amount
               in module.sites().items() if consumer == "emitter")


current = count_emitter_sites()
print(f"current emitter sites: {current}")
module.CONSUMER_PATHS["emitter"] += ("executor_invoke_codegen.inc",)
with_include = sum(amount for (consumer, path, _line, _text, _api), amount
                   in module.sites().items()
                   if consumer == "emitter" and
                   path == "src/executor_invoke_codegen.inc")
print(f"omitted include sites: {with_include}")
if current != 0 or with_include != 1:
    raise SystemExit("reader-lint inventory finding did not reproduce")

entries = module.load_exemptions()
source = (ROOT / "src/semantic_checker.cpp").read_text()
ranges = module.function_ranges(source, {
    fn for path, fn, _api in entries if path == "src/semantic_checker.cpp"})
if len(entries) != 3 or len(ranges) != 3:
    raise SystemExit("keyed exemption inventory no longer resolves uniquely")
for path, function, api in entries:
    start, end = ranges[function]
    lines = source.splitlines()[start - 1:end]
    import re
    count = sum(len(re.findall(r"\b" + re.escape(api) + r"\s*\(", line))
                for line in lines)
    if count != 1:
        raise SystemExit(f"exemption is not one-site fail-closed: {(path, function, api)}={count}")
print("three exemptions resolve uniquely with one site each")
