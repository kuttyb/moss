#!/usr/bin/env python3
"""Verify production clone sidecars bind Root-ingress captures to checker facts."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
OUT = ROOT / "tmp/phase28-root-ingress-provenance"
OUT.mkdir(parents=True, exist_ok=True)
HARNESS_PATH = ROOT / "tests/phase28/oracle_r7_005_1/oracle_harness.py"
spec = importlib.util.spec_from_file_location("phase28_oracle_harness", HARNESS_PATH)
oracle = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = oracle
spec.loader.exec_module(oracle)

FIXTURES = (
    "oracle_43_handler_primitive_reply.moss",
    "oracle_45_handler_primitive_rebind_local.moss",
)


def rejects_mutation(sidecar: Path, source: Path, mode: str,
                     copy_authority: dict | None, root_authority: dict,
                     mutate) -> None:
    original = json.loads(sidecar.read_text())
    candidate = json.loads(json.dumps(original))
    mutate(candidate)
    sidecar.write_text(json.dumps(candidate))
    audit = oracle.audit_clone_provenance(
        Path(str(sidecar)[:-len(".mossclone.json")]), source, mode, 0,
        copy_authority, root_authority)
    assert audit.get("status") == "INVALID_PROVENANCE", audit
    sidecar.write_text(json.dumps(original))


def main() -> None:
    fixture_dir = ROOT / "tests/phase28/oracle_r7_005_1/fixtures"
    cases = 0
    for filename in FIXTURES:
        source = fixture_dir / filename
        root_authority = oracle.query_root_ingress_capture_authority(COMPILER, source)
        assert root_authority.get("status") == "available", root_authority
        assert len(root_authority.get("facts", [])) == 1, root_authority
        copy_authority = oracle.query_copy_authority(COMPILER, source)
        for mode, flag in (("debug", []), ("optimized", ["-O"])):
            rust = OUT / f"{filename}.{mode}.rs"
            subprocess.run([str(COMPILER), *flag, str(source), "-o", str(rust)],
                           cwd=ROOT, check=True, capture_output=True, text=True)
            sidecar = Path(str(rust) + ".mossclone.json")
            manifest = json.loads(sidecar.read_text())
            assert len(manifest["root_ingress_domain_captures"]) == 1, manifest
            assert sum(row["origin_kind"] == "root_ingress_domain_capture"
                       for row in manifest["occurrences"]) == 1, manifest
            audit = oracle.audit_clone_provenance(
                rust, source, mode, 0, copy_authority, root_authority)
            assert audit.get("status") == "UNAUTHORIZED", audit
            assert audit.get("root_ingress_captures") == 1, audit
            assert audit.get("error") is None, audit
            cases += 1

            def mutate_drop(value):
                value["root_ingress_domain_captures"].clear()

            def mutate_duplicate(value):
                value["root_ingress_domain_captures"].append(
                    value["root_ingress_domain_captures"][0])

            def mutate_copy_authority(value):
                value["root_ingress_domain_captures"][0]["checked_fact"][
                    "permits_moss_value_copy"] = True

            def mutate_id(value):
                value["root_ingress_domain_captures"][0]["capture_id"] = \
                    "root-ingress-domain-capture-fact-v1:" + "0" * 64

            def mutate_span(value):
                value["root_ingress_domain_captures"][0]["rust_span"] = \
                    next(row["rust_span"] for row in value["occurrences"]
                         if row["origin_kind"] != "root_ingress_domain_capture")

            def mutate_occurrence(value):
                value["occurrences"] = [row for row in value["occurrences"]
                                         if row["origin_kind"] !=
                                         "root_ingress_domain_capture"]

            for mutation in (mutate_drop, mutate_duplicate, mutate_copy_authority,
                             mutate_id, mutate_span, mutate_occurrence):
                rejects_mutation(sidecar, source, mode, copy_authority,
                                 root_authority, mutation)
                cases += 1

            missing_authority = oracle.audit_clone_provenance(
                rust, source, mode, 0, copy_authority, None)
            assert missing_authority.get("status") == "PROVENANCE_MISSING", missing_authority
            cases += 1
    print(f"Phase 28 Root-ingress clone provenance: {cases} checks PASS")


if __name__ == "__main__":
    main()
