"""Validation shared by the Phase 27 conformance allowlist gates."""
from pathlib import Path


def missing_fixture_paths(root: Path, cases: list[dict]) -> list[str]:
    return sorted({case["path"] for case in cases
                   if not (root / case["path"]).is_file()})


def self_test() -> None:
    missing = missing_fixture_paths(Path("/phase27-no-such-root"),
                                    [{"path": "seed.moss"}])
    if missing != ["seed.moss"]:
        raise AssertionError("missing allowlist fixtures were not detected")
