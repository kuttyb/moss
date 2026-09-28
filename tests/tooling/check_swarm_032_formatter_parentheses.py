#!/usr/bin/env python3
"""Regression test for SWARM-032: Formatter rejects valid statements starting with '('."""

import pathlib
import subprocess
import sys
import tempfile

def main():
    compiler = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()

    # 1. Positive tests: valid grouped expressions
    positive_cases = [
        # Grouped arithmetic in while condition & return (original reproducer)
        """fn f(i: Int) -> Int:
  var p = 1
  while (i / p) % 2 == 0:
    p = p * 2
  return p
""",
        # Grouped arithmetic in return
        """fn wrap(a: Int, m: Int) -> Int:
  return (a + 1) % m
""",
        # Grouped comparisons
        """fn comparisons(a: Int, b: Int, c: Int, d: Int) -> Bool:
  if (a < b) and (c > d):
    return true
  return false
""",
        # Nested grouping
        """fn nested(x: Int) -> Int:
  return ((x + 1) * (x - 1)) + ((x * 2) / (x + 3))
""",
        # Parenthesized call expressions
        """fn add_one(x: Int) -> Int:
  return x + 1

fn parenthesized_call(a: Int) -> Int:
  return (add_one(a)) + 1
""",
        # Parenthesized pipeline operands
        """fn is_pos(x: Int) -> Bool:
  return x > 0

fn pipeline_grouped(values: Vector[Int]) -> Int:
  return (values |> filter(is_pos)) |> count
""",
    ]

    with tempfile.TemporaryDirectory(prefix="moss-swarm032-") as tmp:
        for idx, code in enumerate(positive_cases):
            src_path = pathlib.Path(tmp) / f"case_{idx}.moss"
            src_path.write_text(code)

            # moss fmt should succeed
            res = subprocess.run([str(compiler), "fmt", str(src_path)], capture_output=True, text=True)
            if res.returncode != 0:
                print(f"Error formatting positive case {idx}:\n{code}\nStderr: {res.stderr}")
                sys.exit(1)

            formatted1 = src_path.read_text()

            # Idempotence: formatting again produces identical text
            res2 = subprocess.run([str(compiler), "fmt", str(src_path)], capture_output=True, text=True)
            if res2.returncode != 0:
                print(f"Error re-formatting positive case {idx}:\nStderr: {res2.stderr}")
                sys.exit(1)

            formatted2 = src_path.read_text()
            if formatted1 != formatted2:
                print(f"Formatter not idempotent on case {idx}!\nFirst:\n{formatted1}\nSecond:\n{formatted2}")
                sys.exit(1)

            # Check that formatted code passes moss check
            res_chk = subprocess.run([str(compiler), "check", str(src_path)], capture_output=True, text=True)
            if res_chk.returncode != 0:
                print(f"moss check failed on formatted case {idx}:\n{formatted1}\nStderr: {res_chk.stderr}")
                sys.exit(1)

        # 2. Negative tests: malformed grouping must be rejected by moss fmt / check
        negative_cases = [
            # Unclosed parenthesis
            """fn bad_unclosed(a: Int) -> Int:
  return (a + 1
""",
            # Mismatched parenthesis
            """fn bad_mismatched(a: Int) -> Int:
  return (a + 1]
""",
            # Extra closing parenthesis
            """fn bad_extra(a: Int) -> Int:
  return (a + 1))
""",
        ]

        for idx, code in enumerate(negative_cases):
            src_path = pathlib.Path(tmp) / f"neg_case_{idx}.moss"
            src_path.write_text(code)

            res = subprocess.run([str(compiler), "fmt", str(src_path)], capture_output=True, text=True)
            if res.returncode == 0:
                print(f"Expected moss fmt to fail on malformed negative case {idx}:\n{code}")
                sys.exit(1)

    print("All SWARM-032 formatter parenthesis regressions passed!")

if __name__ == "__main__":
    main()
