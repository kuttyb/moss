#!/usr/bin/env python3
"""
Experiment Runner for Moss Phase 25.1: Synthetic Build-Speed Benchmark.
Runs 5 clean builds and 5 incremental builds for:
  - Configuration A: Baseline (Unity build, monolithic, no PCH, no ETI)
  - Configuration B: Fully Optimized (8 Split TUs + ETI TU + Main, PCH, ETI, -j16)
Also measures:
  - Parallel scaling for split builds (-j1, -j4, -j8, -j16)
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
    "parsing",
    "sema",
    "types",
    "graph",
    "transform",
    "indexing",
    "serialize",
    "runtime"
]

def run_proc(cmd):
    """Run command and return (wall_time, cpu_time, max_rss_kb, stdout, stderr, returncode)."""
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pid, status, rusage = os.wait4(p.pid, 0)
    t1 = time.perf_counter()
    out, err = p.communicate()
    ret = p.returncode
    wall = t1 - t0
    cpu = rusage.ru_utime + rusage.ru_stime
    rss = rusage.ru_maxrss
    return wall, cpu, rss, out, err, ret

def parse_trace(trace_path):
    """Parse Clang -ftime-trace JSON."""
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
        # Clang writes trace as out_bin-unity.json or unity.json
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

def build_config_a_incremental():
    # Make a small modification to subsystem_sema.cpp
    target_file = os.path.join(GEN_DIR, "subsystem_sema.cpp")
    with open(target_file, "r") as f:
        original_content = f.read()

    try:
        # Insert a comment / harmless line
        mod_content = original_content.replace(
            "state.step_count++;",
            "state.step_count++; /* incremental edit */",
            1
        )
        with open(target_file, "w") as f:
            f.write(mod_content)

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
            f.write(original_content)

# ----------------- Configuration B: Fully Optimized -----------------
def build_config_b_clean(jobs=16, instrumented=False, trace_out_dir=None):
    clean_dir(TMP_BUILD_DIR)
    pch_header = os.path.join(GEN_DIR, "pch.hpp")
    pch_gch = os.path.join(TMP_BUILD_DIR, "pch.hpp.gch")

    # Step 1: PCH compilation
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

    # Step 2: Compile TUs in parallel
    tu_files = [os.path.join(GEN_DIR, f"subsystem_{s}.cpp") for s in SUBSYSTEMS]
    tu_files.append(os.path.join(GEN_DIR, "template_instantiations.cpp"))
    tu_files.append(os.path.join(GEN_DIR, "main.cpp"))

    obj_files = []
    tu_cmds = []
    for src in tu_files:
        base = os.path.splitext(os.path.basename(src))[0]
        obj = os.path.join(TMP_BUILD_DIR, base + ".o")
        obj_files.append(obj)
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
    tu_trace_results = []

    def compile_one(cmd):
        w, c, r, o, e, ret = run_proc(cmd)
        if ret != 0:
            raise RuntimeError(f"TU compilation failed: {e.decode('utf-8')}")
        return w, c, r, cmd

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(compile_one, tu_cmds))

    for w, c, r, cmd in results:
        total_tu_cpu += c
        if r > max_tu_rss:
            max_tu_rss = r

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

    # Trace parsing if instrumented
    trace_info = {}
    if instrumented:
        if trace_out_dir:
            os.makedirs(trace_out_dir, exist_ok=True)

        traces = []
        # PCH trace
        pch_trace = os.path.join(TMP_BUILD_DIR, "pch.hpp.json")
        if os.path.exists(pch_trace):
            traces.append(("pch", pch_trace))
        # TU traces
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
            for k in agg_trace:
                agg_trace[k] += parsed.get(k, 0.0)
            if trace_out_dir:
                shutil.copy(p, os.path.join(trace_out_dir, name))

        trace_info = agg_trace

    return {
        'wall': total_wall,
        'cpu': total_cpu,
        'rss_kb': peak_rss,
        'trace': trace_info
    }

def build_config_b_incremental():
    pch_gch = os.path.join(TMP_BUILD_DIR, "pch.hpp.gch")
    target_file = os.path.join(GEN_DIR, "subsystem_sema.cpp")
    with open(target_file, "r") as f:
        original_content = f.read()

    try:
        mod_content = original_content.replace(
            "state.step_count++;",
            "state.step_count++; /* incremental edit */",
            1
        )
        with open(target_file, "w") as f:
            f.write(mod_content)

        t0 = time.perf_counter()
        # 1. Recompile only subsystem_sema.cpp
        obj = os.path.join(TMP_BUILD_DIR, "subsystem_sema.o")
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

        # 2. Relink
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
            f.write(original_content)

# ----------------- Main Experiment -----------------
def run_all_experiments():
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(TRACES_DIR, exist_ok=True)

    print("=" * 60)
    print(" Moss Phase 25.1: Synthetic Build-Speed Experiment")
    print("=" * 60)

    # 1. Measure Config A (Baseline)
    print("\n--- Running Configuration A: Baseline (Monolithic Unity) ---")
    a_clean_walls = []
    a_clean_cpus = []
    a_clean_rss = []
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

    a_incr_walls = []
    for i in range(5):
        print(f"  [Incremental Run {i+1}/5]...", end="", flush=True)
        w = build_config_a_incremental()
        a_incr_walls.append(w)
        print(f" wall={w:.3f}s")

    # 2. Measure Config B (Fully Optimized: Split + PCH + ETI, -j16)
    print("\n--- Running Configuration B: Fully Optimized (Split + PCH + ETI, -j16) ---")
    b_clean_walls = []
    b_clean_cpus = []
    b_clean_rss = []
    for i in range(5):
        print(f"  [Clean Run {i+1}/5]...", end="", flush=True)
        res = build_config_b_clean(jobs=16, instrumented=False)
        b_clean_walls.append(res['wall'])
        b_clean_cpus.append(res['cpu'])
        b_clean_rss.append(res['rss_kb'])
        print(f" wall={res['wall']:.3f}s, cpu={res['cpu']:.3f}s, rss={res['rss_kb']/1024:.1f}MB")

    print("  [Instrumented Trace Run]...", end="", flush=True)
    trace_dir_b = os.path.join(TRACES_DIR, "config_b")
    res_trace_b = build_config_b_clean(jobs=16, instrumented=True, trace_out_dir=trace_dir_b)
    print(" done.")

    b_incr_walls = []
    for i in range(5):
        print(f"  [Incremental Run {i+1}/5]...", end="", flush=True)
        w = build_config_b_incremental()
        b_incr_walls.append(w)
        print(f" wall={w:.3f}s")

    # 3. Parallelism Scaling for Split Builds (-j1, -j4, -j8, -j16)
    print("\n--- Parallel Scaling Analysis for Split Build (Config B) ---")
    parallel_results = {}
    for j in [1, 4, 8, 16]:
        print(f"  Testing -j{j}...", end="", flush=True)
        times = []
        for _ in range(3):
            r = build_config_b_clean(jobs=j, instrumented=False)
            times.append(r['wall'])
        med_w = statistics.median(times)
        parallel_results[f"-j{j}"] = med_w
        print(f" median wall={med_w:.3f}s")

    # 4. Compile final summary data
    med_a_wall = statistics.median(a_clean_walls)
    med_a_cpu = statistics.median(a_clean_cpus)
    med_a_rss = statistics.median(a_clean_rss)
    med_a_incr = statistics.median(a_incr_walls)

    med_b_wall = statistics.median(b_clean_walls)
    med_b_cpu = statistics.median(b_clean_cpus)
    med_b_rss = statistics.median(b_clean_rss)
    med_b_incr = statistics.median(b_incr_walls)

    summary_data = {
        'config_a': {
            'clean_wall_med': med_a_wall,
            'clean_cpu_med': med_a_cpu,
            'peak_rss_mb': med_a_rss / 1024.0,
            'incr_wall_med': med_a_incr,
            'trace': res_trace_a['trace'],
            'raw_clean_walls': a_clean_walls,
            'raw_clean_cpus': a_clean_cpus,
            'raw_incr_walls': a_incr_walls
        },
        'config_b': {
            'clean_wall_med': med_b_wall,
            'clean_cpu_med': med_b_cpu,
            'peak_rss_mb': med_b_rss / 1024.0,
            'incr_wall_med': med_b_incr,
            'trace': res_trace_b['trace'],
            'raw_clean_walls': b_clean_walls,
            'raw_clean_cpus': b_clean_cpus,
            'raw_incr_walls': b_incr_walls
        },
        'parallel_scaling': parallel_results
    }

    results_file = os.path.join(DATA_DIR, "results.json")
    with open(results_file, "w") as f:
        json.dump(summary_data, f, indent=2)
    print(f"\nSaved raw data to {results_file}")

    # Print Table
    print("\n" + "=" * 60)
    print(" BENCHMARK RESULTS SUMMARY (Medians)")
    print("=" * 60)
    header = f"| {'Metric':35s} | {'A: Baseline':15s} | {'B: Fully Optimized':18s} | {'Speedup / Ratio':15s} |"
    sep = f"|{'-'*37}|{'-'*17}|{'-'*20}|{'-'*17}|"
    print(header)
    print(sep)

    clean_speedup = med_a_wall / med_b_wall if med_b_wall > 0 else 0
    incr_speedup = med_a_incr / med_b_incr if med_b_incr > 0 else 0
    cpu_ratio = med_b_cpu / med_a_cpu if med_a_cpu > 0 else 0

    print(f"| {'Clean build wall time':35s} | {med_a_wall:13.3f}s | {med_b_wall:16.3f}s | {clean_speedup:14.2f}x |")
    print(f"| {'Aggregate compiler CPU time':35s} | {med_a_cpu:13.3f}s | {med_b_cpu:16.3f}s | {cpu_ratio:14.2f}x CPU |")
    print(f"| {'Incremental rebuild time':35s} | {med_a_incr:13.3f}s | {med_b_incr:16.3f}s | {incr_speedup:14.2f}x |")
    print(f"| {'Peak memory (RSS)':35s} | {med_a_rss/1024:12.1f}MB | {med_b_rss/1024:15.1f}MB | {'':15s} |")

    tr_a = res_trace_a['trace']
    tr_b = res_trace_b['trace']
    print(f"| {'Template instantiation time':35s} | {tr_a.get('template_inst', 0):13.3f}s | {tr_b.get('template_inst', 0):16.3f}s | {'':15s} |")
    print(f"| {'Backend codegen time':35s} | {tr_a.get('backend_codegen', 0):13.3f}s | {tr_b.get('backend_codegen', 0):16.3f}s | {'':15s} |")
    print(f"| {'Header parsing time':35s} | {tr_a.get('header_parse', 0):13.3f}s | {tr_b.get('header_parse', 0):16.3f}s | {'':15s} |")
    print(f"| {'Parsing & semantics time':35s} | {tr_a.get('parse_sema', 0):13.3f}s | {tr_b.get('parse_sema', 0):16.3f}s | {'':15s} |")
    print(f"| {'LLVM IR generation time':35s} | {tr_a.get('ir_gen', 0):13.3f}s | {tr_b.get('ir_gen', 0):16.3f}s | {'':15s} |")
    print(f"| {'Total traced compilation':35s} | {tr_a.get('total_compile', 0):13.3f}s | {tr_b.get('total_compile', 0):16.3f}s | {'':15s} |")
    print("=" * 60)

    print("\nParallel Scaling (-j jobs):")
    for k, v in parallel_results.items():
        print(f"  {k:5s}: {v:.3f}s (Speedup vs baseline: {med_a_wall / v:.2f}x)")

if __name__ == "__main__":
    run_all_experiments()
