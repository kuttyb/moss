#!/usr/bin/env python3
"""Regression for Phase 10 whole-project Fast Debug source closure."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="moss-phase10-project-") as temporary:
        root = Path(temporary)
        (root / "src").mkdir()
        (root / "moss.toml").write_text(
            '[project]\nname = "phase10_project"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n'
        )
        (root / "src" / "main.moss").write_text(
            "module app\nimport helper\n\n"
            "fn main():\n"
            "  assert helper.double(3) == 6\n"
            "  echo helper.double(4)\n"
        )
        (root / "src" / "helper.moss").write_text(
            "module helper\nimport detail\n\n"
            "export fn double(value: Int) -> Int:\n"
            "  detail.twice(value)\n"
        )
        (root / "src" / "detail.moss").write_text(
            "module detail\n\n"
            "export fn twice(value: Int) -> Int:\n"
            "  value * 2\n"
        )
        environment = dict(os.environ)
        environment["RUSTC"] = "/path/that/does/not/exist"
        for command in (("debug", "app"), ("run", "--interp", "src/main.moss")):
            result = subprocess.run(
                [str(compiler), *command], cwd=root, env=environment,
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                check=False,
            )
            if result.returncode != 0:
                raise AssertionError(result.stderr or result.stdout)
            if result.stdout != "8\n":
                raise AssertionError(
                    f"unexpected Fast Debug output for {command}: {result.stdout!r}"
                )
    print("Fast Debug whole-project closure checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
