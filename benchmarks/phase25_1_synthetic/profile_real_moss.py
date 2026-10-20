#!/usr/bin/env python3
"""
Profiles real Moss compiler build on current host:
- 5 uninstrumented clean compilations (medians of wall, cpu, peak rss)
- 1 instrumented compilation with Clang 18 -ftime-trace -ftime-trace-granularity=500
- Analyzes phase breakdown and hotspots
- Writes real_moss_profile.json
"""

import os
import sys
import time
import json
import statistics
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "../.."))
TMP_DIR = os.path.join(REPO_ROOT, "tmp")
COMPILER = os.path.join(TMP_DIR, "toolchain/bin/clang++")

def run_proc(cmd):
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pid, status, rusage = os.wait4(p.pid, 0)
    t1 = time.perf_counter()
    out, err = p.communicate()
    return t1 - t0, rusage.ru_utime + rusage.ru_stime, rusage.ru_maxrss, out, err, p.returncode

def main():
    print("Profiling real Moss compiler on host...")
    src_file = os.path.join(REPO_ROOT, "src/moss.cpp")
    out_obj = os.path.join(TMP_DIR, "real_moss_bench.o")
    out_bin = os.path.join(TMP_DIR, "real_moss_bench_bin")
    trace_json = os.path.join(TMP_DIR, "real_moss_trace.json")

    base_cmd = [
        COMPILER,
        "-std=c++17", "-O0", "-g0",
        "-Wall", "-Wextra", "-pedantic",
        "-Wno-missing-field-initializers"
    ]

    # 1. Five uninstrumented clean builds (compile + link)
    walls = []
    cpus = []
    rss_list = []
    print("Running 5 clean uninstrumented builds (compile & link)...")
    for i in range(5):
        if os.path.exists(out_bin):
            os.remove(out_bin)
        cmd = base_cmd + [src_file, "-o", out_bin]
        w, c, r, o, e, ret = run_proc(cmd)
        if ret != 0:
            print("Build failed:", e.decode('utf-8'))
            sys.exit(1)
        walls.append(w)
        cpus.append(c)
        rss_list.append(r)
        print(f"  Run {i+1}: wall={w:.3f}s, cpu={c:.3f}s, peak_rss={r/1024:.1f}MB")

    med_wall = statistics.median(walls)
    med_cpu = statistics.median(cpus)
    med_rss = statistics.median(rss_list)

    # 2. Instrumented trace run
    print("Running 1 instrumented build (-ftime-trace)...")
    cmd_trace = base_cmd + [
        "-ftime-trace",
        "-ftime-trace-granularity=500",
        "-c", src_file,
        "-o", out_obj
    ]
    w_tr, c_tr, r_tr, o_tr, e_tr, ret_tr = run_proc(cmd_trace)
    if ret_tr != 0:
        print("Trace build failed:", e_tr.decode('utf-8'))
        sys.exit(1)

    generated_trace = os.path.join(TMP_DIR, "real_moss_bench.json")
    if not os.path.exists(generated_trace):
        # Look in current working directory
        if os.path.exists("src/moss.json"):
            generated_trace = "src/moss.json"
        elif os.path.exists("moss.json"):
            generated_trace = "moss.json"
        elif os.path.exists(os.path.join(TMP_DIR, "moss.json")):
            generated_trace = os.path.join(TMP_DIR, "moss.json")

    print(f"Found trace file at: {generated_trace}")
    with open(generated_trace, 'r') as f:
        trace_data = json.load(f)

    events = trace_data.get('traceEvents', [])
    totals = {}
    codegen_functions = []
    instantiate_functions = []
    source_files = {}

    for e in events:
        name = e.get('name', '')
        ph = e.get('ph', '')
        dur = e.get('dur', 0) / 1e6
        args = e.get('args', {})
        detail = args.get('detail', '') if isinstance(args, dict) else ''

        if name.startswith('Total '):
            totals[name] = dur

        if name == 'CodeGen Function' and ph == 'X':
            codegen_functions.append({'name': detail, 'dur_s': dur})

        if name == 'InstantiateFunction' and ph == 'X':
            instantiate_functions.append({'name': detail, 'dur_s': dur})

        if name == 'Source' and ph == 'X':
            source_files[detail] = source_files.get(detail, 0.0) + dur

    codegen_functions.sort(key=lambda x: x['dur_s'], reverse=True)
    instantiate_functions.sort(key=lambda x: x['dur_s'], reverse=True)
    sorted_sources = sorted(source_files.items(), key=lambda x: x[1], reverse=True)

    template_inst = totals.get('Total InstantiateFunction', 0.0) + totals.get('Total InstantiateClass', 0.0)
    backend_codegen = totals.get('Total CodeGenPasses', 0.0)
    ir_gen = totals.get('Total OptModule', 0.0)
    parse_sema = totals.get('Total ParseDeclarationOrFunctionDefinition', 0.0)
    header_parse = totals.get('Total Source', 0.0)
    total_compile = totals.get('Total ExecuteCompiler', 0.0)

    profile = {
        'uninstrumented': {
            'clean_wall_med_s': med_wall,
            'clean_cpu_med_s': med_cpu,
            'peak_rss_mb': med_rss / 1024.0,
            'raw_walls_s': walls,
            'raw_cpus_s': cpus
        },
        'instrumented_trace': {
            'total_compile_s': total_compile,
            'frontend_s': totals.get('Total Frontend', 0.0),
            'backend_s': totals.get('Total Backend', 0.0),
            'template_inst_s': template_inst,
            'backend_codegen_s': backend_codegen,
            'ir_gen_s': ir_gen,
            'parse_sema_s': parse_sema,
            'header_parse_s': header_parse,
            'all_totals': totals
        },
        'hotspots': {
            'top_codegen_functions': codegen_functions[:25],
            'top_template_instantiations': instantiate_functions[:25],
            'top_source_headers': [{'header': k, 'dur_s': v} for k, v in sorted_sources[:20]]
        }
    }

    out_file = os.path.join(SCRIPT_DIR, "real_moss_profile.json")
    with open(out_file, "w") as f:
        json.dump(profile, f, indent=2)

    print(f"Successfully wrote {out_file}")
    print("\nReal Moss Profile Summary:")
    print(f"  Uninstrumented Clean Wall (med): {med_wall:.3f}s")
    print(f"  Uninstrumented Clean CPU  (med): {med_cpu:.3f}s")
    print(f"  Peak RSS:                       {med_rss/1024:.1f} MB")
    print(f"  Total Traced Compilation:       {total_compile:.3f}s")
    print(f"  Frontend Time:                  {totals.get('Total Frontend', 0.0):.3f}s")
    print(f"    - Template Instantiation:     {template_inst:.3f}s")
    print(f"    - Header Parsing (Source):    {header_parse:.3f}s")
    print(f"    - Parse / Sema:               {parse_sema:.3f}s")
    print(f"  Backend Time:                   {totals.get('Total Backend', 0.0):.3f}s")
    print(f"    - CodeGen Passes:             {backend_codegen:.3f}s")
    print(f"    - IR Generation (OptModule):  {ir_gen:.3f}s")

if __name__ == "__main__":
    main()
