#!/usr/bin/env python3
"""
Differential Test Harness between C++ Moss Compiler Oracle and Java Moss Compiler.
Supports single files, directory trees, negative corpus expectations, and structured diagnostic JSON comparison.
"""

import sys
import os
import glob
import subprocess
import json
import argparse
from pathlib import Path

def run_cmd(cmd, env=None):
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    return res.returncode, res.stdout, res.stderr

def normalize_output(text):
    lines = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        # Normalize temporary file paths
        if "tmp/" in line or "/tmp/" in line:
            continue
        lines.append(line)
    return "\n".join(lines)

def collect_files(inputs):
    """
    Expand directories recursively and resolve file paths and globs.
    """
    collected = []
    for item in inputs:
        p = Path(item)
        if p.is_dir():
            moss_files = sorted(p.rglob("*.moss"))
            collected.extend(str(f) for f in moss_files)
        elif p.is_file():
            collected.append(str(p))
        else:
            # Handle glob patterns if not expanded by shell
            matched = sorted(glob.glob(item, recursive=True))
            if matched:
                collected.extend(str(m) for m in matched if m.endswith(".moss") and os.path.isfile(m))
            else:
                collected.append(item)

    # Deduplicate while preserving order
    seen = set()
    result = []
    for f in collected:
        resolved = str(Path(f).resolve())
        if resolved not in seen:
            seen.add(resolved)
            result.append(f)
    return result

def parse_expectations(source_file):
    """
    Parse test expectation comments from source file.
    Supports formats:
      # EXPECT: ok=false
      # EXPECT: error.code=TYPE_MISMATCH
      # EXPECT: error.line=10
      # EXPECT: error.message_substr=expects 2 arguments
    And:
      # EXPECT_ERROR: TYPE_MISMATCH
      # EXPECT_LINE: 10
      # EXPECT_SUBSTR: expects 2 arguments
    """
    expectations = {
        "ok": None,
        "error.code": None,
        "error.line": None,
        "error.message_substr": None,
    }
    stem = Path(source_file).name
    if stem.startswith("neg_"):
        expectations["ok"] = False
    elif stem.startswith("pos_"):
        expectations["ok"] = True

    try:
        with open(source_file, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line.startswith("#"):
                    continue
                line = line.lstrip("#").strip()
                if line.startswith("EXPECT:"):
                    payload = line[len("EXPECT:"):].strip()
                    if "=" in payload:
                        k, v = payload.split("=", 1)
                        k = k.strip().lower()
                        v = v.strip().strip('"').strip("'")
                        if k == "ok":
                            expectations["ok"] = (v.lower() == "true")
                        elif k in ("error.code", "code"):
                            expectations["error.code"] = v
                        elif k in ("error.line", "line"):
                            try:
                                expectations["error.line"] = int(v)
                            except ValueError:
                                pass
                        elif k in ("error.message_substr", "error.message", "message_substr", "message", "substr"):
                            expectations["error.message_substr"] = v
                elif line.startswith("EXPECT_ERROR:"):
                    expectations["ok"] = False
                    expectations["error.code"] = line[len("EXPECT_ERROR:"):].strip().strip('"')
                elif line.startswith("EXPECT_LINE:"):
                    try:
                        expectations["error.line"] = int(line[len("EXPECT_LINE:"):].strip())
                    except ValueError:
                        pass
                elif line.startswith("EXPECT_SUBSTR:") or line.startswith("EXPECT_MESSAGE:"):
                    prefix = "EXPECT_SUBSTR:" if line.startswith("EXPECT_SUBSTR:") else "EXPECT_MESSAGE:"
                    expectations["error.message_substr"] = line[len(prefix):].strip().strip('"')
                elif line.startswith("EXPECT_OK"):
                    expectations["ok"] = True
    except Exception:
        pass

    return expectations

def parse_json_output(stdout, stderr):
    """
    Attempt to parse JSON from stdout first, then stderr.
    """
    for out in (stdout, stderr):
        text = out.strip()
        if not text:
            continue
        try:
            return json.loads(text)
        except Exception:
            # If output contains non-JSON prefix/suffix, attempt to extract JSON block
            first_brace = text.find("{")
            last_brace = text.rfind("}")
            if first_brace != -1 and last_brace != -1 and last_brace > first_brace:
                try:
                    return json.loads(text[first_brace:last_brace + 1])
                except Exception:
                    pass
    return None

def verify_oracle(expectations, c_ret, c_json, c_out, c_err):
    """
    Verify C++ oracle output against test expectations.
    """
    errors = []
    if expectations["ok"] is not None:
        if c_json is not None:
            c_ok = c_json.get("ok")
            if c_ok != expectations["ok"]:
                errors.append(f"Oracle ok mismatch: got {c_ok}, expected {expectations['ok']}")
        else:
            if expectations["ok"] and c_ret != 0:
                errors.append(f"Oracle exit code mismatch: got {c_ret}, expected 0")
            elif not expectations["ok"] and c_ret == 0:
                errors.append(f"Oracle exit code mismatch: got {c_ret}, expected non-zero")

    if c_json is not None and not c_json.get("ok", True):
        err_obj = c_json.get("error", {})
        if expectations["error.code"]:
            actual_code = err_obj.get("code")
            if actual_code != expectations["error.code"]:
                errors.append(f"Oracle error.code mismatch: got '{actual_code}', expected '{expectations['error.code']}'")
        if expectations["error.line"] is not None:
            actual_line = err_obj.get("line")
            if actual_line != expectations["error.line"]:
                errors.append(f"Oracle error.line mismatch: got {actual_line}, expected {expectations['error.line']}")
        if expectations["error.message_substr"]:
            actual_msg = err_obj.get("message", "")
            if expectations["error.message_substr"] not in actual_msg:
                errors.append(f"Oracle error.message does not contain '{expectations['error.message_substr']}': got '{actual_msg}'")

    return len(errors) == 0, errors

def compare_file(source_file, cpp_bin, java_run, mode="check", oracle_only=False):
    source_path = str(Path(source_file).resolve())
    expectations = parse_expectations(source_path)

    if mode == "check_json":
        cpp_cmd = [cpp_bin, "check", "--json", source_path]
        java_cmd = [java_run, "check", "--json", source_path]
    elif mode == "check":
        cpp_cmd = [cpp_bin, "check", source_path]
        java_cmd = [java_run, "check", source_path]
    elif mode == "interp":
        cpp_cmd = [cpp_bin, "run", "--interp", source_path]
        java_cmd = [java_run, "run", "--interp", source_path]
    elif mode == "compile":
        out_rs_cpp = f"tmp/diff_cpp_{Path(source_file).stem}.rs"
        out_rs_java = f"tmp/diff_java_{Path(source_file).stem}.rs"
        cpp_cmd = [cpp_bin, source_path, "-o", out_rs_cpp]
        java_cmd = [java_run, source_path, "-o", out_rs_java]
    else:
        raise ValueError(f"Unknown mode {mode}")

    c_ret, c_out, c_err = run_cmd(cpp_cmd)
    cj = parse_json_output(c_out, c_err) if mode == "check_json" else None

    oracle_ok, oracle_errors = verify_oracle(expectations, c_ret, cj, c_out, c_err)

    if oracle_only:
        details = {
            "file": source_file,
            "mode": mode,
            "expectations": expectations,
            "cpp_exit": c_ret,
            "java_exit": None,
            "oracle_ok": oracle_ok,
            "oracle_errors": oracle_errors,
            "exit_match": oracle_ok,
            "out_match": oracle_ok,
            "cpp_out": c_out,
            "java_out": "",
            "cpp_err": c_err,
            "java_err": "",
            "divergence_reasons": oracle_errors,
            "cj": cj,
            "jj": None
        }
        return oracle_ok, details

    j_ret, j_out, j_err = run_cmd(java_cmd)
    jj = parse_json_output(j_out, j_err) if mode == "check_json" else None

    match_exit = (c_ret == j_ret)
    divergence_reasons = []

    if not match_exit:
        divergence_reasons.append(f"Exit code mismatch: cpp={c_ret}, java={j_ret}")

    if mode == "check_json":
        if cj is not None and jj is not None:
            c_ok = cj.get("ok")
            j_ok = jj.get("ok")
            if c_ok != j_ok:
                divergence_reasons.append(f"Diagnostic 'ok' mismatch: cpp={c_ok}, java={j_ok}")
            if not c_ok and not j_ok:
                c_err_obj = cj.get("error", {})
                j_err_obj = jj.get("error", {})
                c_code = c_err_obj.get("code")
                j_code = j_err_obj.get("code")
                if c_code != j_code:
                    divergence_reasons.append(f"Diagnostic error.code mismatch: cpp='{c_code}', java='{j_code}'")
                c_line = c_err_obj.get("line")
                j_line = j_err_obj.get("line")
                if c_line != j_line:
                    divergence_reasons.append(f"Diagnostic error.line mismatch: cpp={c_line}, java={j_line}")
                if expectations["error.message_substr"]:
                    j_msg = j_err_obj.get("message", "")
                    if expectations["error.message_substr"] not in j_msg:
                        divergence_reasons.append(f"Java error.message missing '{expectations['error.message_substr']}': got '{j_msg}'")
            elif c_ok and j_ok:
                pass
            match_out = (len(divergence_reasons) == (0 if match_exit else 1))
        else:
            match_out = (normalize_output(c_out) == normalize_output(j_out))
            if not match_out:
                divergence_reasons.append("Non-JSON stdout mismatch")
    else:
        match_out = (normalize_output(c_out) == normalize_output(j_out))
        if not match_out:
            divergence_reasons.append("Output stdout mismatch")

    overall_ok = oracle_ok and match_exit and match_out

    details = {
        "file": source_file,
        "mode": mode,
        "expectations": expectations,
        "cpp_exit": c_ret,
        "java_exit": j_ret,
        "oracle_ok": oracle_ok,
        "oracle_errors": oracle_errors,
        "exit_match": match_exit,
        "out_match": match_out,
        "cpp_out": c_out,
        "java_out": j_out,
        "cpp_err": c_err,
        "java_err": j_err,
        "divergence_reasons": divergence_reasons,
        "cj": cj,
        "jj": jj
    }
    return overall_ok, details

def main():
    parser = argparse.ArgumentParser(description="Differential Test Runner between C++ and Java Moss Compilers")
    parser.add_argument("--cpp", default="./moss", help="Path to C++ moss compiler")
    parser.add_argument("--java", default="./scripts/java/run.sh", help="Path to Java compiler runner")
    parser.add_argument("--mode", default="check", choices=["check", "check_json", "interp", "compile"])
    parser.add_argument("--oracle-only", action="store_true", help="Only verify C++ compiler against test expectations")
    parser.add_argument("--allow-divergence", action="store_true", help="Record and report divergences without failing with exit 1")
    parser.add_argument("files", nargs="+", help="Moss source files, directories, or globs to test")
    args = parser.parse_args()

    file_list = collect_files(args.files)
    if not file_list:
        print("No .moss files found to run.")
        sys.exit(0)

    print(f"Running differential tests on {len(file_list)} file(s) in '{args.mode}' mode...")
    passed = 0
    diverged = 0
    oracle_failed = 0

    for f in file_list:
        ok, details = compare_file(f, args.cpp, args.java, args.mode, oracle_only=args.oracle_only)
        if not details["oracle_ok"]:
            oracle_failed += 1
            print(f"[ORACLE-FAIL] {f}")
            for err in details["oracle_errors"]:
                print(f"  - {err}")
        elif ok:
            passed += 1
            print(f"[PASS] {f} (cpp_exit={details['cpp_exit']}, java_exit={details['java_exit']})")
        else:
            diverged += 1
            status = "DIVERGED" if args.allow_divergence else "FAIL"
            print(f"[{status}] {f} (cpp_exit={details['cpp_exit']}, java_exit={details['java_exit']})")
            for reason in details["divergence_reasons"]:
                print(f"  Reason: {reason}")
            if details["expectations"]["error.code"]:
                print(f"  Expected: code={details['expectations']['error.code']}, line={details['expectations']['error.line']}, substr='{details['expectations']['error.message_substr']}'")
            if details["cj"] is not None:
                err_summary = details["cj"].get("error") if not details["cj"].get("ok") else "ok: true"
                print(f"  CPP JSON: {json.dumps(err_summary)}")
            elif details["cpp_out"].strip():
                print(f"  CPP out: {details['cpp_out'].strip()[:300]}")
            if details["jj"] is not None:
                err_summary = details["jj"].get("error") if not details["jj"].get("ok") else "ok: true"
                print(f"  JAVA JSON: {json.dumps(err_summary)}")
            elif details["java_out"].strip():
                print(f"  JAVA out: {details['java_out'].strip()[:300]}")
            if details["cpp_err"].strip():
                print(f"  CPP err: {details['cpp_err'].strip()[:200]}")
            if details["java_err"].strip():
                print(f"  JAVA err: {details['java_err'].strip()[:200]}")

    print(f"\nDifferential run finished: {passed} passed, {diverged} diverged, {oracle_failed} oracle failures.")
    if oracle_failed > 0:
        sys.exit(1)
    if diverged > 0 and not args.allow_divergence:
        sys.exit(1)

if __name__ == "__main__":
    main()
