#!/usr/bin/env python3
"""Check finite inline nominal layouts before native lowering."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"

CASES = {
    "self_cycle": (
        "type Node:\n  next: Node\n\nfn main():\n  pass\n",
        "TYPE_RECURSIVE_LAYOUT",
        "Node.next -> Node",
        2,
    ),
    "mutual_cycle": (
        "type First:\n  next: Second\n\ntype Second:\n  back: First\n\nfn main():\n  pass\n",
        "TYPE_RECURSIVE_LAYOUT",
        "First.next -> Second.back -> First",
        5,
    ),
    "cycle_after_prefix": (
        "type Prelude:\n  entry: First\n\ntype First:\n  next: Second\n\ntype Second:\n  back: First\n\nfn main():\n  pass\n",
        "TYPE_RECURSIVE_LAYOUT",
        "First.next -> Second.back -> First",
        8,
    ),
    "finite_chain": (
        "type Leaf:\n  value: Int\n\ntype Root:\n  leaf: Leaf\n\nfn main():\n  pass\n",
        None,
        None,
        None,
    ),
    "indirect_collection": (
        "type Tree:\n  children: Vector[Tree]\n\nfn main():\n  pass\n",
        None,
        None,
        None,
    ),
}


def main() -> None:
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="phase25-layout-", dir=ROOT / "tmp") as scratch:
        for name, (source, code, witness, line) in CASES.items():
            path = Path(scratch) / f"{name}.moss"
            path.write_text(source)
            result = subprocess.run(
                [str(COMPILER), "check", str(path), "--json"],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            payload = json.loads(result.stdout)
            if code is None:
                assert result.returncode == 0 and payload["ok"], (name, payload)
            else:
                error = payload["error"]
                assert result.returncode != 0 and not payload["ok"], (name, payload)
                assert error["code"] == code, (name, error)
                assert witness in error["message"], (name, error)
                assert error["line"] == line, (name, error)
                assert error["source_file"] == str(path), (name, error)
    print("PASS: Phase 25 recursive nominal layout checks")


if __name__ == "__main__":
    main()
