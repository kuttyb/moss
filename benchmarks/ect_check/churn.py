"""Summary churn (Phase 28.5, Team 4): how often does an edit move any function
summary, and does it move summaries of functions that were NOT edited (ripple)?

Everything semantic comes from the existing Phase 22 queries; nothing here
computes or parses compiler internals:

  moss symbols --source F --json                 enumerates the functions of F
  moss type      ENTITY --source F --json        -> types
  moss ownership ENTITY --source F --json        -> access (READ/WRITE/CONSUME)
  moss effects   ENTITY --source F --json        -> raises + observable effects

Compared fields (both trees, same function = same durable entity id), after the
only normalization applied: positional `@<line>` suffixes of semantic identities
are stripped, because a line number is a location, not a fact.

  types    `type`.resolved_type, `type`.type_facts (status, resolved, category,
           origin) and every parameter's (name, type) from `ownership`.
  access   every parameter's (name, READ|WRITE|CONSUME) and the value_boundary
           from `ownership`.ownership_facts.
  raises   may_panic and raise_set, both `effects`.observable_effects and
           `effects`.effect_summary.transitive.
  effects  the remaining observable-effect flags of those two objects
           (local_capture_read, local_mutation, domain_read, domain_write,
           message, external_io, may_diverge, unresolved) and the summary's
           status / certainty / transitive_scope / direct_status.

Deliberately NOT compared: source line/column, semantic_identity,
implementation_hash, semantic_interface_hash (positional or whole-function
hashes, not the summary), and the synchronization plan (its graph identity
changes with any program change).

"Edited" is textual and only attributes the diff to functions: a function is
edited when its source slice (declaration line to the line before the next
non-blank line that is not indented deeper than the declaration, trailing blanks
trimmed, line ends stripped) differs between the two trees. It is a location
fact, not a semantic one.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
import tomllib

import ect_common as C

SUMMARY_KINDS = {"function", "method", "handler"}
CATEGORIES = ("types", "access", "raises", "effects")
RAISE_KEYS = ("may_panic", "raise_set")
EFFECT_KEYS = ("local_capture_read", "local_mutation", "domain_read", "domain_write", "message",
               "external_io", "may_diverge", "unresolved")
LINE_SUFFIX = re.compile(r"@\d+")


def norm(value):
    if isinstance(value, dict):
        return {k: norm(v) for k, v in sorted(value.items())}
    if isinstance(value, list):
        return [norm(v) for v in value]
    if isinstance(value, str):
        return LINE_SUFFIX.sub("", value)
    return value


def summarize_entity(type_doc: dict, own_doc: dict, eff_doc: dict) -> dict:
    """Reduce the three query `result` objects to the compared categories."""
    facts = own_doc.get("ownership_facts") or {}
    params = facts.get("parameters") or []
    obs = eff_doc.get("observable_effects") or {}
    es = eff_doc.get("effect_summary") or {}
    trans = es.get("transitive") or {}
    out = {
        "types": {"resolved_type": type_doc.get("resolved_type"), "type_facts": type_doc.get("type_facts"),
                  "params": [[p.get("name"), p.get("type")] for p in params]},
        "access": {"status": facts.get("status"),
                   "params": [[p.get("name"), p.get("effect")] for p in params],
                   "value_boundary": facts.get("value_boundary")},
        "raises": {"observable": {k: obs.get(k) for k in RAISE_KEYS},
                   "transitive": {k: trans.get(k) for k in RAISE_KEYS}},
        "effects": {"observable": {k: obs.get(k) for k in EFFECT_KEYS},
                    "transitive": {k: trans.get(k) for k in EFFECT_KEYS},
                    "summary": {k: es.get(k) for k in ("status", "certainty", "transitive_scope", "direct_status")}},
    }
    return norm(out)


# --------------------------------------------------------------------------
# queries
# --------------------------------------------------------------------------

def module_path_for(pkg_root: Path) -> list[str]:
    """Interface directories of the package's dependencies, as margo sets
    MOSS_MODULE_PATH for a `--check` build: every transitive dependency's
    build/check-debug (not the root's own)."""
    seen: set[Path] = set()
    out: list[Path] = []

    def visit(p: Path):
        try:
            doc = tomllib.loads((p / "Moss.toml").read_text())
        except (OSError, ValueError):
            return
        for dep in (doc.get("dependencies") or {}).values():
            if isinstance(dep, dict) and "path" in dep:
                q = (p / dep["path"]).resolve()
                if q not in seen:
                    seen.add(q)
                    visit(q)
                    out.append(q)
    visit(pkg_root.resolve())
    return [str(d / "build" / "check-debug") for d in out]


def query(moss: Path, name: str, target: str | None, source: Path, cwd: Path, env: dict) -> tuple[dict | None, str | None]:
    argv = [str(moss), name] + ([target] if target else []) + ["--source", str(source), "--json"]
    p = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True)
    try:
        doc = json.loads(p.stdout)
    except ValueError:
        return None, f"{name}: non-JSON output (exit {p.returncode})"
    if not doc.get("ok"):
        err = doc.get("error") or {}
        return None, f"{name}: {err.get('code')}: {str(err.get('message'))[:160]}"
    return doc.get("result") or {}, None


def span_text(lines: list[str], line_no: int) -> str:
    i = line_no - 1
    if not 0 <= i < len(lines):
        return ""
    ind = len(lines[i]) - len(lines[i].lstrip())
    j = i + 1
    while j < len(lines):
        s = lines[j]
        if s.strip() and (len(s) - len(s.lstrip())) <= ind:
            break
        j += 1
    seg = lines[i:j]
    while seg and not seg[-1].strip():
        seg.pop()
    return "\n".join(l.rstrip() for l in seg)


def collect_file(tc: C.Toolchain, pkg_root: Path, file: Path, module_path: list[str], workers: int = 12) -> dict:
    """Summaries of every function of `file`. {"ok": bool, "error": str|None,
    "functions": {entity_id: {"summary": ..., "text": ..., "line": int}}}."""
    env = tc.env()
    if module_path:
        env["MOSS_MODULE_PATH"] = os.pathsep.join(module_path)
    sym, err = query(tc.moss, "symbols", None, file, pkg_root, env)
    if sym is None:
        return {"ok": False, "error": err, "functions": {}}
    lines = file.read_text().splitlines()
    real = str(file.resolve())
    ents = []
    for s in sym.get("symbols", []):
        src = s.get("source") or {}
        if s.get("kind") in SUMMARY_KINDS and src.get("file") and str(Path(src["file"]).resolve()) == real:
            ents.append((s["entity_id"], int(src.get("line") or 0)))
    ents.sort()

    def one(item):
        eid, line = item
        docs, errs = [], []
        for q in ("type", "ownership", "effects"):
            d, e = query(tc.moss, q, eid, file, pkg_root, env)
            docs.append(d)
            if e:
                errs.append(e)
        if errs:
            return eid, None, "; ".join(errs)
        return eid, {"summary": summarize_entity(docs[0], docs[1], docs[2]), "line": line,
                     "text": span_text(lines, line)}, None

    functions, errors = {}, []
    with ThreadPoolExecutor(max_workers=workers) as ex:
        for eid, rec, e in ex.map(one, ents):
            if e:
                errors.append(f"{eid}: {e}")
            else:
                key = eid
                n = 1
                while key in functions:
                    n += 1
                    key = f"{eid}#{n}"
                functions[key] = rec
    if errors:
        return {"ok": False, "error": f"{len(errors)} entity query failure(s), first: {errors[0]}", "functions": {}}
    return {"ok": True, "error": None, "functions": functions}


def source_files(pkg_root: Path) -> list[Path]:
    try:
        doc = tomllib.loads((pkg_root / "Moss.toml").read_text())
        src = (doc.get("build") or {}).get("source", "src")
    except (OSError, ValueError):
        src = "src"
    base = pkg_root / src
    return sorted(base.rglob("*.moss")) if base.is_dir() else []


# --------------------------------------------------------------------------
# classification (pure)
# --------------------------------------------------------------------------

def diff_categories(a: dict, b: dict) -> list[str]:
    return [c for c in CATEGORIES if a[c] != b[c]]


def classify(pre: dict[str, dict], post: dict[str, dict]) -> dict:
    """pre/post: {function key: {"summary": {...}, "text": str}} -> the churn
    record of one edit. Pure; deterministic (sorted ids)."""
    added = sorted(set(post) - set(pre))
    removed = sorted(set(pre) - set(post))
    common = sorted(set(pre) & set(post))
    edited = [k for k in common if pre[k]["text"] != post[k]["text"]]
    changed = {k: diff_categories(pre[k]["summary"], post[k]["summary"]) for k in common}
    changed = {k: v for k, v in changed.items() if v}
    edited_set = set(edited)
    own = {k: v for k, v in changed.items() if k in edited_set}
    other = {k: v for k, v in changed.items() if k not in edited_set}

    def cats(d):
        return sorted({c for v in d.values() for c in v})
    return {"functions_pre": len(pre), "functions_post": len(post), "common": len(common),
            "added": added, "removed": removed, "edited": edited,
            "changed": changed, "changed_own": own, "changed_other": other,
            "n_changed": len(changed), "n_changed_own": len(own), "n_changed_other": len(other),
            "categories_any": cats(changed), "categories_own": cats(own), "categories_other": cats(other),
            "any_change": bool(changed), "own_change": bool(own), "other_change": bool(other),
            "membership_only": bool(added or removed) and not changed}
