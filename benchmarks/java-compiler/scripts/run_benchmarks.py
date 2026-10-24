#!/usr/bin/env python3
"""
Comprehensive Benchmark Runner for Moss Java Compiler Build-System Shootout (Phase 25.3 J0-R1)
Strict Methodology Repairs:
- Distinct, deterministic source mutations per trial (no identical repeats / no no-ops).
- Real-time compiler activity verification (Zinc recompiled sources, JDT refreshed sources & generated classes).
- Runtime verification of mutated behavior directly against toolchain class outputs.
- Multiple cold clean trials (B1).
- Dependency invalidation and deliberate syntax error recovery controls.
"""

import os
import sys
import time
import json
import math
import socket
import shutil
import subprocess
import statistics
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
BENCHMARK_DIR = REPO_ROOT / "benchmarks" / "java-compiler"
SCRIPTS_DIR = BENCHMARK_DIR / "scripts"
CORPUS_DIR = BENCHMARK_DIR / "corpus"
LIB_DIR = BENCHMARK_DIR / "lib"
FIXTURES_DIR = BENCHMARK_DIR / "fixtures"
FIXTURES_BIN = FIXTURES_DIR / "bin"
RESULTS_DIR = BENCHMARK_DIR / "results"

sys.path.insert(0, str(SCRIPTS_DIR))
import mutations

# JDT paths
JDT_WORKSPACE = BENCHMARK_DIR / "jdt" / "workspace"
JDT_CONFIG = BENCHMARK_DIR / "jdt" / "configuration"
JDT_BIN = BENCHMARK_DIR / "jdt" / "bin"
JDT_PORT = 45289

# Mill paths
MILL_DIR = BENCHMARK_DIR / "mill"
MILL_CLASSES = MILL_DIR / "out" / "compiler" / "compile.dest" / "classes"

# Javac paths
JAVAC_OUT = BENCHMARK_DIR / "javac" / "bin"

ANSI_ESCAPE = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE.sub('', text)

def calc_p95(samples: list) -> float:
    if not samples:
        return 0.0
    s = sorted(samples)
    idx = max(0, int(math.ceil(0.95 * len(s))) - 1)
    return s[idx]

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


def run_javac_build(clean=False):
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
    cmd = ["/usr/bin/time", "-f", "%M %P", "javac", "--release", "25", "-sourcepath", "", "-cp", cp, "-d", str(JAVAC_OUT), f"@{sources_txt}"]

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
    clean_combined = strip_ansi(res.stdout + "\n" + res.stderr)
    for line in clean_combined.splitlines():
        if "compiling" in line and "Java source" in line:
            m = re.search(r"compiling\s+(\d+)\s+Java\s+source", line)
            if m:
                recompiled_count += int(m.group(1))

    return {
        "success": res.returncode == 0,
        "wall_ms": wall_ms,
        "compile_ms": wall_ms,
        "rss_mb": rss_mb,
        "recompiled_sources": recompiled_count,
        "stdout": res.stdout,
        "stderr": res.stderr
    }


def run_jdt_build(jdt_client: JdtClient, mode="incremental"):
    if mode == "clean":
        cmd = "BUILD_CLEAN"
    elif mode == "full":
        cmd = "BUILD_FULL"
    else:
        cmd = "BUILD_INCREMENTAL"

    resp, client_ms = jdt_client.send_command(cmd)
    parts = resp.split()
    success = len(parts) > 0 and parts[0] == "SUCCESS"
    kv = {}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k] = v

    wall_ms = float(kv.get("wall_ms", client_ms))
    compile_ms = float(kv.get("compile_ms", wall_ms))
    refreshed_sources = int(kv.get("refreshed_sources", 0))
    changed_classes = int(kv.get("changed_classes", 0))
    errors = int(kv.get("errors", 0))
    rss_mb = jdt_client.get_memory_rss_mb()

    return {
        "success": success and errors == 0,
        "wall_ms": client_ms,
        "compile_ms": compile_ms,
        "rss_mb": rss_mb,
        "refreshed_sources": refreshed_sources,
        "changed_classes": changed_classes,
        "errors": errors,
        "raw_response": resp
    }


def verify_mutation_behavior(toolchain_name: str, class_dir: Path, scenario: str, trial: int):
    """
    Runs MutationVerifier against the specific toolchain's class directory using reflection.
    Does NOT invoke javac on source files.
    """
    cp = f"{class_dir}:{FIXTURES_BIN}:{LIB_DIR}/*"
    cmd = ["java", "-cp", cp, "org.mosslang.compiler.fixtures.MutationVerifier", scenario, str(trial)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise AssertionError(f"Mutation verification failed for {toolchain_name} on {scenario} trial {trial}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
    return True


def verify_baseline_fixtures(toolchain: str, class_dir: Path):
    fixture_test = FIXTURES_DIR / "CompilerFixtureTest.java"
    FIXTURES_BIN.mkdir(parents=True, exist_ok=True)
    cmd_compile = ["javac", "--release", "25", "-sourcepath", "", "-cp", f"{class_dir}:{LIB_DIR}/*", "-d", str(FIXTURES_BIN), str(fixture_test)]
    subprocess.run(cmd_compile, check=True, capture_output=True)

    cmd_run = ["java", "-cp", f"{class_dir}:{LIB_DIR}/*:{FIXTURES_BIN}", "org.mosslang.compiler.fixtures.CompilerFixtureTest"]
    res = subprocess.run(cmd_run, capture_output=True, text=True)
    if res.returncode != 0:
        raise AssertionError(f"Baseline fixture test failed for {toolchain}:\n{res.stderr}\n{res.stdout}")
    return True


def run_benchmark_matrix(trials=15, cold_trials=5):
    print("=" * 80)
    print(f"MOSS PHASE 25.3 J0-R1: BENCHMARK SUITE ({trials} WARM TRIALS / SCENARIO, {cold_trials} COLD TRIALS)")
    print("=" * 80)
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    mut_mgr = mutations.MutationManager()
    mut_mgr.restore()

    # Ensure MutationVerifier is pre-compiled
    FIXTURES_BIN.mkdir(parents=True, exist_ok=True)
    subprocess.run(["javac", "--release", "25", "-sourcepath", "", "-cp", f"{CORPUS_DIR}:{LIB_DIR}/*", "-d", str(FIXTURES_BIN), str(FIXTURES_DIR / "MutationVerifier.java")], check=True)

    jdt_client = JdtClient()
    print("Starting JDT persistent daemon...")
    jdt_client.start_daemon()

    print("Pre-warming Mill daemon...")
    run_mill_build(clean=False)

    scenarios = ["B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10"]
    raw_results = {s: {
        "javac": [], "mill": [], "jdt": [],
        "rebuilt_files": {"mill": [], "jdt": []},
        "refreshed_sources": {"jdt": []},
        "rss_mb": {"javac": [], "mill": [], "jdt": []}
    } for s in scenarios}

    # =========================================================================
    # SCENARIO B1: Cold Clean Build (Multi-trial to characterize variance)
    # =========================================================================
    print(f"\n>>> Executing Scenario B1: Cold Clean Build ({cold_trials} cold trials)...")
    for ct in range(1, cold_trials + 1):
        # 1. javac cold
        res_j = run_javac_build(clean=True)
        assert res_j["success"], "Cold javac failed"
        raw_results["B1"]["javac"].append(res_j["wall_ms"])
        raw_results["B1"]["rss_mb"]["javac"].append(res_j["rss_mb"])

        # 2. mill cold
        subprocess.run(["mill", "shutdown"], cwd=MILL_DIR, capture_output=True)
        if (MILL_DIR / "out").exists():
            shutil.rmtree(MILL_DIR / "out")
        res_m = run_mill_build(clean=False)
        assert res_m["success"], "Cold Mill failed"
        raw_results["B1"]["mill"].append(res_m["wall_ms"])
        raw_results["B1"]["rss_mb"]["mill"].append(res_m["rss_mb"])

        # 3. jdt cold
        jdt_client.stop_daemon()
        if JDT_BIN.exists():
            shutil.rmtree(JDT_BIN)
        JDT_BIN.mkdir(parents=True, exist_ok=True)
        t0_cold = time.perf_counter()
        cmd_jdt_cold = [
            "/opt/eclipse-jdt/eclipse", "-nosplash",
            "-data", str(JDT_WORKSPACE),
            "-configuration", str(JDT_CONFIG),
            "-application", "org.mosslang.jdt.builder.headlessBuilder",
            "-action", "clean_build",
            "-src", str(CORPUS_DIR),
            "-lib", str(LIB_DIR),
            "-bin", str(JDT_BIN)
        ]
        res_jdt_proc = subprocess.run(cmd_jdt_cold, capture_output=True, text=True)
        t1_cold = time.perf_counter()
        assert res_jdt_proc.returncode == 0, f"Cold JDT failed: {res_jdt_proc.stderr}"
        raw_results["B1"]["jdt"].append((t1_cold - t0_cold) * 1000.0)

        jdt_client.start_daemon()
        print(f"  Cold Trial {ct}/{cold_trials}: javac={res_j['wall_ms']:.1f}ms, mill={res_m['wall_ms']:.1f}ms, jdt={(t1_cold - t0_cold)*1000.0:.1f}ms")

    # =========================================================================
    # SCENARIO B2: Persistent Process Warm Clean Build
    # =========================================================================
    print(f"\n>>> Executing Scenario B2: Persistent Warm Clean Build ({trials} trials)...")
    for trial in range(1, trials + 1):
        # 1. javac warm clean (empty out dir)
        res_j = run_javac_build(clean=True)
        assert res_j["success"]
        raw_results["B2"]["javac"].append(res_j["wall_ms"])
        raw_results["B2"]["rss_mb"]["javac"].append(res_j["rss_mb"])

        # 2. mill warm clean (mill clean + compile via daemon)
        res_m = run_mill_build(clean=True)
        assert res_m["success"]
        raw_results["B2"]["mill"].append(res_m["wall_ms"])
        raw_results["B2"]["rss_mb"]["mill"].append(res_m["rss_mb"])
        raw_results["B2"]["rebuilt_files"]["mill"].append(res_m["recompiled_sources"])

        # 3. jdt warm clean (BUILD_CLEAN via daemon)
        res_jdt = run_jdt_build(jdt_client, mode="clean")
        assert res_jdt["success"]
        raw_results["B2"]["jdt"].append(res_jdt["wall_ms"])
        raw_results["B2"]["rss_mb"]["jdt"].append(res_jdt["rss_mb"])
        raw_results["B2"]["rebuilt_files"]["jdt"].append(res_jdt["changed_classes"])
        raw_results["B2"]["refreshed_sources"]["jdt"].append(res_jdt["refreshed_sources"])

        if trial % 5 == 0 or trial == trials:
            print(f"  Warm Clean Trial {trial}/{trials}: javac={res_j['wall_ms']:.1f}ms, mill={res_m['wall_ms']:.1f}ms, jdt={res_jdt['wall_ms']:.1f}ms")

    # =========================================================================
    # SCENARIOS B3 to B10: Incremental Warm Trials (Distinct Revisions per Trial)
    # =========================================================================
    incremental_scenarios = ["B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10"]

    for sc in incremental_scenarios:
        print(f"\n>>> Running Scenario {sc} ({trials} incremental trials with distinct source revisions)...")
        # Ensure fresh baseline before scenario starts
        mut_mgr.restore()
        run_javac_build(clean=False)
        run_mill_build(clean=False)
        run_jdt_build(jdt_client, mode="full")

        for trial in range(trials):
            # Apply distinct mutation for this trial (transitioning from trial-1 to trial)
            audit = mut_mgr.apply_mutation(sc, trial=trial)
            if sc == "B3":
                assert len(audit) == 0, "B3 must not change any files"
            else:
                assert len(audit) > 0, f"{sc} trial {trial} must change source files"

            # 1. Toolchain A: javac
            res_j = run_javac_build(clean=False)
            assert res_j["success"], f"Javac build failed on {sc} trial {trial}: {res_j['stderr']}"
            raw_results[sc]["javac"].append(res_j["wall_ms"])
            raw_results[sc]["rss_mb"]["javac"].append(res_j["rss_mb"])
            if sc != "B3":
                verify_mutation_behavior("javac", JAVAC_OUT, sc, trial)

            # 2. Toolchain B: Mill + Zinc
            res_m = run_mill_build(clean=False)
            assert res_m["success"], f"Mill build failed on {sc} trial {trial}: {res_m['stderr']}"
            raw_results[sc]["mill"].append(res_m["wall_ms"])
            raw_results[sc]["rss_mb"]["mill"].append(res_m["rss_mb"])
            raw_results[sc]["rebuilt_files"]["mill"].append(res_m["recompiled_sources"])
            if sc != "B3":
                assert res_m["recompiled_sources"] >= 1, f"Mill recorded 0 recompiled sources on {sc} trial {trial}!"
                verify_mutation_behavior("mill", MILL_CLASSES, sc, trial)
            else:
                assert res_m["recompiled_sources"] == 0, "Mill recompiled files on no-op B3!"

            # 3. Toolchain C: JDT Headless Incremental Builder
            res_jdt = run_jdt_build(jdt_client, mode="incremental")
            assert res_jdt["success"], f"JDT build failed on {sc} trial {trial}: {res_jdt['raw_response']}"
            raw_results[sc]["jdt"].append(res_jdt["wall_ms"])
            raw_results[sc]["rss_mb"]["jdt"].append(res_jdt["rss_mb"])
            raw_results[sc]["rebuilt_files"]["jdt"].append(res_jdt["changed_classes"])
            raw_results[sc]["refreshed_sources"]["jdt"].append(res_jdt["refreshed_sources"])
            if sc != "B3":
                assert res_jdt["refreshed_sources"] >= 1, f"JDT refreshed 0 sources on {sc} trial {trial}!"
                assert res_jdt["changed_classes"] >= 1, f"JDT produced 0 changed classes on {sc} trial {trial}!"
                verify_mutation_behavior("jdt", JDT_BIN, sc, trial)
            else:
                assert res_jdt["refreshed_sources"] == 0, "JDT refreshed sources on no-op B3!"
                assert res_jdt["changed_classes"] == 0, "JDT produced changed classes on no-op B3!"

            if (trial + 1) % 5 == 0 or (trial + 1) == trials:
                print(f"  Trial {trial+1}/{trials}: javac={res_j['wall_ms']:.1f}ms, "
                      f"mill={res_m['wall_ms']:.1f}ms (recompiled={res_m['recompiled_sources']}), "
                      f"jdt={res_jdt['wall_ms']:.1f}ms (refreshed={res_jdt['refreshed_sources']}, classes={res_jdt['changed_classes']})")

        # Restore baseline and verify fixture tests
        mut_mgr.restore()
        run_javac_build(clean=False)
        run_mill_build(clean=False)
        run_jdt_build(jdt_client, mode="full")
        verify_baseline_fixtures("javac", JAVAC_OUT)
        verify_baseline_fixtures("mill", MILL_CLASSES)
        verify_baseline_fixtures("jdt", JDT_BIN)

    # =========================================================================
    # DEPENDENCY INVALIDATION CONTROL TEST
    # =========================================================================
    print("\n>>> Testing Dependency Invalidation Control (TypeRef.tagValue -> TypeSolver.computeTag)...")
    mut_mgr.apply_mutation("DEP_INVALIDATION", trial=0)
    res_j_dep = run_javac_build(clean=False)
    assert res_j_dep["success"]
    verify_mutation_behavior("javac", JAVAC_OUT, "DEP_INVALIDATION", 0)

    res_m_dep = run_mill_build(clean=False)
    assert res_m_dep["success"]
    assert res_m_dep["recompiled_sources"] >= 2, "Mill failed to invalidate downstream caller TypeSolver!"
    verify_mutation_behavior("mill", MILL_CLASSES, "DEP_INVALIDATION", 0)

    res_jdt_dep = run_jdt_build(jdt_client, mode="incremental")
    assert res_jdt_dep["success"]
    assert res_jdt_dep["changed_classes"] >= 2, "JDT failed to invalidate downstream caller TypeSolver!"
    verify_mutation_behavior("jdt", JDT_BIN, "DEP_INVALIDATION", 0)
    print("Dependency Invalidation Control PASSED across all toolchains (TypeSolver was correctly invalidated and recompiled).")
    mut_mgr.restore()

    # =========================================================================
    # DELIBERATE SYNTAX ERROR REJECTION AND RECOVERY TEST
    # =========================================================================
    print("\n>>> Testing Deliberate Syntax Error Rejection and Recovery...")
    mut_mgr.apply_mutation("SYNTAX_ERROR", trial=0)
    res_j_err = run_javac_build(clean=False)
    assert not res_j_err["success"], "Javac must reject syntax error"
    res_m_err = run_mill_build(clean=False)
    assert not res_m_err["success"], "Mill must reject syntax error"
    res_jdt_err = run_jdt_build(jdt_client, mode="incremental")
    assert not res_jdt_err["success"], "JDT must reject syntax error"

    mut_mgr.restore()
    assert run_javac_build(clean=False)["success"], "Javac must recover from syntax error"
    assert run_mill_build(clean=False)["success"], "Mill must recover from syntax error"
    assert run_jdt_build(jdt_client, mode="incremental")["success"], "JDT must recover from syntax error"
    print("Syntax error rejection and incremental recovery PASSED across all toolchains.")

    # Stop JDT daemon cleanly
    jdt_client.stop_daemon()

    # =========================================================================
    # SUMMARY CALCULATIONS & METRICS EXPORT
    # =========================================================================
    with open(RESULTS_DIR / "raw_measurements.json", "w", encoding="utf-8") as f:
        json.dump(raw_results, f, indent=2)

    summary = {}
    for sc, data in raw_results.items():
        summary[sc] = {}
        for tool in ["javac", "mill", "jdt"]:
            times = data[tool]
            if not times:
                continue
            times_sorted = sorted(times)
            med = statistics.median(times)
            p95 = calc_p95(times)
            stdev = statistics.stdev(times) if len(times) > 1 else 0.0
            avg_rss = statistics.mean(data["rss_mb"][tool]) if data["rss_mb"][tool] else 0.0
            summary[sc][tool] = {
                "median_ms": round(med, 2),
                "p95_ms": round(p95, 2),
                "min_ms": round(min(times), 2),
                "max_ms": round(max(times), 2),
                "stddev_ms": round(stdev, 2),
                "avg_rss_mb": round(avg_rss, 1),
                "trials": len(times)
            }

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
    cold_trials = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    run_benchmark_matrix(trials, cold_trials)
