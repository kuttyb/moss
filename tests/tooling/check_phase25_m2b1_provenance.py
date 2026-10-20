#!/usr/bin/env python3
"""Focused source-free ABI-v10 diagnostic provenance acceptance for M2b.1."""

import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
CONFORMANCE = ROOT / "tests/tooling/check_phase25_conformance.py"


def load_g2_helpers():
    saved = sys.argv[:]
    try:
        sys.argv = [str(CONFORMANCE), str(MOSS)]
        spec = importlib.util.spec_from_file_location("phase25_g2_conformance", CONFORMANCE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        sys.argv = saved


def check_case(g2, root, env, name, source_text, code, consumer_line,
               provider_symbol=None, provider_line=None, provider_identity=None,
               require_provider=True, forbid_provider=False):
    _, source = g2.consumer(root, name, source_text)
    assert not (root / "provider" / "src").exists(), "provider source remained available"
    result = g2.run(g2.MOSS, "check", source, "--json", env=env)
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(
            f"{name}: Moss did not return structured diagnostics: "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}") from exc
    error = data.get("error", {})
    failures = []
    if result.returncode == 0 or data.get("ok") is not False:
        failures.append(f"expected checker rejection, got returncode={result.returncode}")
    if error.get("code") != code:
        failures.append(f"expected code {code}, got {error.get('code')!r}")
    if error.get("source_file") != str(source):
        failures.append(f"primary source_file is not consumer source: {error.get('source_file')!r}")
    if error.get("line") != consumer_line:
        failures.append(f"primary line should be consumer line {consumer_line}: {error.get('line')!r}")
    primary_source = error.get("source") or {}
    if primary_source.get("file") != str(source) or primary_source.get("line") != consumer_line:
        failures.append(f"structured primary source does not identify consumer call: {primary_source!r}")

    related = error.get("related") or []
    provider_notes = []
    for note in related:
        origin = note.get("source") or {}
        file = origin.get("file", "")
        message = note.get("message", "")
        if Path(file).name == "provider.moss" and isinstance(origin.get("line"), int) and origin["line"] > 0:
            provider_notes.append(note)
    if require_provider and not provider_notes:
        failures.append("no structured related note links to provider.moss at a positive source line")
    if provider_symbol and not any(provider_symbol in note.get("message", "")
                                   for note in provider_notes):
        failures.append(f"provider provenance does not name {provider_symbol}")
    if provider_line is not None and not any(
            note.get("source", {}).get("line") == provider_line
            for note in provider_notes):
        failures.append(f"provider provenance does not identify declaration line {provider_line}")
    if provider_identity and not any(provider_identity in note.get("message", "")
                                     for note in provider_notes):
        failures.append("provider provenance does not include the semantic callable identity")
    if (provider_identity and ":fn:fresh" in provider_identity and
            f"MOSS_CHECKED_BODY_PROJECTED|{provider_identity}|checked-body-v1"
            not in result.stderr):
        failures.append("fresh callable did not use its checked-body projection")
    if forbid_provider and provider_notes:
        failures.append("ordinary local collection conflict acquired provider provenance")

    return {
        "case": name,
        "consumer_source": str(source),
        "provider_source_removed": not (root / "provider" / "src").exists(),
        "expected_code": code,
        "expected_consumer_line": consumer_line,
        "expect_provider_provenance": require_provider,
        "returncode": result.returncode,
        "compiler_stderr": result.stderr,
        "diagnostic": error,
        "failures": failures,
    }


def main():
    (ROOT / "tmp").mkdir(exist_ok=True)
    g2 = load_g2_helpers()
    evidence = {"compiler": str(MOSS), "abi_mode": "staged-v10", "cases": []}
    with tempfile.TemporaryDirectory(prefix="phase25-m2b1-", dir=ROOT / "tmp") as temp:
        scratch = Path(temp)
        mutator = scratch / "mutate_empty_vector_artifact"
        compiled_mutator = g2.run(
            "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
            "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
            "tests/tooling/mutate_phase25_r2c_empty_vector_artifact.cpp",
            "-o", mutator)
        assert compiled_mutator.returncode == 0, (
            compiled_mutator.stdout, compiled_mutator.stderr)
        env, artifacts = g2.source_free(scratch / "provider_setup", g2.PROVIDER_OPEN)
        assert not (scratch / "provider_setup" / "provider" / "src").exists()

        evidence["provider_artifacts"] = [path.name for path in artifacts.iterdir()]
        evidence["cases"].append(check_case(
            g2, scratch / "provider_setup", env, "ambiguous_relay",
            """module Consumer
import Provider
fn main():
  echo Provider.relay()
""",
            "TYPE_AMBIGUOUS_OPEN_RESULT", 4, "Provider.relay", 5,
            "callable-v1:module-v1:g2provider@0.1.0::Provider:fn:relay"))
        evidence["cases"].append(check_case(
            g2, scratch / "provider_setup", env, "conflicting_fresh_use",
            """module Consumer
import Provider
fn main():
  values = Provider.fresh()
  values.push(1)
  values.push("one")
""",
            "TYPE_MISMATCH", 6, "Provider.fresh", 2,
            "callable-v1:module-v1:g2provider@0.1.0::Provider:fn:fresh"))
        for name, mode in (
                ("missing_generic_source", "generic-source-missing"),
                ("forged_generic_source", "generic-source-forged")):
            variant_artifacts = scratch / f"artifacts-{name}"
            shutil.copytree(artifacts, variant_artifacts)
            mutated = g2.run(mutator, variant_artifacts / "Provider.mossi",
                             variant_artifacts / "libProvider.rlib", mode)
            assert mutated.returncode == 0, (name, mutated.stdout, mutated.stderr)
            variant_env = env.copy()
            variant_env["MOSS_MODULE_PATH"] = str(variant_artifacts)
            evidence["cases"].append(check_case(
                g2, scratch / "provider_setup", variant_env, name,
                """module Consumer
import Provider
fn main():
  values = Provider.fresh()
  values.push(1)
  values.push("one")
""",
                "TYPE_MISMATCH", 6, "Provider.fresh", 2,
                "callable-v1:module-v1:g2provider@0.1.0::Provider:fn:fresh"))
        evidence["cases"].append(check_case(
            g2, scratch / "provider_setup", env, "ordinary_local_conflict",
            """module Consumer
fn main():
  values = []
  values.push(1)
  values.push("one")
""",
            "TYPE_MISMATCH", 5, require_provider=False, forbid_provider=True))

    evidence_path = ROOT / "tmp/phase25_m2b1_diagnostics.json"
    evidence_path.write_text(json.dumps(evidence, indent=2) + "\n")
    failures = [f"{case['case']}: {failure}"
                for case in evidence["cases"] for failure in case["failures"]]
    if failures:
        print(json.dumps(evidence, indent=2), file=sys.stderr)
        print("PROVENANCE ACCEPTANCE FAIL: " + "; ".join(failures), file=sys.stderr)
        print(f"full diagnostics: {evidence_path}", file=sys.stderr)
        return 1
    for case in evidence["cases"]:
        print(f"PASS {case['case']}: {case['expected_code']} at consumer line "
              f"{case['expected_consumer_line']}" +
              ("; provider provenance retained" if case["expect_provider_provenance"] else ""))
    print(f"diagnostics: {evidence_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
