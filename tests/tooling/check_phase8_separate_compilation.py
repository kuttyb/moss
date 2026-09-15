#!/usr/bin/env python3
"""Focused Phase 8 regression for real module crates and source independence."""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(compiler: Path, cwd: Path, *args: str) -> dict:
    process = subprocess.run(
        [str(compiler), *args], cwd=cwd, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )
    if process.returncode != 0:
        raise AssertionError(process.stdout)
    return json.loads(process.stdout)


def main() -> int:
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="moss-phase8-") as temporary:
        root = Path(temporary)
        (root / "src").mkdir()
        (root / "moss.toml").write_text(
            '[project]\nname = "phase8_regression"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n'
        )
        (root / "src" / "pricing.moss").write_text(
            "module pricing\n\n"
            "export fn notional(value: Int) -> Int:\n  value * 2\n\n"
            "export fn identity(value):\n  value\n"
        )
        (root / "src" / "main.moss").write_text(
            "module app\nimport pricing\n\n"
            "fn main():\n  value = pricing.identity(3)\n  echo value\n"
        )
        first = run(compiler, root, "build", "--json")
        artifacts = first["result"]["artifacts"]
        build = root / "build" / "debug"
        assert (build / "pricing.rs").exists()
        assert (build / "app.rs").exists()
        assert any(Path(path).name.startswith("libpricing") for path in artifacts["module_rlibs"])
        assert "fn pricing__notional" not in (build / "app.rs").read_text()
        hidden = root / "pricing.moss.hidden"
        shutil.move(root / "src" / "pricing.moss", hidden)
        second = run(compiler, root, "build", "--json")
        assert second["result"]["artifacts"]["module_interfaces"]
        executable = Path(second["result"]["artifacts"]["executable"])
        output = subprocess.check_output([str(executable)], text=True)
        assert output == "3\n", output
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
