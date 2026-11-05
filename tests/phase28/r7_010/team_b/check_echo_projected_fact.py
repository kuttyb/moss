"""Check projected module Echo lowering uses the full checked occurrence facts."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
MOSS = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
PROJECT = ROOT / "tmp/team-b/echo-projected-fact"
SOURCE = PROJECT / "src/provider.moss"

PROJECT.mkdir(parents=True, exist_ok=True)
(PROJECT / "src").mkdir(parents=True, exist_ok=True)
(PROJECT / "moss.toml").write_text(
    '[project]\nname = "echo_projected_provider"\nversion = "0.1.0"\n')
SOURCE.write_text(
    "module provider\n"
    "export fn show(values: Vector[String]) -> Int:\n"
    "  echo values[0]\n"
    "  return 1\n")


def build(env, expect=0):
    proc = subprocess.run([str(MARGO), "build", "--json"], cwd=PROJECT,
                          env=env, text=True, capture_output=True,
                          check=False, timeout=120)
    if proc.returncode != expect:
        raise RuntimeError(
            f"margo build returned {proc.returncode}, expected {expect}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    return proc.stdout + proc.stderr


def clean_build():
    shutil.rmtree(PROJECT / "build", ignore_errors=True)

base_env = dict(os.environ)
base_env["MOSS"] = str(MOSS)
clean_build()
output = build(base_env)
assert '"ok": true' in output, output

hooks = (
    "MOSS_TEST_CONSUMER_DROP_ECHO_ARGUMENT_FACT",
    "MOSS_TEST_CONSUMER_DROP_ECHO_ARGUMENT_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_ECHO_ARGUMENT_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_ECHO_ARGUMENT_REQUIREMENT",
    "MOSS_TEST_CONSUMER_PERTURB_ECHO_ARGUMENT_FACT",
    "MOSS_TEST_CONSUMER_WRONG_ECHO_ARGUMENT_CONTEXT",
    "MOSS_TEST_CONSUMER_PERTURB_ECHO_ARGUMENT_CONTEXT",
)
for hook in hooks:
    env = dict(base_env)
    env[hook] = "1"
    clean_build()
    result = build(env, expect=1)
    assert "MOSS_INTERNAL_INVARIANT" in result, (hook, result)

print("projected module Echo exact-fact lookup and seven fail-closed corruption gates PASS")
