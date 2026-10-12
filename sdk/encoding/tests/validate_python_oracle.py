#!/usr/bin/env python3
"""Deterministic differential corpus against Python's strict UTF-8 codec."""

import os
from pathlib import Path
import random
import shutil
import subprocess

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
WORK = ROOT / "tmp" / "phase24-encoding-python-oracle"
SEED = 240024


def moss_vector(values: list[int]) -> str:
    if not values:
        return "Vector[Int]()"
    return "[" + ", ".join(map(str, values)) + "]"


def run(command: list[str]) -> str:
    completed = subprocess.run(command, cwd=WORK, env=ENV, text=True,
                               capture_output=True, check=True)
    return completed.stdout


if WORK.exists():
    shutil.rmtree(WORK)
(WORK / "src").mkdir(parents=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(ROOT / "moss"), TMPDIR=str(ROOT / "tmp"),
           MARGO_HOME=str(ROOT / "tmp" / "margo-encoding-oracle"))
(WORK / "Moss.toml").write_text(
    '[package]\nname = "encoding_python_oracle"\nversion = "0.1.0"\n\n'
    '[build]\nsource = "src"\n\n[dependencies]\n'
    f'"moss.encoding" = {{ path = "{PACKAGE}" }}\n', encoding="utf-8")

rng = random.Random(SEED)
boundaries = [0, 1, 0x7F, 0x80, 0x7FF, 0x800, 0xD7FF,
              0xE000, 0xFFFF, 0x10000, 0x10FFFF, 0x301, 0x4E2D, 0x1F642]
cases: list[list[int]] = [[], boundaries]
for _ in range(64):
    values = []
    for _ in range(rng.randrange(0, 25)):
        value = rng.randrange(0, 0x110000)
        while 0xD800 <= value <= 0xDFFF:
            value = rng.randrange(0, 0x110000)
        values.append(value)
    cases.append(values)

invalid = [
    [-1], [256], [0x80], [0xBF], [0xC0, 0x80], [0xC1, 0xBF],
    [0xC2], [0xE0, 0x80, 0x80], [0xE2, 0x82], [0xED, 0xA0, 0x80],
    [0xF0, 0x80, 0x80, 0x80], [0xF0, 0x9F, 0x99],
    [0xF4, 0x90, 0x80, 0x80], [0xF5, 0x80, 0x80, 0x80], [0xFF],
]
for values in invalid:
    if all(0 <= value <= 255 for value in values):
        try:
            bytes(values).decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            pass
        else:
            raise AssertionError(f"invalid oracle vector decoded in Python: {values}")

lines = [
    "module app",
    "import encoding",
    "",
    "fn same_ints(left: Vector[Int], right: Vector[Int]) -> Bool:",
    "  if (left |> count) != (right |> count):",
    "    return false",
    "  var i = 0",
    "  while i < (left |> count):",
    "    if left[i] != right[i]:",
    "      return false",
    "    i = i + 1",
    "  return true",
    "",
    "fn verify() -> Bool:",
]
for index, scalars in enumerate(cases):
    expected = list("".join(chr(value) for value in scalars).encode("utf-8"))
    lines += [
        f"  encoded_{index} = encoding.encode_utf8({moss_vector(scalars)})",
        f"  assert(same_ints(encoded_{index}, {moss_vector(expected)}))",
        f"  decoded_{index} = encoding.decode_utf8({moss_vector(expected)})",
        f"  assert(same_ints(decoded_{index}, {moss_vector(scalars)}))",
    ]
for index, values in enumerate(invalid):
    lines.append(f"  assert(not encoding.validate_utf8({moss_vector(values)}))")
lines += [
    "  return true",
    "",
    "fn main():",
    "  try:",
    "    if verify():",
    '      echo "oracle-ok"',
    "  recover error:",
    '    echo "oracle-error"',
]
(WORK / "src" / "main.moss").write_text("\n".join(lines) + "\n", encoding="utf-8")

for command, label in (
    ([str(MARGO), "run"], "native-debug"),
    ([str(MARGO), "--release", "run"], "native-release"),
    ([str(MARGO), "debug"], "fast-debug"),
):
    output = run(command)
    if output != "oracle-ok\n":
        raise SystemExit(f"{label}: unexpected output {output!r}")
    print(f"{label}: Python UTF-8 oracle passed {len(cases)} valid and {len(invalid)} invalid cases")
