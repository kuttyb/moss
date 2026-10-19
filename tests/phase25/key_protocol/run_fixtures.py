"""Phase 25 key-protocol-v1 Map/Set acceptance fixtures (Team S S3.3 / S1.5).

Usage:
  python3 tests/phase25/key_protocol/run_fixtures.py [--moss PATH] [--activate] [--observe] [NAME...]

Each fixture directory holds an EXPECTED file (header lines, `---`, then the
expected stdout) and either `case.moss` (layout: standalone) or package
directories with an entry package (layout: packages).

Default mode verifies that each fixture's *current* behaviour is exactly the
one recorded on its `current:` line, and reports it:

  fail-closed CODE                    the checker rejects with CODE before
                                      native emission (pending activation)
  known-failing accepted-rustc-fails  the checker accepts and rustc rejects
                                      the generated Rust (a recorded defect)
  passes                              a control that already runs and matches
                                      EXPECTED in all three engines

Any drift exits 1: a fixture that becomes accepted must be activated, and a
known-failing fixture that starts failing closed or passing must have its
`current:` line updated deliberately.

`--activate` runs every fixture for real against EXPECTED (stdout in Fast
Debug, Native Debug, and Native Release; or a checker rejection before native
emission). It is expected to fail until Team G's G2 foundations pass and
`MossMap<K,V>` is available (see README.md).

`--observe` prints the observed status line of each fixture and never fails.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SCRATCH = ROOT / "tmp"
CHECKER_CODE = re.compile(r'\\?"code\\?": \\?"([A-Z][A-Z0-9_]+)')
BACKEND_CODES = {"BUILD_BACKEND_ERROR"}


def parse_expected(path):
    header, _, body = path.read_text().partition("\n---\n")
    fields = {}
    for line in header.splitlines():
        if line.strip() and not line.startswith("#"):
            key, _, value = line.partition(":")
            fields[key.strip()] = value.strip()
    fields["stdout"] = body
    for required in ("title", "covers", "layout", "expect", "current", "owner"):
        if required not in fields:
            raise SystemExit(f"{path}: missing '{required}:'")
    return fields


class Runner:
    def __init__(self, moss):
        self.moss = Path(moss).resolve()
        self.margo = self.moss.parent / "margo"
        if not self.margo.exists():
            self.margo = ROOT / "margo"
        self.env = {k: v for k, v in os.environ.items() if k != "MOSS_STAGED_STRING_REPR"}
        self.env["MOSS"] = str(self.moss)
        self.env["TMPDIR"] = str(SCRATCH)

    def run(self, cmd, cwd, env=None, timeout=900):
        return subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True,
                              timeout=timeout, env=env or self.env)

    # -- observation ---------------------------------------------------------

    def observe(self, fixture, fields, work):
        if fields["layout"] == "standalone":
            return self.observe_standalone(fixture / "case.moss", work)
        if fields["layout"] == "packages":
            return self.observe_packages(fixture, fields, work)
        raise SystemExit(f"{fixture.name}: unknown layout {fields['layout']}")

    def observe_standalone(self, source, work):
        case = work / "case.moss"
        shutil.copy2(source, case)
        check = self.run([self.moss, "check", case, "--json"], work)
        fast = self.run([self.moss, "run", "--interp", case], work)
        if check.returncode != 0:
            code = first_code(check.stdout)
            lowered = self.run([self.moss, case, "--debug", "-o", work / "d.rs"], work)
            leaked = fast.returncode == 0 or lowered.returncode == 0
            return {"status": f"fail-closed {code}" if not leaked else f"check-only-reject {code}",
                    "code": code, "fast": fast}
        outputs = {"fast-debug": fast}
        for mode, flag, rustc_flags in (("native-debug", "--debug", []),
                                        ("native-release", "-O", ["-O"])):
            rust, binary = work / f"{mode}.rs", work / mode
            gen = self.run([self.moss, case, flag, "-o", rust], work)
            if gen.returncode != 0:
                return {"status": f"lowering-reject {first_code(gen.stderr)}", "fast": fast}
            build = self.run(["rustc", *rustc_flags, "--crate-name", "case", rust, "-o", binary], work)
            if build.returncode != 0:
                return {"status": "known-failing accepted-rustc-fails", "fast": fast,
                        "detail": first_rustc_error(build.stderr)}
            outputs[mode] = self.run([binary], work)
        return {"status": "runs", "outputs": outputs, "fast": fast}

    def observe_packages(self, fixture, fields, work):
        shutil.copytree(fixture, work / "fx", ignore=shutil.ignore_patterns("build", "Moss.lock"))
        base = work / "fx"
        entry = base / fields.get("entry", "app")
        if fields.get("source-free"):
            return self.observe_source_free(base, entry, fields)
        build = self.run([self.margo, "--json", "build"], entry)
        fast = self.run([self.margo, "debug"], entry)
        if build.returncode != 0 or '"ok": false' in build.stdout:
            text = build.stdout + build.stderr
            code = first_code(text)
            if code in BACKEND_CODES:
                return {"status": "known-failing accepted-rustc-fails", "fast": fast,
                        "detail": first_rustc_error(text)}
            leaked = fast.returncode == 0
            return {"status": f"fail-closed {code}" if not leaked else f"check-only-reject {code}",
                    "code": code, "fast": fast}
        outputs = {"fast-debug": fast,
                   "native-debug": self.run([self.margo, "run"], entry),
                   "native-release": self.run([self.margo, "--release", "run"], entry)}
        return {"status": "runs", "outputs": outputs, "fast": fast}

    def observe_source_free(self, base, entry, fields):
        provider = base / fields["source-free"]
        for profile in ([], ["--release"]):
            build = self.run([self.margo, "--json", *profile, "build"], provider)
            if build.returncode != 0 or '"ok": false' in build.stdout:
                text = build.stdout + build.stderr
                code = first_code(text)
                if code in BACKEND_CODES:
                    return {"status": "known-failing accepted-rustc-fails",
                            "detail": "provider: " + first_rustc_error(text)}
                return {"status": f"fail-closed {code}", "code": code}
        for source in (provider / "src").glob("*.moss"):
            source.rename(source.with_suffix(".removed"))
        if fields.get("tamper"):
            self.tamper(provider, fields["tamper"])
        consumer = base / "source_free_consumer"
        shutil.copytree(entry / "src", consumer / "src")
        (consumer / "Moss.toml").write_text(
            '[project]\nname = "source_free_consumer"\nversion = "0.1.0"\n')
        outputs = {}
        for mode, profile, directory in (("native-debug", [], "debug"),
                                         ("native-release", ["--release"], "release")):
            env = dict(self.env, MOSS_MODULE_PATH=str(provider / "build" / directory))
            check = self.run([self.moss, "check", "src/main.moss", "--json"], consumer, env)
            if check.returncode != 0:
                code = first_code(check.stdout)
                return {"status": f"fail-closed {code}", "code": code}
            build = self.run([self.moss, "build", *profile, "--json"], consumer, env)
            if build.returncode != 0:
                code = first_code(build.stdout + build.stderr)
                if code in BACKEND_CODES:
                    return {"status": "known-failing accepted-rustc-fails",
                            "detail": first_rustc_error(build.stdout)}
                return {"status": f"fail-closed {code}", "code": code}
            executable = json.loads(build.stdout)["result"]["artifacts"]["executable"]
            outputs[mode] = self.run([executable], consumer, env)
        # ABI v10 section 9: Fast Debug rejects a source-free provider before
        # interpreting the consumer, so only the two native engines run here.
        return {"status": "runs", "outputs": outputs}

    @staticmethod
    def tamper(provider, spec):
        """spec: `FILE | LINE-REGEX | OLD | NEW` - replace literal OLD by NEW on
        every line of build/*/FILE that matches LINE-REGEX."""
        filename, selector, old, new = (part.strip() for part in spec.split("|"))
        changed = 0
        for interface in provider.glob(f"build/*/{filename}"):
            lines = interface.read_text().splitlines(keepends=True)
            for index, line in enumerate(lines):
                if re.search(selector, line) and old in line:
                    lines[index] = line.replace(old, new)
                    changed += 1
            interface.write_text("".join(lines))
        if not changed:
            raise SystemExit(f"tamper '{spec}' matched nothing in {provider}")

    # -- classification ------------------------------------------------------

    def classify(self, fields, observed):
        """Turn a 'runs' observation into 'passes' or 'accepted-wrong-output'."""
        if observed["status"] != "runs":
            return observed["status"]
        outputs = observed["outputs"]
        failed = [m for m, r in outputs.items() if r.returncode != 0]
        stdouts = {m: r.stdout for m, r in outputs.items()}
        if (not failed and fields["expect"] == "stdout"
                and set(stdouts.values()) == {fields["stdout"]}):
            return "passes"
        observed["detail"] = "; ".join(
            f"{m}: rc={outputs[m].returncode} {stdouts[m]!r}" for m in outputs)
        return "accepted-wrong-output"

    def meets_expectation(self, fields, status):
        expect = fields["expect"]
        if expect == "stdout":
            return status == "passes"
        if expect.startswith("reject "):
            code = expect.split()[1]
            if not status.startswith("fail-closed "):
                return False
            return code == "PROVISIONAL" or status.split()[1] == code
        raise SystemExit(f"unknown expect '{expect}'")


def first_code(text):
    match = CHECKER_CODE.search(text or "")
    if match:
        return match.group(1)
    bracket = re.search(r"\[([A-Z][A-Z0-9_]+)\]", text or "")
    return bracket.group(1) if bracket else "UNCODED"


def first_rustc_error(text):
    match = re.search(r"error\[E\d+\][^\n\\]*", text or "")
    return match.group(0) if match else "rustc failed"


def describe_fast(fields, observed):
    fast = observed.get("fast")
    if fast is None:
        return ""
    if fast.returncode == 0:
        same = fast.stdout == fields["stdout"] and fields["expect"] == "stdout"
        return f"Fast Debug runs ({'matches' if same else 'differs from'} EXPECTED: {fast.stdout!r})"
    message = (fast.stderr.strip().splitlines() or ["(no stderr)"])[-1]
    return f"Fast Debug rc={fast.returncode}: {message[:160]}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--moss", default=str(ROOT / "moss"))
    parser.add_argument("--activate", action="store_true")
    parser.add_argument("--observe", action="store_true")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    runner = Runner(args.moss)
    SCRATCH.mkdir(exist_ok=True)
    fixtures = sorted(p.parent for p in HERE.glob("*/EXPECTED"))
    if args.names:
        fixtures = [f for f in fixtures if f.name in args.names]
    mode = "activate" if args.activate else "observe" if args.observe else "verify-current"
    print(f"=== key-protocol-v1 Map/Set fixtures ({mode}, {len(fixtures)} fixtures) ===")
    problems = []
    for fixture in fixtures:
        fields = parse_expected(fixture / "EXPECTED")
        with tempfile.TemporaryDirectory(prefix="p25-keyproto-", dir=SCRATCH) as td:
            observed = runner.observe(fixture, fields, Path(td))
        status = runner.classify(fields, observed)
        detail = observed.get("detail", "")
        fast = describe_fast(fields, observed)
        if args.observe:
            print(f"  {fixture.name}: current: {status}" + (f" [{detail}]" if detail else "")
                  + (f" | {fast}" if fast else ""))
            continue
        if args.activate:
            ok = runner.meets_expectation(fields, status)
            print(f"  {'PASS' if ok else 'FAIL'}: {fixture.name} -> {status}"
                  + (f" [{detail}]" if detail and not ok else ""))
            if not ok:
                problems.append(fixture.name)
            continue
        recorded = fields["current"]
        if status != recorded:
            problems.append(fixture.name)
            print(f"  DRIFT: {fixture.name}: recorded '{recorded}', observed '{status}'"
                  + (f" [{detail}]" if detail else ""))
            continue
        if status.startswith("fail-closed"):
            label = f"pending activation ({status})"
        elif status.startswith("known-failing"):
            label = f"known-failing ({fields['owner']}): {status.split(' ', 1)[1]}"
            if detail:
                label += f" [{detail}]"
        elif status == "passes":
            label = "control passes in all engines"
        else:
            label = status
        print(f"  OK: {fixture.name}: {label}" + (f" | {fast}" if fast and 'known' in label else ""))
    if problems:
        print(f"=== {len(problems)} fixture(s) did not match: {', '.join(problems)} ===")
        return 1
    if not args.observe:
        print("=== all fixtures match their recorded status ===" if not args.activate
              else "=== all fixtures activated ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
