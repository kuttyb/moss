#!/usr/bin/env python3
"""Keep the tracked/example Moss corpus on current source syntax.

Scan source text (including comments) rather than historical Markdown or Rust.
Build products and project caches are not source examples. The two exact
migration fixtures must each contain exactly their one intentional keyword.
"""
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MIGRATIONS = {
    "tests/negative/await_retired.moss": "await",
    "tests/negative/spawn_retired.moss": "spawn",
}
KEYWORDS = re.compile(r"\b(await|spawn)\b")
ARTIFACT_DIRS = {"build", ".moss", ".git", "__pycache__"}


def audit(root):
    errors, allowed = [], []
    seen = set()
    for directory in ("examples", "tests", "benchmarks"):
        for source in sorted((root / directory).rglob("*.moss")):
            relative = source.relative_to(root)
            if not source.is_file() or ARTIFACT_DIRS.intersection(relative.parts[:-1]):
                continue
            path = relative.as_posix()
            occurrences = [(line, match.group())
                           for line, text in enumerate(source.read_text(encoding="utf-8").splitlines(), 1)
                           for match in KEYWORDS.finditer(text)]
            if path in MIGRATIONS:
                seen.add(path)
                if [token for _, token in occurrences] != [MIGRATIONS[path]]:
                    errors.append(f"{path}: expected exactly one {MIGRATIONS[path]} migration token")
                else:
                    line, token = occurrences[0]
                    allowed.append(f"{path}:{line}: {token} (intentional migration diagnostic)")
            else:
                errors.extend(f"{path}:{line}: unexpected retired keyword {token}"
                              for line, token in occurrences)
    errors.extend(f"missing migration fixture: {path}" for path in sorted(set(MIGRATIONS) - seen))
    return errors, allowed


def self_test():
    scratch = ROOT / "tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch, prefix="syntax-gate-") as temporary:
        root = Path(temporary)
        for path, token in MIGRATIONS.items():
            fixture = root / path
            fixture.parent.mkdir(parents=True, exist_ok=True)
            fixture.write_text(token + "\n")
        assert not audit(root)[0]
        for directory in ("examples/nested", "tests/project/src", "benchmarks/nested"):
            fixture = root / directory / "unexpected.moss"
            fixture.parent.mkdir(parents=True)
            fixture.write_text("# await\nvalue = spawn Domain()\n")
            assert len(audit(root)[0]) == 2
            fixture.unlink()
        # Names alone never grant an exception; removing a required fixture fails.
        fixture = root / "examples/await_retired.moss"
        fixture.write_text("await\n")
        assert audit(root)[0]
        fixture.unlink()
        (root / next(iter(MIGRATIONS))).unlink()
        assert audit(root)[0]


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        self_test()
    errors, allowed = audit(ROOT)
    if errors:
        print("Retired-syntax hygiene failed:\n" + "\n".join(errors), file=sys.stderr)
        sys.exit(1)
    print("Retired-syntax hygiene passed; remaining source occurrences:")
    print("\n".join(allowed))
