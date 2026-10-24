#!/usr/bin/env python3
"""
Scale and Dependency Sensitivity Suite for Moss Java Compiler Shootout (Phase 25.3 J0)
Evaluates corpus sizes ~50, ~500, and ~1000 files across:
- Clean build scaling
- Single method implementation change (B4)
- Central TypeRef API change (B7)
- No-op build (B3)
"""

import os
import sys
import time
import json
import socket
import shutil
import subprocess
import statistics
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
BENCHMARK_DIR = REPO_ROOT / "benchmarks" / "java-compiler"
SCRIPTS_DIR = BENCHMARK_DIR / "scripts"
GENERATOR = BENCHMARK_DIR / "generator" / "generate_corpus.py"
LIB_DIR = BENCHMARK_DIR / "lib"
RESULTS_DIR = BENCHMARK_DIR / "results"
TMP_SCALE_DIR = REPO_ROOT / "tmp" / "scale"

sys.path.insert(0, str(SCRIPTS_DIR))
import mutations
from run_benchmarks import JdtClient, run_mill_build, run_jdt_build, get_mill_daemon_rss_mb, JDT_PORT

def run_scale_experiment(sizes=[50, 500, 1000], trials=5):
    print("=" * 80)
    print("MOSS PHASE 25.3 J0: SCALE SENSITIVITY BENCHMARK (~50, ~500, ~1000 FILES)")
    print("=" * 80)
    TMP_SCALE_DIR.mkdir(parents=True, exist_ok=True)
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    results = {}

    for size in sizes:
        print(f"\n==================== EVALUATING SCALE: {size} FILES ====================")
        corpus_dir = TMP_SCALE_DIR / f"corpus_{size}"
        javac_bin = TMP_SCALE_DIR / f"javac_{size}"
        jdt_bin = TMP_SCALE_DIR / f"jdt_{size}"
        jdt_ws = TMP_SCALE_DIR / f"jdt_ws_{size}"
        mill_dir = TMP_SCALE_DIR / f"mill_{size}"

        # 1. Generate corpus
        print(f"Generating synthetic corpus of {size} files...")
        subprocess.run([sys.executable, str(GENERATOR), "--target-files", str(size), "--seed", "42", "--output-dir", str(corpus_dir)], check=True, capture_output=True)

        # Count lines
        total_lines = 0
        for f in corpus_dir.glob("**/*.java"):
            with open(f, "r", encoding="utf-8") as fp:
                total_lines += len(fp.readlines())

        actual_files = len(list(corpus_dir.glob("**/*.java")))
        print(f"Generated {actual_files} files, {total_lines} lines.")

        # Setup Mill module for this scale
        mill_dir.mkdir(parents=True, exist_ok=True)
        build_mill = f"""package build
import mill._, javalib._

object compiler extends JavaModule {{
  def javacOptions = Seq("--release", "25")
  def zincIncrementalCompilation = Task {{ true }}
  def sources = Task.Sources {{
    os.Path("{corpus_dir.resolve()}")
  }}
  def unmanagedClasspath = Task.Input {{
    os.list(os.Path("{LIB_DIR.resolve()}"))
      .filter(_.ext == "jar")
      .map(PathRef(_))
      .toSeq
  }}
}}
"""
        (mill_dir / "build.mill").write_text(build_mill, encoding="utf-8")

        # Setup JDT daemon for this scale
        jdt_client = JdtClient(port=JDT_PORT + 1 + (size % 100))
        jdt_client.stop_daemon()
        if jdt_ws.exists():
            shutil.rmtree(jdt_ws)
        jdt_ws.mkdir(parents=True, exist_ok=True)
        jdt_bin.mkdir(parents=True, exist_ok=True)

        cmd_jdt = [
            "/opt/eclipse-jdt/eclipse", "-nosplash",
            "-data", str(jdt_ws),
            "-configuration", str(BENCHMARK_DIR / "jdt" / "configuration"),
            "-application", "org.mosslang.jdt.builder.headlessBuilder",
            "-port", str(jdt_client.port)
        ]
        log_f = open(TMP_SCALE_DIR / f"jdt_daemon_{size}.log", "w", encoding="utf-8")
        jdt_proc = subprocess.Popen(cmd_jdt, stdout=log_f, stderr=subprocess.STDOUT)
        jdt_client.proc = jdt_proc

        ready = False
        for _ in range(60):
            time.sleep(0.2)
            try:
                s = socket.socket()
                s.connect(("127.0.0.1", jdt_client.port))
                s.close()
                ready = True
                break
            except Exception:
                pass
        if not ready:
            print(f"Warning: JDT daemon for scale {size} failed to start. Skipping JDT for {size}.")

        if ready:
            jdt_client.send_command(f"INIT {corpus_dir} {LIB_DIR} {jdt_bin}")

        # Helper for javac at this scale
        def run_javac_scale(clean=False):
            if clean and javac_bin.exists():
                shutil.rmtree(javac_bin)
            javac_bin.mkdir(parents=True, exist_ok=True)
            sources = list(corpus_dir.glob("**/*.java"))
            src_txt = TMP_SCALE_DIR / f"sources_{size}.txt"
            with open(src_txt, "w", encoding="utf-8") as fp:
                for s in sources: fp.write(f'"{s}"\n')
            cp = f"{LIB_DIR}/*:{javac_bin}"
            cmd = ["javac", "--release", "25", "-cp", cp, "-d", str(javac_bin), f"@{src_txt}"]
            t0 = time.perf_counter()
            res = subprocess.run(cmd, capture_output=True, text=True)
            t1 = time.perf_counter()
            return (t1 - t0) * 1000.0, res.returncode == 0

        # Helper for mill at this scale
        def run_mill_scale(clean=False):
            if clean:
                subprocess.run(["mill", "clean"], cwd=mill_dir, capture_output=True)
            t0 = time.perf_counter()
            res = subprocess.run(["mill", "compiler.compile"], cwd=mill_dir, capture_output=True)
            t1 = time.perf_counter()
            return (t1 - t0) * 1000.0, res.returncode == 0

        # Helper for jdt at this scale
        def run_jdt_scale(full=False):
            if not ready:
                return -1.0, False
            cmd = "BUILD_FULL" if full else "BUILD_INCREMENTAL"
            resp, client_ms = jdt_client.send_command(cmd)
            return client_ms, "SUCCESS" in resp

        scale_res = {
            "file_count": actual_files,
            "source_lines": total_lines,
            "clean_warm": {"javac": [], "mill": [], "jdt": []},
            "no_op": {"javac": [], "mill": [], "jdt": []},
            "single_method_b4": {"javac": [], "mill": [], "jdt": []},
            "central_typeref_b7": {"javac": [], "mill": [], "jdt": []}
        }

        mut_mgr = mutations.MutationManager(corpus_path=corpus_dir)

        # 1. Warm clean build
        print("  Benchmarking Warm Clean Build...")
        for _ in range(trials):
            t_j, ok_j = run_javac_scale(clean=True)
            scale_res["clean_warm"]["javac"].append(t_j)
            t_m, ok_m = run_mill_scale(clean=True)
            scale_res["clean_warm"]["mill"].append(t_m)
            t_jdt, ok_jdt = run_jdt_scale(full=True)
            if ok_jdt: scale_res["clean_warm"]["jdt"].append(t_jdt)

        # 2. No-op build
        print("  Benchmarking No-op Build...")
        for _ in range(trials):
            t_j, ok_j = run_javac_scale(clean=False)
            scale_res["no_op"]["javac"].append(t_j)
            t_m, ok_m = run_mill_scale(clean=False)
            scale_res["no_op"]["mill"].append(t_m)
            t_jdt, ok_jdt = run_jdt_scale(full=False)
            if ok_jdt: scale_res["no_op"]["jdt"].append(t_jdt)

        # 3. Single method edit (B4)
        print("  Benchmarking Single Method Edit (B4)...")
        mut_mgr.restore()
        run_javac_scale(clean=False)
        run_mill_scale(clean=False)
        run_jdt_scale(full=True)
        for tr in range(trials):
            mut_mgr.apply_mutation("B4", trial=tr)
            t_j, ok_j = run_javac_scale(clean=False)
            scale_res["single_method_b4"]["javac"].append(t_j)
            t_m, ok_m = run_mill_scale(clean=False)
            scale_res["single_method_b4"]["mill"].append(t_m)
            t_jdt, ok_jdt = run_jdt_scale(full=False)
            if ok_jdt: scale_res["single_method_b4"]["jdt"].append(t_jdt)

        # 4. Central TypeRef API change (B7)
        print("  Benchmarking Central TypeRef API change (B7)...")
        mut_mgr.restore()
        run_javac_scale(clean=False)
        run_mill_scale(clean=False)
        run_jdt_scale(full=True)
        for tr in range(trials):
            mut_mgr.apply_mutation("B7", trial=tr)
            t_j, ok_j = run_javac_scale(clean=False)
            scale_res["central_typeref_b7"]["javac"].append(t_j)
            t_m, ok_m = run_mill_scale(clean=False)
            scale_res["central_typeref_b7"]["mill"].append(t_m)
            t_jdt, ok_jdt = run_jdt_scale(full=False)
            if ok_jdt: scale_res["central_typeref_b7"]["jdt"].append(t_jdt)

        mut_mgr.restore()
        jdt_client.stop_daemon()
        subprocess.run(["mill", "shutdown"], cwd=mill_dir, capture_output=True)

        # Calculate medians
        size_summary = {
            "files": actual_files,
            "lines": total_lines,
        }
        for task in ["clean_warm", "no_op", "single_method_b4", "central_typeref_b7"]:
            size_summary[task] = {
                "javac_median_ms": round(statistics.median(scale_res[task]["javac"]), 1),
                "mill_median_ms": round(statistics.median(scale_res[task]["mill"]), 1),
                "jdt_median_ms": round(statistics.median(scale_res[task]["jdt"]), 1) if scale_res[task]["jdt"] else -1.0
            }
        results[str(size)] = size_summary
        print(f"Results for {actual_files} files: {json.dumps(size_summary, indent=2)}")

    with open(RESULTS_DIR / "scale_sensitivity.json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    return results

if __name__ == "__main__":
    run_scale_experiment()
