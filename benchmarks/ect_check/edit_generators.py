"""Edit-sequence, cold-vs-warm and worktree generators (Phase 28.5, Team 3).

Registered in generate.py's GENERATORS. Three families, each producing ordinary
manifest fixtures (so the existing bench, gate and validate loops cover them)
plus frozen SCENARIO specs (manifest key `scenarios`):

  b3_edits      B3 Inference Ripple. The generic DAG (D=20, W=4, K=2 and the
                fan-in D=50, W=4, K=2) as ONE package whose leaves have a
                three-statement body; five frozen patches at a leaf:
                  a   body change, inferred summary unchanged      (k = 1 -> 2)
                  b   consumed fact: return type Int -> Float      (k = 1 -> 0.5)
                  b2  consumed fact: may_panic effect propagates   (r + k -> r / k)
                  c   provenance-only change: two statements swapped, so the
                      call's source line moves; no type/effect/ownership fact moves
                  d   comment-only change
                  e   one comment line inserted above the leaf (every later line shifts)
                Scenario per (shape, patch): check, apply, check, revert, check.
  b4_cold_warm  B4 Cold Tax. Floors (empty `main`, as a file and as a package),
                a 12-package sdk-style set and the B3 DAG base; scenario:
                check, check (no edit), apply a one-line edit, check, revert.
  b5_worktrees  B5. N = 1, 4, 16 real `git worktree` checkouts of one commit,
                each with a distinct small edit (frozen per-slot patches).

Scenario rows are frozen with a SHA-256 of their canonical JSON; fixture-level
hashes cover the base trees; patch rows carry the pre/post file hashes.
"""
from __future__ import annotations

from pathlib import Path
import random
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ect_common as C  # noqa: E402
from dag_generators import user_types  # noqa: E402

Generator = C.Generator

B3_DEPTH, B3_WIDTH, B3_TYPES = 20, 4, 2
B3F_DEPTH, B3F_WIDTH, B3F_TYPES = 50, 4, 2
PKGSET_PACKAGES, PKGSET_FUNCS = 12, 40
WORKTREE_COUNTS = (1, 4, 16)
WORKTREE_SLOTS = 16
WORKTREE_REP_STRIDE = 100

LEAF_HEADER_COMMENT = "  # leaf body\n"


def leaf_source(name: str, *, k: str = "1", swapped: bool = False, ret: str = "r + k",
                comment: str = "# leaf body") -> str:
    body = (f"  r = x.score()\n  k = {k}\n" if swapped else f"  k = {k}\n  r = x.score()\n")
    return f"fn {name}(x):\n  {comment}\n{body}  return {ret}\n"


def dag_source(depth: int, width: int) -> list[str]:
    """Spine DAG (W parallel chains under one root) with 3-statement leaves."""
    lines: list[str] = []
    for level in range(depth - 1, -1, -1):
        for i in range(width):
            name = f"n{level:03d}_{i}"
            if level == depth - 1:
                lines.append(leaf_source(name))
            else:
                lines.append(f"fn {name}(x):\n  return n{level + 1:03d}_{i}(x) + 1\n")
    lines.append("fn root(x):\n  return " + " + ".join(f"n000_{i}(x)" for i in range(width)) + "\n")
    return lines


def fanin_source(depth: int, width: int) -> list[str]:
    lines: list[str] = []
    for level in range(depth - 1, -1, -1):
        name = f"c{level:03d}"
        if level == depth - 1:
            lines.append(leaf_source(name))
        else:
            lines.append(f"fn {name}(x):\n  return c{level + 1:03d}(x) + 1\n")
    for i in range(width):
        lines.append(f"fn e{i}(x):\n  return c000(x) + {i}\n")
    lines.append("fn root(x):\n  return " + " + ".join(f"e{i}(x)" for i in range(width)) + "\n")
    return lines


def float_main(k: int) -> list[str]:
    """main sums into a Float so both an Int and a Float root result are legal."""
    return (["fn main():", "  var total = 0.0"] +
            [f"  total = total + root(T{i:03d}(v: {i + 1}))" for i in range(k)] +
            ["  echo total", ""])


def write_pkg(root: Path, name: str, main_source: str) -> None:
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(f'[project]\nname = "{name}"\nversion = "0.1.0"\n')
    (root / "src" / "main.moss").write_text(main_source)


def scenario_row(row: dict) -> dict:
    """Attach the freezing hash: SHA-256 of the row's canonical JSON."""
    row = dict(row)
    row.pop("sha256", None)
    row["sha256"] = C.text_sha256(C.canonical_json(row))
    return row


class B3EditsGenerator(Generator):
    name = "b3_edits"
    version = "1.0.0"
    category = "b3_edits"

    SHAPES = (
        ("dag", "b3_dag_d20_w4_k02", B3_DEPTH, B3_WIDTH, B3_TYPES, "n019_0", dag_source,
         "spine DAG: W parallel chains under one root, no shared callees"),
        ("fanin", "b3_fanin_d50_w4_k02", B3F_DEPTH, B3F_WIDTH, B3F_TYPES, "c049", fanin_source,
         "fan-in: W entries into one shared chain"),
    )
    # (id, label, classification, how the leaf text changes)
    EDITS = (
        ("a", "summary-unchanged body edit", "local", dict(k="2")),
        ("b", "consumed fact: return type Int -> Float", "cone", dict(k="0.5")),
        ("b2", "consumed fact: may_panic propagates", "cone", dict(ret="r / k")),
        ("c", "provenance-only: statement swap moves call line", "local", dict(swapped=True)),
        ("d", "comment-only", "none", dict(comment="# leaf body (reworded)")),
        ("e", "one comment line inserted ABOVE the leaf (line shift)", "identity", dict(prefix=True)),
    )

    def __init__(self):
        self.scenarios: list[dict] = []

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        self.scenarios = []
        base = rng.randint(1, 9)
        rows = []
        for shape, fid, depth, width, k, leaf, builder, blurb in self.SHAPES:
            d = out_dir / "b3_edits" / fid
            main_source = "module main\n\n" + "\n".join(
                user_types(k, base) + builder(depth, width) + float_main(k))
            write_pkg(d / "pkg", f"b3_{shape}", main_source)
            nodes = (depth * width + 1) if shape == "dag" else (depth + width + 1)
            rows.append({
                "id": fid, "path": f"b3_edits/{fid}", "kind": "package", "entry": "pkg",
                "timeout_s": 600, "expected_status": "pass",
                "metadata": {"family": f"b3_{shape}", "depth": depth, "width": width,
                             "types": k, "nodes": nodes, "packages": 1, "edit_leaf": leaf,
                             "predicted_functions_analyzed": nodes,
                             "predicted_spec_keys": nodes * k, "shape": blurb}})
            base_leaf = leaf_source(leaf)
            for eid, label, classification, change in self.EDITS:
                new_leaf = ("# inserted above the leaf\n" + base_leaf if change.get("prefix")
                            else leaf_source(leaf, **change))
                patch = C.make_patch(eid, d, [{
                    "file": "pkg/src/main.moss", "old": base_leaf, "new": new_leaf}])
                sid = f"b3_{shape}_{eid}"
                self.scenarios.append(scenario_row({
                    "id": sid, "family": "b3_edits", "fixture": fid, "kind": "edit_sequence",
                    "description": f"{label} at leaf {leaf} of the {blurb}",
                    "edited_function": leaf,
                    "classification": classification,
                    "patches": {eid: patch},
                    "steps": [
                        {"op": "check", "label": "base", "expect": "pass"},
                        {"op": "apply", "patch": eid},
                        {"op": "check", "label": "edit", "expect": "pass"},
                        {"op": "revert", "patch": eid},
                        {"op": "check", "label": "revert", "expect": "pass"}]}))
        return rows


class B3ErrorGenerator(Generator):
    """Team 5: edit `f` of B3, the error path. Scenarios only (no fixtures of its
    own): the frozen B3 fixtures are the base trees; the frozen B3 scenarios are not
    touched (this is a separate generator so their rows stay byte-identical).

    The break is a type error in the TOP node of the edited chain (DAG `n000_0`,
    fan-in `e0`): `child(x) + 1` -> `child(x) + "one"`. A break at the leaf, where
    the other B3 edits are, was probed first and is ruled out: the Moss check
    did not answer within 20 s for six different leaf breaks on both shapes
    (probes/f_edit_probe.txt). Step expectations: `edit` fails with exit code 1
    (margo), `revert` passes."""
    name = "b3_error"
    version = "1.0.0"
    category = "b3_error"

    BREAKS = (("dag", "b3_dag_d20_w4_k02", "n000_0", "fn n000_0(x):\n  return n001_0(x) + 1\n"),
              ("fanin", "b3_fanin_d50_w4_k02", "e0", "fn e0(x):\n  return c000(x) + 0\n"))

    def __init__(self):
        self.scenarios: list[dict] = []

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        import shutil
        self.scenarios = []
        ref = out_dir / "_b3_error_ref"
        B3EditsGenerator().generate(random.Random(f"{C.SUITE_SEED}:b3_edits"), ref)
        for shape, fid, node, old in self.BREAKS:
            new = old.replace("+ 1\n", '+ "one"\n').replace("+ 0\n", '+ "one"\n')
            assert new != old
            patch = C.make_patch("f", ref / "b3_edits" / fid, [{"file": "pkg/src/main.moss", "old": old, "new": new}])
            self.scenarios.append(scenario_row({
                "id": f"b3_{shape}_f", "family": "b3_error", "fixture": fid, "kind": "edit_sequence",
                "description": f"error path: type error (`+ \"one\"`) in the top node {node} of the "
                               f"{'spine DAG' if shape == 'dag' else 'fan-in'}",
                "edited_function": node, "classification": "error",
                "patches": {"f": patch},
                "steps": [
                    {"op": "check", "label": "base", "expect": "pass"},
                    {"op": "apply", "patch": "f"},
                    {"op": "check", "label": "edit", "expect": "fail", "expect_exit": 1,
                     "expect_code": "TYPE_AMBIGUOUS_OPEN_RESULT"},
                    {"op": "revert", "patch": "f"},
                    {"op": "check", "label": "revert", "expect": "pass"}]}))
        shutil.rmtree(ref, ignore_errors=True)
        return []


class B4ColdWarmGenerator(Generator):
    name = "b4_cold_warm"
    version = "1.0.0"
    category = "b4_cold_warm"

    def __init__(self):
        self.scenarios: list[dict] = []

    @staticmethod
    def pkgset(root: Path, rng: random.Random) -> dict:
        names = [f"ect_p{i:02d}" for i in range(PKGSET_PACKAGES)]
        for i, name in enumerate(names):
            pkg = root / f"p{i:02d}"
            (pkg / "src").mkdir(parents=True)
            (pkg / "Moss.toml").write_text(f'[project]\nname = "{name}"\nversion = "0.1.0"\n')
            lines = [f"module {name}", ""]
            for j in range(PKGSET_FUNCS):
                mult = rng.randint(2, 9)
                lines += [f"export fn f{j:02d}(a: Int, b: Int) -> Int:",
                          f"  return a * {mult} + b + {i + j}", ""]
            lines += ["export fn show(x):", "  return x.score() + 1", ""]
            (pkg / "src" / f"{name}.moss").write_text("\n".join(lines))
        app = root / "app"
        (app / "src").mkdir(parents=True)
        deps = "".join(f'{n} = {{ path = "../p{i:02d}" }}\n' for i, n in enumerate(names))
        (app / "Moss.toml").write_text(
            f'[project]\nname = "ect_pkgset_app"\nversion = "0.1.0"\n\n[dependencies]\n{deps}')
        terms = []
        for i, n in enumerate(names):
            terms += [f"{n}.f{(i * 3) % PKGSET_FUNCS:02d}(seed, {i + 1})", f"{n}.show(T(v: seed))"]
        main = ["module main", *[f"import {n}" for n in names], "",
                "type T:", "  v: Int", "", "  fn score() -> Int:", "    return v + 1", "",
                "fn main():", "  seed = 7", "  var total = 0"]
        main += [f"  total = total + {t}" for t in terms]
        main += ["  echo total", ""]
        (app / "src" / "main.moss").write_text("\n".join(main))
        return {"packages": PKGSET_PACKAGES + 1, "functions": PKGSET_PACKAGES * (PKGSET_FUNCS + 1)}

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        self.scenarios = []
        rows = []
        d = out_dir / "b4_cold_warm"
        # 1. floor, single file
        (d / "floor_file").mkdir(parents=True)
        (d / "floor_file" / "main.moss").write_text("fn main():\n  echo 0\n")
        rows.append({"id": "b4_floor_file", "path": "b4_cold_warm/floor_file", "kind": "file",
                     "entry": "main.moss", "expected_status": "pass",
                     "metadata": {"family": "b4_floor", "shape": "file", "nodes": 0,
                                  "note": "startup floor: an `main` that only echoes a literal"}})
        # 2. floor, package (margo + python startup included)
        write_pkg(d / "floor_pkg" / "pkg", "b4_floor_pkg", "module main\n\nfn main():\n  echo 0\n")
        rows.append({"id": "b4_floor_pkg", "path": "b4_cold_warm/floor_pkg", "kind": "package",
                     "entry": "pkg", "expected_status": "pass",
                     "metadata": {"family": "b4_floor", "shape": "package", "nodes": 0}})
        # 3. sdk-style package set
        info = self.pkgset(d / "pkgset", rng)
        rows.append({"id": "b4_pkgset_p12", "path": "b4_cold_warm/pkgset", "kind": "package",
                     "entry": "app", "timeout_s": 600, "expected_status": "pass",
                     "metadata": {"family": "b4_pkgset", **info,
                                  "note": "12 provider packages x 40 typed exports + 1 exported "
                                          "generic each; the app imports all and calls 2 per package"}})
        one_line = {
            "b4_floor_file": ("main.moss", "  echo 0\n", "  echo 1\n"),
            "b4_floor_pkg": ("pkg/src/main.moss", "  echo 0\n", "  echo 1\n"),
            "b4_pkgset_p12": ("app/src/main.moss", "  seed = 7\n", "  seed = 8\n"),
        }
        targets = list(one_line) + ["b3_dag_d20_w4_k02"]
        for fid in targets:
            if fid == "b3_dag_d20_w4_k02":
                # B3 is generated by another generator; its tree is reproduced
                # here only to compute the frozen patch hashes.
                tmp_rng = random.Random(f"{C.SUITE_SEED}:b3_edits")
                base = tmp_rng.randint(1, 9)
                text = "module main\n\n" + "\n".join(
                    user_types(B3_TYPES, base) + dag_source(B3_DEPTH, B3_WIDTH) + float_main(B3_TYPES))
                fdir = out_dir / "_b4_dag_ref"
                (fdir / "pkg" / "src").mkdir(parents=True, exist_ok=True)
                (fdir / "pkg" / "src" / "main.moss").write_text(text)
                edit = {"file": "pkg/src/main.moss", "old": leaf_source("n019_0"),
                        "new": leaf_source("n019_0", k="2")}
                patch = C.make_patch("edit", fdir, [edit])
            else:
                rel, old, new = one_line[fid]
                fx_dir = d / {"b4_floor_file": "floor_file", "b4_floor_pkg": "floor_pkg",
                              "b4_pkgset_p12": "pkgset"}[fid]
                patch = C.make_patch("edit", fx_dir, [{"file": rel, "old": old, "new": new}])
            self.scenarios.append(scenario_row({
                "id": f"b4_seq_{fid.removeprefix('b3_').removeprefix('b4_')}",
                "family": "b4_cold_warm", "fixture": fid, "kind": "edit_sequence",
                "description": "check, check again with no edit, one-line edit, check, revert",
                "classification": "local",
                "patches": {"edit": patch},
                "steps": [
                    {"op": "check", "label": "first", "expect": "pass"},
                    {"op": "check", "label": "second", "expect": "pass"},
                    {"op": "apply", "patch": "edit"},
                    {"op": "check", "label": "edit", "expect": "pass"},
                    {"op": "revert", "patch": "edit"}]}))
        import shutil
        shutil.rmtree(out_dir / "_b4_dag_ref", ignore_errors=True)
        return rows


class B5WorktreesGenerator(Generator):
    """No fixtures of its own: the committed project is an existing fixture."""
    name = "b5_worktrees"
    version = "1.0.0"
    category = "b5_worktrees"

    def __init__(self):
        self.scenarios: list[dict] = []

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        self.scenarios = []
        values = rng.sample(range(2, 60), WORKTREE_SLOTS)   # distinct, frozen
        base_text = {}
        # slot edits for the DAG: leaves n019_0..n019_3, k = value
        slots_dag = []
        for i in range(WORKTREE_SLOTS):
            leaf = f"n019_{i % B3_WIDTH}"
            slots_dag.append({"file": "pkg/src/main.moss", "old": leaf_source(leaf),
                              "new_fmt": leaf_source(leaf, k="{v}")})
        slots_pkgset = [{"file": "app/src/main.moss", "old": "  seed = 7\n",
                         "new_fmt": "  seed = {v}\n"}] * WORKTREE_SLOTS
        for fid, slots, what in (("b3_dag_d20_w4_k02", slots_dag, "leaf constant"),
                                 ("b4_pkgset_p12", slots_pkgset, "app `seed` constant")):
            self.scenarios.append(scenario_row({
                "id": f"b5_wt_{'dag' if 'dag' in fid else 'pkgset'}", "family": "b5_worktrees",
                "fixture": fid, "kind": "worktrees",
                "description": f"N real git worktrees of one commit, each with a distinct {what} edit",
                "counts": list(WORKTREE_COUNTS),
                "slots": slots, "values": values, "rep_stride": WORKTREE_REP_STRIDE,
                "commit": {"author": "ect-bench <ect-bench@example.invalid>",
                           "date": "2026-01-01T00:00:00+0000", "message": "ect-check base"}}))
        return []


EDIT_GENERATORS = [B3EditsGenerator(), B4ColdWarmGenerator(), B5WorktreesGenerator(), B3ErrorGenerator()]
