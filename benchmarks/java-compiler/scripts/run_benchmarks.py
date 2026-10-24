#!/usr/bin/env python3
"""
Comprehensive Benchmark Runner for Moss Java Compiler Build-System Shootout (Phase 25.3 J0)
Measures OpenJDK 25 direct javac, Mill 1.3.1 + Zinc, and Eclipse JDT Headless Incremental Builder.
Collects 15 warm trials, peak RSS, CPU %, rebuilt source files, and changed class counts.
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
CORPUS_DIR = BENCHMARK_DIR / "corpus"
LIB_DIR = BENCHMARK_DIR / "lib"
RESULTS_DIR = BENCHMARK_DIR / "results"
TMP_DIR = REPO_ROOT / "tmp"

sys.path.insert(0, str(SCRIPTS_DIR))
import mutations

# JDT paths
JDT_WORKSPACE = BENCHMARK_DIR / "jdt" / "workspace"
JDT_CONFIG = BENCHMARK_DIR / "jdt" / "configuration"
JDT_BIN = BENCHMARK_DIR / "jdt" / "bin"
JDT_PORT = 45289

# Mill paths
MILL_DIR = BENCHMARK_DIR / "mill"

# Javac paths
JAVAC_OUT = BENCHMARK_DIR / "javac" / "bin"

class JdtClient:
    def __init__(self, port=JDT_PORT):
        self.port = port
        self.proc = None

    def start_daemon(self):
        self.stop_daemon()
        cmd = [
            "/opt/eclipse-jdt/eclipse",
            "-nosplash",
            "-data", str(JDT_WORKSPACE),
            "-configuration", str(JDT_CONFIG),
            "-application", "org.mosslang.jdt.builder.headlessBuilder",
            "-port", str(self.port)
        ]
        log_file = open(RESULTS_DIR / "jdt_daemon.log", "w", encoding="utf-8")
        self.proc = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT)
        
        # Wait for daemon ready
        ready = False
        for _ in range(50):
            time.sleep(0.2)
            try:
                s = socket.socket()
                s.connect(("127.0.0.1", self.port))
                s.close()
                ready = True
                break
            except Exception:
                pass
        if not ready:
            raise RuntimeError("JDT Daemon failed to start within 10s")
        
        # Initialize project
        self.send_command(f"INIT {CORPUS_DIR} {LIB_DIR} {JDT_BIN}")

    def send_command(self, cmd_str):
        s = socket.socket()
        s.connect(("127.0.0.1", self.port))
        f = s.makefile("rw", encoding="utf-8")
        t0 = time.perf_counter()
        f.write(cmd_str.strip() + "\n")
        f.flush()
        resp = f.readline().strip()
        t1 = time.perf_counter()
        s.close()
        client_elapsed_ms = (t1 - t0) * 1000.0
        return resp, client_elapsed_ms

    def get_memory_rss_mb(self):
        if not self.proc:
            return 0.0
        try:
            res = subprocess.run(["pgrep", "-P", str(self.proc.pid)], capture_output=True, text=True)
            pids = res.stdout.strip().split()
            target_pid = pids[0] if pids else str(self.proc.pid)
            with open(f"/proc/{target_pid}/status") as f:
                for line in f:
                    if "VmRSS:" in line:
                        return float(line.split()[1]) / 1024.0
        except Exception:
            pass
        return 0.0

    def stop_daemon(self):
        try:
            s = socket.socket()
            s.connect(("127.0.0.1", self.port))
            f = s.makefile("rw", encoding="utf-8")
            f.write("STOP\n")
            f.flush()
            s.close()
        except Exception:
            pass
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()
            self.proc = None


def get_mill_daemon_rss_mb():
    try:
        res = subprocess.run(["pgrep", "-f", "mill.daemon.MillDaemonMain"], capture_output=True, text=True)
        pids = res.stdout.strip().split()
        if pids:
            with open(f"/proc/{pids[0]}/status") as f:
                for line in f:
                    if "VmRSS:" in line:
                        return float(line.split()[1]) / 1024.0
    except Exception:
        pass
    return 0.0


def run_javac_build(clean=False, changed_file=None):
    if clean:
        if JAVAC_OUT.exists():
            shutil.rmtree(JAVAC_OUT)
        JAVAC_OUT.mkdir(parents=True, exist_ok=True)

    sources = list(CORPUS_DIR.glob("**/*.java"))
    sources_txt = BENCHMARK_DIR / "javac" / "sources.txt"
    with open(sources_txt, "w", encoding="utf-8") as f:
        for s in sources:
            f.write(f'"{s}"\n')

    cp = f"{LIB_DIR}/*:{JAVAC_OUT}"
    if changed_file:
        cmd = ["/usr/bin/time", "-f", "%M %P", "javac", "--release", "25", "-cp", cp, "-d", str(JAVAC_OUT), str(changed_file)]
    else:
        cmd = ["/usr/bin/time", "-f", "%M %P", "javac", "--release", "25", "-cp", cp, "-d", str(JAVAC_OUT), f"@{sources_txt}"]

    t0 = time.perf_counter()
    res = subprocess.run(cmd, capture_output=True, text=True)
    t1 = time.perf_counter()
    wall_ms = (t1 - t0) * 1000.0

    rss_mb = 0.0
    cpu_pct = "100%"
    lines = res.stderr.strip().splitlines()
    if lines:
        parts = lines[-1].split()
        if len(parts) >= 2 and parts[0].isdigit():
            rss_mb = float(parts[0]) / 1024.0
            cpu_pct = parts[1]

    return {
        "success": res.returncode == 0,
        "wall_ms": wall_ms,
        "compile_ms": wall_ms,
        "rss_mb": rss_mb,
        "cpu_pct": cpu_pct,
        "stdout": res.stdout,
        "stderr": res.stderr
    }


def run_mill_build(clean=False):
    if clean:
        subprocess.run(["mill", "clean"], cwd=MILL_DIR, capture_output=True, text=True)

    t0 = time.perf_counter()
    res = subprocess.run(["mill", "compiler.compile"], cwd=MILL_DIR, capture_output=True, text=True)
    t1 = time.perf_counter()
    wall_ms = (t1 - t0) * 1000.0
    rss_mb = get_mill_daemon_rss_mb()

    recompiled_count = 0
    for line in res.stdout.splitlines():
        if "compiling" in line and "Java source" in line:
            parts = line.split()
            for i, p in enumerate(parts):
                if p == "compiling" and i + 1 < len(parts) and parts[i+1].isdigit():
                    recompiled_count = int(parts[i+1])

    return {
        "success": res.returncode == 0,
        "wall_ms": wall_ms,
        "compile_ms": wall_ms,
        "rss_mb": rss_mb,
        "recompiled_sources": recompiled_count,
        "stdout": res.stdout,
        "stderr": res.stderr
    }


def run_jdt_build(jdt_client: JdtClient, full=False):
    cmd = "BUILD_FULL" if full else "BUILD_INCREMENTAL"
    resp, client_ms = jdt_client.send_command(cmd)
    parts = resp.split()
    success = parts[0] == "SUCCESS"
    kv = {}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k] = v

    wall_ms = float(kv.get("wall_ms", client_ms))
    compile_ms = float(kv.get("compile_ms", wall_ms))
    changed_classes = int(kv.get("changed_classes", 0))
    errors = int(kv.get("errors", 0))
    rss_mb = jdt_client.get_memory_rss_mb()

    return {
        "success": success and errors == 0,
        "wall_ms": client_ms,
        "compile_ms": compile_ms,
        "rss_mb": rss_mb,
        "changed_classes": changed_classes,
        "raw_response": resp
    }


def verify_correctness(toolchain: str, class_dir: Path):
    fixture_test = BENCHMARK_DIR / "fixtures" / "CompilerFixtureTest.java"
    fixture_bin = BENCHMARK_DIR / "fixtures" / "bin"
    fixture_bin.mkdir(parents=True, exist_ok=True)
    cmd_compile = ["javac", "-cp", f"{LIB_DIR}/*:{CORPUS_DIR}", "-d", str(fixture_bin), str(fixture_test)]
    subprocess.run(cmd_compile, check=True, capture_output=True)

    cmd_run = ["java", "-cp", f"{class_dir}:{LIB_DIR}/*:{fixture_bin}", "org.mosslang.compiler.fixtures.CompilerFixtureTest"]
    res = subprocess.run(cmd_run, capture_output=True, text=True)
    if res.returncode != 0:
        raise AssertionError(f"Correctness fixture test failed for {toolchain}:\n{res.stderr}\n{res.stdout}")
    return True


def run_benchmark_matrix(trials=15):
    print("=" * 80)
    print(f"MOSS PHASE 25.3 J0: STARTING BENCHMARK MATRIX ({trials} WARM TRIALS / SCENARIO)")
    print("=" * 80)
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    mutations.backup_baseline()

    jdt_client = JdtClient()
    print("Starting JDT persistent daemon...")
    jdt_client.start_daemon()

    print("Pre-warming Mill daemon...")
    run_mill_build(clean=False)

    scenarios = ["B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10"]
    raw_results = {s: {"javac": [], "mill": [], "jdt": [], "rebuilt_files": {"mill": [], "jdt": []}, "rss_mb": {"javac": [], "mill": [], "jdt": []}} for s in scenarios}

    # B1: Cold clean build
    print("\n>>> Executing Scenario B1: Cold Clean Build")
    res_javac_cold = run_javac_build(clean=True)
    raw_results["B1"]["javac"].append(res_javac_cold["wall_ms"])
    raw_results["B1"]["rss_mb"]["javac"].append(res_javac_cold["rss_mb"])

    subprocess.run(["mill", "shutdown"], cwd=MILL_DIR, capture_output=True)
    res_mill_cold = run_mill_build(clean=True)
    raw_results["B1"]["mill"].append(res_mill_cold["wall_ms"])
    raw_results["B1"]["rss_mb"]["mill"].append(res_mill_cold["rss_mb"])

    jdt_client.stop_daemon()
    t0_cold = time.perf_counter()
    cmd_jdt_cold = [
        "/opt/eclipse-jdt/eclipse", "-nosplash",
        "-data", str(JDT_WORKSPACE),
        "-configuration", str(JDT_CONFIG),
        "-application", "org.mosslang.jdt.builder.headlessBuilder",
        "-action", "clean_build"
    ]
    subprocess.run(cmd_jdt_cold, capture_output=True, text=True)
    t1_cold = time.perf_counter()
    raw_results["B1"]["jdt"].append((t1_cold - t0_cold) * 1000.0)
    jdt_client.start_daemon()

    # B2 to B10: Warm trials
    warm_scenarios = ["B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10"]

    for sc in warm_scenarios:
        print(f"\n>>> Running Scenario {sc} ({trials} interleaved warm trials)...")
        for trial in range(1, trials + 1):
            mutations.restore_baseline()

            if sc == "B2":
                res_j = run_javac_build(clean=True)
                raw_results["B2"]["javac"].append(res_j["wall_ms"])
                raw_results["B2"]["rss_mb"]["javac"].append(res_j["rss_mb"])

                res_m = run_mill_build(clean=True)
                raw_results["B2"]["mill"].append(res_m["wall_ms"])
                raw_results["B2"]["rss_mb"]["mill"].append(res_m["rss_mb"])
                raw_results["B2"]["rebuilt_files"]["mill"].append(res_m["recompiled_sources"])

                res_jdt = run_jdt_build(jdt_client, full=True)
                raw_results["B2"]["jdt"].append(res_jdt["wall_ms"])
                raw_results["B2"]["rss_mb"]["jdt"].append(res_jdt["rss_mb"])
                raw_results["B2"]["rebuilt_files"]["jdt"].append(res_jdt["changed_classes"])

            else:
                mutations.apply_mutation(sc)

                # Toolchain A: javac
                res_j = run_javac_build(clean=False)
                raw_results[sc]["javac"].append(res_j["wall_ms"])
                raw_results[sc]["rss_mb"]["javac"].append(res_j["rss_mb"])

                # Toolchain B: Mill + Zinc
                res_m = run_mill_build(clean=False)
                raw_results[sc]["mill"].append(res_m["wall_ms"])
                raw_results[sc]["rss_mb"]["mill"].append(res_m["rss_mb"])
                raw_results[sc]["rebuilt_files"]["mill"].append(res_m["recompiled_sources"])

                # Toolchain C: JDT Headless Incremental Builder
                res_jdt = run_jdt_build(jdt_client, full=False)
                raw_results[sc]["jdt"].append(res_jdt["wall_ms"])
                raw_results[sc]["rss_mb"]["jdt"].append(res_jdt["rss_mb"])
                raw_results[sc]["rebuilt_files"]["jdt"].append(res_jdt["changed_classes"])

            if trial % 5 == 0 or trial == trials:
                print(f"  Trial {trial}/{trials}: javac={raw_results[sc]['javac'][-1]:.1f}ms, "
                      f"mill={raw_results[sc]['mill'][-1]:.1f}ms, jdt={raw_results[sc]['jdt'][-1]:.1f}ms")

        # Verify correctness on baseline after scenario
        mutations.restore_baseline()
        run_javac_build(clean=True)
        run_mill_build(clean=True)
        run_jdt_build(jdt_client, full=True)
        verify_correctness("javac", JAVAC_OUT)
        verify_correctness("mill", MILL_DIR / "out" / "compiler" / "compile.dest" / "classes")
        verify_correctness("jdt", JDT_BIN)

    # Stop JDT daemon
    jdt_client.stop_daemon()

    # Syntax Error Test
    print("\n>>> Testing Deliberate Syntax Error Rejection and Recovery...")
    mutations.apply_mutation("SYNTAX_ERROR")
    res_j_err = run_javac_build(clean=False)
    assert not res_j_err["success"], "Javac must reject syntax error"
    res_m_err = run_mill_build(clean=False)
    assert not res_m_err["success"], "Mill must reject syntax error"
    jdt_client.start_daemon()
    res_jdt_err = run_jdt_build(jdt_client, full=False)
    assert not res_jdt_err["success"], "JDT must reject syntax error"

    mutations.restore_baseline()
    assert run_javac_build(clean=False)["success"], "Javac must recover from syntax error"
    assert run_mill_build(clean=False)["success"], "Mill must recover from syntax error"
    assert run_jdt_build(jdt_client, full=False)["success"], "JDT must recover from syntax error"
    jdt_client.stop_daemon()
    print("Syntax error rejection and incremental recovery PASSED across all toolchains.")

    # Calculate summary statistics
    summary = {}
    for sc, data in raw_results.items():
        summary[sc] = {}
        for tool in ["javac", "mill", "jdt"]:
            times = data[tool]
            if not times:
                continue
            times_sorted = sorted(times)
            med = statistics.median(times)
            p95 = times_sorted[int(len(times_sorted) * 0.95)] if len(times_sorted) > 1 else times_sorted[0]
            std = statistics.stdev(times) if len(times) > 1 else 0.0
            avg_rss = statistics.mean(data["rss_mb"][tool]) if data["rss_mb"][tool] else 0.0
            summary[sc][tool] = {
                "median_ms": round(med, 2),
                "p95_ms": round(p95, 2),
                "min_ms": round(min(times), 2),
                "max_ms": round(max(times), 2),
                "stddev_ms": round(std, 2),
                "avg_rss_mb": round(avg_rss, 1),
                "trials": len(times)
            }

    with open(RESULTS_DIR / "raw_measurements.json", "w", encoding="utf-8") as f:
        json.dump(raw_results, f, indent=2)

    with open(RESULTS_DIR / "summary_results.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    desc_map = {
        "B1": "Cold clean build",
        "B2": "Warm clean build",
        "B3": "No-op build",
        "B4": "Private method edit",
        "B5": "Public method impl edit",
        "B6": "Public signature edit",
        "B7": "TypeRef change",
        "B8": "Semantic-checker change",
        "B9": "Multi-file edit (~20 files)",
        "B10": "Collection impl change"
    }

    print("\n" + "=" * 90)
    print(f"{'Scenario':<8} | {'Description':<28} | {'javac (ms)':<10} | {'Mill/Zinc':<10} | {'JDT (ms)':<10} | {'Winner':<8} | {'Speedup':<8}")
    print("-" * 90)
    csv_lines = ["Scenario,Description,javac_median_ms,mill_median_ms,jdt_median_ms,winner,speedup"]

    for sc in scenarios:
        s = summary[sc]
        j_med = s["javac"]["median_ms"]
        m_med = s["mill"]["median_ms"]
        jdt_med = s["jdt"]["median_ms"]

        best = min(j_med, m_med, jdt_med)
        if best == jdt_med:
            winner = "JDT"
            runner_up = min(j_med, m_med)
        elif best == m_med:
            winner = "Mill"
            runner_up = min(j_med, jdt_med)
        else:
            winner = "javac"
            runner_up = min(m_med, jdt_med)

        speedup = f"{runner_up / best:.2f}x" if best > 0 else "1.0x"
        print(f"{sc:<8} | {desc_map[sc]:<28} | {j_med:<10.1f} | {m_med:<10.1f} | {jdt_med:<10.1f} | {winner:<8} | {speedup:<8}")
        csv_lines.append(f"{sc},{desc_map[sc]},{j_med},{m_med},{jdt_med},{winner},{speedup}")

    print("=" * 90)
    with open(RESULTS_DIR / "summary_table.csv", "w", encoding="utf-8") as f:
        f.write("\n".join(csv_lines) + "\n")

    return summary


if __name__ == "__main__":
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 15
    run_benchmark_matrix(trials)
