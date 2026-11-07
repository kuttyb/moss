"""Phase 28.5.1: sweep every package fixture for exported untyped generics whose requirements
no visible trait covers (what Phase 28.6's package-build hardening would reject).

Independent of the generators: it reads the committed `.moss` sources only. For every
`export fn` with an untyped parameter it computes the methods called on that parameter
(`p.name(`), following the parameter when it is passed straight to another top-level function
of the same module (`callee(p)`, position-matched), and requires each such method to be
declared by an `export trait` of the same module (`fn name(`). Operators and indexing/iteration
are built-in traits' business and are not checked. Heuristic text analysis (no parser): it
reports what it can see; selftest proves it flags an uncovered method and accepts a covered one.

  python3 benchmarks/ect_check/export_sweep.py          # exit 1 when anything is uncovered
"""
from __future__ import annotations

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ect_common as C  # noqa: E402

FN = re.compile(r"^(export\s+)?fn\s+(\w+)\s*\((.*?)\)\s*(->\s*[^:]+)?:\s*$")
TRAIT = re.compile(r"^(export\s+)?trait\s+(\w+)\s*:")
TRAIT_METHOD = re.compile(r"^\s+fn\s+(\w+)\s*\(")


def parse_module(text: str):
    """-> (functions {name: {exported, params[(name, typed)], body[str]}}, exported trait methods set)"""
    fns, trait_methods = {}, set()
    cur = None
    in_trait = None
    for line in text.splitlines():
        t = TRAIT.match(line)
        if t:
            in_trait, cur = bool(t.group(1)), None
            continue
        f = FN.match(line)
        if f:
            params = []
            for raw in [p.strip() for p in f.group(3).split(",") if p.strip()]:
                params.append((raw.split(":")[0].strip(), ":" in raw))
            cur = {"exported": bool(f.group(1)), "params": params, "body": []}
            fns[f.group(2)] = cur
            in_trait = None
            continue
        if in_trait is not None:
            m = TRAIT_METHOD.match(line)
            if m and in_trait:
                trait_methods.add(m.group(1))
            elif line.strip() and not line.startswith(" "):
                in_trait = None
            continue
        if cur is not None and (line.startswith(" ") or not line.strip()):
            cur["body"].append(line)
        elif line.strip():
            cur = None
    return fns, trait_methods


def required_methods(fns: dict, name: str, param: str, seen=None) -> set[str]:
    seen = seen or set()
    if (name, param) in seen or name not in fns:
        return set()
    seen.add((name, param))
    body = "\n".join(fns[name]["body"])
    need = set(re.findall(rf"\b{re.escape(param)}\.(\w+)\s*\(", body))
    for callee, args in re.findall(r"\b(\w+)\(([^()]*)\)", body):
        if callee in fns:
            for pos, arg in enumerate([a.strip() for a in args.split(",")]):
                if arg == param and pos < len(fns[callee]["params"]):
                    cname, typed = fns[callee]["params"][pos]
                    if not typed:
                        need |= required_methods(fns, callee, cname, seen)
    return need


def sweep_text(text: str) -> list[str]:
    """Uncovered requirements of one module's exported untyped generics, as messages."""
    fns, covered = parse_module(text)
    bad = []
    for name, fn in fns.items():
        if not fn["exported"]:
            continue
        for pname, typed in fn["params"]:
            if typed:
                continue
            for method in sorted(required_methods(fns, name, pname) - covered):
                bad.append(f"export fn {name}({pname}) requires .{method}() but no exported trait declares it")
    return bad


def sweep_fixtures(manifest: dict | None = None) -> dict[str, list[str]]:
    manifest = manifest or C.load_manifest()
    out: dict[str, list[str]] = {}
    for fx in manifest["fixtures"]:
        if fx["kind"] != "package":
            continue
        root = C.FIXTURES_DIR / fx["path"]
        for src in sorted(root.rglob("*.moss")):
            if "build" in src.relative_to(root).parts:
                continue
            for msg in sweep_text(src.read_text()):
                out.setdefault(fx["id"], []).append(f"{src.relative_to(root)}: {msg}")
    return out


def count_exported_generics(manifest: dict | None = None) -> int:
    manifest = manifest or C.load_manifest()
    n = 0
    for fx in manifest["fixtures"]:
        if fx["kind"] == "package":
            for src in (C.FIXTURES_DIR / fx["path"]).rglob("*.moss"):
                fns, _ = parse_module(src.read_text())
                n += sum(1 for f in fns.values() if f["exported"] and any(not t for _, t in f["params"]))
    return n


if __name__ == "__main__":
    bad = sweep_fixtures()
    for fid, msgs in bad.items():
        print(f"{fid}: " + "; ".join(msgs[:3]))
    print(f"export sweep: {count_exported_generics()} exported untyped generics in package fixtures, "
          f"{len(bad)} fixture(s) with an uncovered requirement")
    sys.exit(1 if bad else 0)
