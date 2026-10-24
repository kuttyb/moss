#!/usr/bin/env python3
"""
Differential Test Harness between C++ Moss Compiler Oracle and Java Moss Compiler.
"""

import sys
import os
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

def compare_file(source_file, cpp_bin, java_run, mode="check"):
    source_path = str(Path(source_file).resolve())
    
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
    j_ret, j_out, j_err = run_cmd(java_cmd)

    match_exit = (c_ret == j_ret)
    if mode == "check_json":
        try:
            cj = json.loads(c_out)
            jj = json.loads(j_out)
            # compare keys ignoring compiler_version suffix
            cj.pop("compiler_version", None)
            jj.pop("compiler_version", None)
            match_out = (cj == jj)
        except Exception:
            match_out = (normalize_output(c_out) == normalize_output(j_out))
    else:
        match_out = (normalize_output(c_out) == normalize_output(j_out))
    
    details = {
        "file": source_file,
        "mode": mode,
        "cpp_exit": c_ret,
        "java_exit": j_ret,
        "exit_match": match_exit,
        "out_match": match_out,
        "cpp_out": c_out[:500],
        "java_out": j_out[:500],
        "cpp_err": c_err[:500],
        "java_err": j_err[:500]
    }
    return match_exit and match_out, details

def main():
    parser = argparse.ArgumentParser(description="Differential Test Runner")
    parser.add_argument("--cpp", default="./moss", help="Path to C++ moss compiler")
    parser.add_argument("--java", default="./scripts/java/run.sh", help="Path to Java compiler runner")
    parser.add_argument("--mode", default="check", choices=["check", "check_json", "interp", "compile"])
    parser.add_argument("files", nargs="+", help="Moss source files to compare")
    args = parser.parse_args()

    passed = 0
    failed = 0
    for f in args.files:
        ok, details = compare_file(f, args.cpp, args.java, args.mode)
        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {f} (cpp_exit={details['cpp_exit']}, java_exit={details['java_exit']})")
        if not ok:
            failed += 1
            print(f"  CPP out: {details['cpp_out']}")
            print(f"  JAVA out: {details['java_out']}")
            print(f"  CPP err: {details['cpp_err']}")
            print(f"  JAVA err: {details['java_err']}")
        else:
            passed += 1

    print(f"\nDifferential run finished: {passed} passed, {failed} failed.")
    if failed > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
