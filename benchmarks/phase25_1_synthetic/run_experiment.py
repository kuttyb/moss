#!/usr/bin/env python3
"""
Benchmark Runner for Moss Phase 25.1B: Source-Faithful Build-Speed Experiment.
Evaluates 10-TU asymmetric partition (Checker split into 3, Generator, Project Engine, etc.)
Runs 5 clean builds and 5 incremental rebuilds (Checker, Generator, Driver) for:
  - Configuration A: Baseline (Monolithic Unity, no PCH, no ETI)
  - Configuration B: Fully Optimized (10 Split TUs + ETI TU + Main, PCH, ETI, -j16)
Also analyzes:
  - Critical path, PCH creation time, longest TU, link time
  - Clang -ftime-trace breakdowns
"""

import os
import sys
import time
import json
import shutil
import subprocess
import statistics
from concurrent.futures import ThreadPoolExecutor

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GEN_DIR = os.path.join(SCRIPT_DIR, "generated")
DATA_DIR = os.path.join(SCRIPT_DIR, "data")
TRACES_DIR = os.path.join(SCRIPT_DIR, "traces")
TMP_BUILD_DIR = os.path.join(SCRIPT_DIR, "build_tmp")

COMPILER = os.path.abspath(os.path.join(SCRIPT_DIR, "../../tmp/toolchain/bin/clang++"))
if not os.path.exists(COMPILER):
    COMPILER = "clang++"

SUBSYSTEMS = [
    "core_diag",
    "parser",
    "abi_sourcefree",
    "checker_core",
    "checker_expr",
    "checker_ownership",
    "generator",
    "semantic_queries",
    "project_engine",
    "tooling_driver"
]

def run_proc(cmd):
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pid, status, rusage = os.wait4(p.pid, 0)
    t1 = time.perf_counter()
    out, err = p.communicate()
    return t1 - t0, rusage.ru_utime + rusage.ru_stime, rusage.ru_maxrss, out, err, p.returncode

def parse_trace(trace_path):
    if not os.path.exists(trace_path):
        return {}
    with open(trace_path, 'r') as f:
        data = json.load(f)
    events = data.get('traceEvents', [])
    totals = {}
    for e in events:
        name = e.get('name', '')
        if name.startswith('Total '):
            totals[name] = e.get('dur', 0) / 1e6

    template_inst = totals.get('Total InstantiateFunction', 0.0) + totals.get('Total InstantiateClass', 0.0)
    backend_codegen = totals.get('Total CodeGenPasses', 0.0)
    ir_gen = totals.get('Total OptModule', 0.0)
    parse_sema = totals.get('Total ParseDeclarationOrFunctionDefinition', 0.0)
    header_parse = totals.get('Total Source', 0.0)
    total_compile = totals.get('Total ExecuteCompiler', 0.0)

    return {
        'total_compile': total_compile,
        'template_inst': template_inst,
        'backend_codegen': backend_codegen,
        'ir_gen': ir_gen,
        'parse_sema': parse_sema,
        'header_parse': header_parse,
        'frontend': totals.get('Total Frontend', 0.0),
        'backend': totals.get('Total Backend', 0.0)
    }

def clean_dir(d):
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d, exist_ok=True)

# ----------------- Configuration A: Baseline -----------------
def build_config_a_clean(instrumented=False, trace_out_dir=None):
    clean_dir(TMP_BUILD_DIR)
    out_bin = os.path.join(TMP_BUILD_DIR, "baseline_bin")
    cmd = [
        COMPILER,
        "-std=c++17", "-O0", "-g0",
        "-Wall", "-Wextra", "-pedantic",
        "-I" + GEN_DIR
    ]
    if instrumented:
        cmd.extend(["-ftime-trace", "-ftime-trace-granularity=500"])
    cmd.extend([os.path.join(GEN_DIR, "unity.cpp"), "-o", out_bin])

    wall, cpu, rss, out, err, ret = run_proc(cmd)
    if ret != 0:
        raise RuntimeError(f"Config A failed: {err.decode('utf-8')}")

    trace_info = {}
    if instrumented:
        expected_trace = out_bin + "-unity.json"
        if not os.path.exists(expected_trace):
            expected_trace = os.path.join(TMP_BUILD_DIR, "unity.json")
        if os.path.exists(expected_trace):
            trace_info = parse_trace(expected_trace)
            if trace_out_dir:
                os.makedirs(trace_out_dir, exist_ok=True)
                shutil.copy(expected_trace, os.path.join(trace_out_dir, "config_a_unity.json"))

    return {
        'wall': wall,
        'cpu': cpu,
        'rss_kb': rss,
        'trace': trace_info
    }

def build_config_a_incremental(target_subsystem="checker_core"):
    target_file = os.path.join(GEN_DIR, f"subsystem_{target_subsystem}.cpp")
    with open(target_file, "r") as f:
        orig = f.read()
    try:
        mod = orig.replace("state.step_count++;", "state.step_count++; /* incremental edit */", 1)
        with open(target_file, "w") as f:
            f.write(mod)

        out_bin = os.path.join(TMP_BUILD_DIR, "baseline_bin")
        cmd = [
            COMPILER,
            "-std=c++17", "-O0", "-g0",
            "-Wall", "-Wextra", "-pedantic",
            "-I" + GEN_DIR,
            os.path.join(GEN_DIR, "unity.cpp"),
            "-o", out_bin
        ]
        wall, cpu, rss, out, err, ret = run_proc(cmd)
        if ret != 0:
            raise RuntimeError(f"Config A incremental failed: {err.decode('utf-8')}")
        return wall
    finally:
        with open(target_file, "w") as f:
            f.write(orig)

# ----------------- Configuration B: Fully Optimized -----------------
def build_config_b_clean(jobs=16, instrumented=False, trace_out_dir=None):
    clean_dir(TMP_BUILD_DIR)
    pch_header = os.path.join(GEN_DIR, "pch.hpp")
    pch_gch = os.path.join(TMP_BUILD_DIR, "pch.hpp.gch")

    # Step 1: PCH
    pch_cmd = [
        COMPILER,
        "-x", "c++-header",
        "-std=c++17", "-O0", "-g0",
        "-Wall", "-Wextra", "-pedantic",
        "-I" + GEN_DIR
    ]
    if instrumented:
        pch_cmd.extend(["-ftime-trace", "-ftime-trace-granularity=500"])
    pch_cmd.extend([pch_header, "-o", pch_gch])

    t_start = time.perf_counter()
    w_pch, c_pch, r_pch, _, err_pch, ret_pch = run_proc(pch_cmd)
    if ret_pch != 0:
        raise RuntimeError(f"Config B PCH creation failed: {err_pch.decode('utf-8')}")

    # Step 2: 12 TUs in parallel
    tu_files = [os.path.join(GEN_DIR, f"subsystem_{s}.cpp") for s in SUBSYSTEMS]
    tu_files.append(os.path.join(GEN_DIR, "template_instantiations.cpp"))
    tu_files.append(os.path.join(GEN_DIR, "main.cpp"))

    obj_files = []
    tu_cmds = []
    tu_names = []
    for src in tu_files:
        base = os.path.splitext(os.path.basename(src))[0]
        obj = os.path.join(TMP_BUILD_DIR, base + ".o")
        obj_files.append(obj)
        tu_names.append(base)
        cmd = [
            COMPILER,
            "-DUSE_ETI",
            "-std=c++17", "-O0", "-g0",
            "-Wall", "-Wextra", "-pedantic",
            "-I" + GEN_DIR,
            "-include-pch", pch_gch
        ]
        if instrumented:
            cmd.extend(["-ftime-trace", "-ftime-trace-granularity=500"])
        cmd.extend(["-c", src, "-o", obj])
        tu_cmds.append(cmd)

    total_tu_cpu = 0.0
    max_tu_rss = r_pch
    tu_wall_times = {}

    def compile_one(item):
        name, cmd = item
        w, c, r, o, e, ret = run_proc(cmd)
        if ret != 0:
            raise RuntimeError(f"TU compilation {name} failed: {e.decode('utf-8')}")
        return name, w, c, r

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(compile_one, zip(tu_names, tu_cmds)))

    for name, w, c, r in results:
        total_tu_cpu += c
        tu_wall_times[name] = w
        if r > max_tu_rss:
            max_tu_rss = r

    longest_tu_name, longest_tu_wall = max(tu_wall_times.items(), key=lambda x: x[1])

    # Step 3: Link
    out_bin = os.path.join(TMP_BUILD_DIR, "optimized_bin")
    link_cmd = [
        COMPILER,
        "-std=c++17", "-O0", "-g0"
    ] + obj_files + ["-o", out_bin]

    w_link, c_link, r_link, _, err_link, ret_link = run_proc(link_cmd)
    if ret_link != 0:
        raise RuntimeError(f"Config B linking failed: {err_link.decode('utf-8')}")

    t_end = time.perf_counter()
    total_wall = t_end - t_start
    total_cpu = c_pch + total_tu_cpu + c_link
    peak_rss = max(max_tu_rss, r_link)
    critical_path = w_pch + longest_tu_wall + w_link

    # Trace parsing
    trace_info = {}
    tu_traces = {}
    if instrumented:
        if trace_out_dir:
            os.makedirs(trace_out_dir, exist_ok=True)

        traces = []
        pch_trace = os.path.join(TMP_BUILD_DIR, "pch.hpp.json")
        if os.path.exists(pch_trace):
            traces.append(("pch", pch_trace))
        for obj in obj_files:
            tr = os.path.splitext(obj)[0] + ".json"
            if os.path.exists(tr):
                traces.append((os.path.basename(tr), tr))

        agg_trace = {
            'total_compile': 0.0,
            'template_inst': 0.0,
            'backend_codegen': 0.0,
            'ir_gen': 0.0,
            'parse_sema': 0.0,
            'header_parse': 0.0,
            'frontend': 0.0,
            'backend': 0.0
        }
        for name, p in traces:
            parsed = parse_trace(p)
            tu_traces[name] = parsed
            for k in agg_trace:
                agg_trace[k] += parsed.get(k, 0.0)
            if trace_out_dir:
                shutil.copy(p, os.path.join(trace_out_dir, name))

        trace_info = agg_trace

    return {
        'wall': total_wall,
        'cpu': total_cpu,
        'rss_kb': peak_rss,
        'pch_wall': w_pch,
        'link_wall': w_link,
        'longest_tu_name': longest_tu_name,
        'longest_tu_wall': longest_tu_wall,
        'critical_path': critical_path,
        'tu_walls': tu_wall_times,
        'trace': trace_info,
        'tu_traces': tu_traces
    }

def build_config_b_incremental(target_subsystem="checker_core"):
    pch_gch = os.path.join(TMP_BUILD_DIR, "pch.hpp.gch")
    target_file = os.path.join(GEN_DIR, f"subsystem_{target_subsystem}.cpp")
    with open(target_file, "r") as f:
        orig = f.read()

    try:
        mod = orig.replace("state.step_count++;", "state.step_count++; /* incremental edit */", 1)
        with open(target_file, "w") as f:
            f.write(mod)

        t0 = time.perf_counter()
        obj = os.path.join(TMP_BUILD_DIR, f"subsystem_{target_subsystem}.o")
        cmd = [
            COMPILER,
            "-DUSE_ETI",
            "-std=c++17", "-O0", "-g0",
            "-Wall", "-Wextra", "-pedantic",
            "-I" + GEN_DIR,
            "-include-pch", pch_gch,
            "-c", target_file,
            "-o", obj
        ]
        w, c, r, _, e, ret = run_proc(cmd)
        if ret != 0:
            raise RuntimeError(f"Config B incremental compile failed: {e.decode('utf-8')}")

        obj_files = [os.path.join(TMP_BUILD_DIR, f"subsystem_{s}.o") for s in SUBSYSTEMS]
        obj_files.append(os.path.join(TMP_BUILD_DIR, "template_instantiations.o"))
        obj_files.append(os.path.join(TMP_BUILD_DIR, "main.o"))
        out_bin = os.path.join(TMP_BUILD_DIR, "optimized_bin")
        link_cmd = [COMPILER, "-std=c++17", "-O0", "-g0"] + obj_files + ["-o", out_bin]
        w_l, c_l, r_l, _, e_l, ret_l = run_proc(link_cmd)
        if ret_l != 0:
            raise RuntimeError(f"Config B incremental link failed: {e_l.decode('utf-8')}")

        t1 = time.perf_counter()
        return t1 - t0
    finally:
        with open(target_file, "w") as f:
            f.write(orig)

# ----------------- Main Experiment -----------------
def run_all_experiments():
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(TRACES_DIR, exist_ok=True)

    print("=" * 70)
    print(" Moss Phase 25.1B: Source-Faithful Build-Speed Experiment")
    print("=" * 70)

    # 1. Config A: Baseline
    print("\n--- Running Configuration A: Baseline (Monolithic Unity) ---")
    a_clean_walls, a_clean_cpus, a_clean_rss = [], [], []
    for i in range(5):
        print(f"  [Clean Run {i+1}/5]...", end="", flush=True)
        res = build_config_a_clean(instrumented=False)
        a_clean_walls.append(res['wall'])
        a_clean_cpus.append(res['cpu'])
        a_clean_rss.append(res['rss_kb'])
        print(f" wall={res['wall']:.3f}s, cpu={res['cpu']:.3f}s, rss={res['rss_kb']/1024:.1f}MB")

    print("  [Instrumented Trace Run]...", end="", flush=True)
    trace_dir_a = os.path.join(TRACES_DIR, "config_a")
    res_trace_a = build_config_a_clean(instrumented=True, trace_out_dir=trace_dir_a)
    print(" done.")

    # Incremental runs for Checker, Generator, Driver
    a_incr_checker, a_incr_gen, a_incr_driver = [], [], []
    print("  [Incremental Runs (Checker, Generator, Driver)]...")
    for i in range(5):
        w_c = build_config_a_incremental("checker_core")
        a_incr_checker.append(w_c)
        w_g = build_config_a_incremental("generator")
        a_incr_gen.append(w_g)
        w_d = build_config_a_incremental("tooling_driver")
        a_incr_driver.append(w_d)
        print(f"    Run {i+1}: Checker={w_c:.3f}s, Generator={w_g:.3f}s, Driver={w_d:.3f}s")

    # 2. Config B: Fully Optimized
    print("\n--- Running Configuration B: Fully Optimized (10 TUs + PCH + ETI, -j16) ---")
    b_clean_walls, b_clean_cpus, b_clean_rss = [], [], []
    b_pch_walls, b_link_walls, b_crit_paths = [], [], []
    longest_tus = []
    for i in range(5):
        print(f"  [Clean Run {i+1}/5]...", end="", flush=True)
        res = build_config_b_clean(jobs=16, instrumented=False)
        b_clean_walls.append(res['wall'])
        b_clean_cpus.append(res['cpu'])
        b_clean_rss.append(res['rss_kb'])
        b_pch_walls.append(res['pch_wall'])
        b_link_walls.append(res['link_wall'])
        b_crit_paths.append(res['critical_path'])
        longest_tus.append((res['longest_tu_name'], res['longest_tu_wall']))
        print(f" wall={res['wall']:.3f}s, cpu={res['cpu']:.3f}s, longest_tu={res['longest_tu_name']} ({res['longest_tu_wall']:.3f}s)")

    print("  [Instrumented Trace Run]...", end="", flush=True)
    trace_dir_b = os.path.join(TRACES_DIR, "config_b")
    res_trace_b = build_config_b_clean(jobs=16, instrumented=True, trace_out_dir=trace_dir_b)
    print(" done.")

    b_incr_checker, b_incr_gen, b_incr_driver = [], [], []
    print("  [Incremental Runs (Checker, Generator, Driver)]...")
    for i in range(5):
        w_c = build_config_b_incremental("checker_core")
        b_incr_checker.append(w_c)
        w_g = build_config_b_incremental("generator")
        b_incr_gen.append(w_g)
        w_d = build_config_b_incremental("tooling_driver")
        b_incr_driver.append(w_d)
        print(f"    Run {i+1}: Checker={w_c:.3f}s, Generator={w_g:.3f}s, Driver={w_d:.3f}s")

    # 3. Medians
    med_a_wall = statistics.median(a_clean_walls)
    med_a_cpu = statistics.median(a_clean_cpus)
    med_a_rss = statistics.median(a_clean_rss)
    med_a_incr_c = statistics.median(a_incr_checker)
    med_a_incr_g = statistics.median(a_incr_gen)
    med_a_incr_d = statistics.median(a_incr_driver)

    med_b_wall = statistics.median(b_clean_walls)
    med_b_cpu = statistics.median(b_clean_cpus)
    med_b_rss = statistics.median(b_clean_rss)
    med_b_pch = statistics.median(b_pch_walls)
    med_b_link = statistics.median(b_link_walls)
    med_b_crit = statistics.median(b_crit_paths)
    med_b_incr_c = statistics.median(b_incr_checker)
    med_b_incr_g = statistics.median(b_incr_gen)
    med_b_incr_d = statistics.median(b_incr_driver)

    # Sort longest TU
    longest_tus.sort(key=lambda x: x[1], reverse=True)
    top_tu_name, top_tu_time = longest_tus[0]

    summary_data = {
        'config_a': {
            'clean_wall_med': med_a_wall,
            'clean_cpu_med': med_a_cpu,
            'peak_rss_mb': med_a_rss / 1024.0,
            'incr_checker_med': med_a_incr_c,
            'incr_generator_med': med_a_incr_g,
            'incr_driver_med': med_a_incr_d,
            'trace': res_trace_a['trace'],
            'raw_clean_walls': a_clean_walls
        },
        'config_b': {
            'clean_wall_med': med_b_wall,
            'clean_cpu_med': med_b_cpu,
            'peak_rss_mb': med_b_rss / 1024.0,
            'pch_creation_med': med_b_pch,
            'link_med': med_b_link,
            'critical_path_med': med_b_crit,
            'longest_tu': {'name': top_tu_name, 'wall_s': top_tu_time},
            'incr_checker_med': med_b_incr_c,
            'incr_generator_med': med_b_incr_g,
            'incr_driver_med': med_b_incr_d,
            'tu_walls': res_trace_b['tu_walls'],
            'trace': res_trace_b['trace']
        }
    }

    results_file = os.path.join(DATA_DIR, "results_v2.json")
    with open(results_file, "w") as f:
        json.dump(summary_data, f, indent=2)
    print(f"\nSaved raw data to {results_file}")

    print("\n" + "=" * 70)
    print(" PHASE 25.1B BENCHMARK RESULTS SUMMARY (Medians)")
    print("=" * 70)
    print(f"Clean Build Wall Time:        A={med_a_wall:.3f}s  |  B={med_b_wall:.3f}s  |  Speedup: {med_a_wall/med_b_wall:.2f}x")
    print(f"Compiler Aggregate CPU Time:  A={med_a_cpu:.3f}s  |  B={med_b_cpu:.3f}s  |  Ratio:   {med_b_cpu/med_a_cpu:.2f}x CPU")
    print(f"Checker Edit-Build Rebuild:   A={med_a_incr_c:.3f}s  |  B={med_b_incr_c:.3f}s  |  Speedup: {med_a_incr_c/med_b_incr_c:.2f}x")
    print(f"Generator Edit-Build Rebuild: A={med_a_incr_g:.3f}s  |  B={med_b_incr_g:.3f}s  |  Speedup: {med_a_incr_g/med_b_incr_g:.2f}x")
    print(f"Driver Edit-Build Rebuild:    A={med_a_incr_d:.3f}s  |  B={med_b_incr_d:.3f}s  |  Speedup: {med_a_incr_d/med_b_incr_d:.2f}x")
    print(f"Peak Memory (Process RSS):    A={med_a_rss/1024:.1f}MB |  B={med_b_rss/1024:.1f}MB |  Reduction: {med_a_rss/med_b_rss:.2f}x")
    print(f"Longest TU Compilation:       {top_tu_name} ({top_tu_time:.3f}s)")
    print(f"PCH Creation Time:            {med_b_pch:.3f}s")
    print(f"Linking Time:                 {med_b_link:.3f}s")
    print(f"Overall Critical Path:        {med_b_crit:.3f}s")
    print("=" * 70)

if __name__ == "__main__":
    run_all_experiments()
