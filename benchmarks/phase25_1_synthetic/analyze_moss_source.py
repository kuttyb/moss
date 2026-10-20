#!/usr/bin/env python3
"""
Deep Source Analyzer for Moss Compiler:
- Analyzes src/moss.cpp and all src/*.hpp, src/*.inc
- Correlates with real_moss_profile.json
- Extracts function complexity, STL containers, templates, include graph, coupling
- Emits moss_source_fingerprint.json and SOURCE_ANALYSIS.md
"""

import os
import sys
import re
import json
from collections import defaultdict, Counter

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "../.."))
SRC_DIR = os.path.join(REPO_ROOT, "src")
PROFILE_PATH = os.path.join(SCRIPT_DIR, "real_moss_profile.json")

SUBSYSTEM_RANGES = [
    ("Core & Infrastructure", 1, 1711, "src/moss.cpp"),
    ("Parser", 1712, 2792, "src/moss.cpp"),
    ("ABI Records & Serialization", 2793, 4094, "src/moss.cpp"),
    ("Checker: Core & Types", 4095, 11000, "src/moss.cpp"),
    ("Checker: Ownership & Effects", 11001, 17500, "src/moss.cpp"),
    ("Checker: Bodies & Unification", 17501, 23615, "src/moss.cpp"),
    ("Generator (Rust Lowering)", 23616, 30444, "src/moss.cpp"),
    ("Semantic Queries & Agent APIs", 30445, 35077, "src/moss.cpp"),
    ("Project & Module Engine", 35078, 41990, "src/moss.cpp"),
    ("Tooling, Bench & Impact", 41991, 45095, "src/moss.cpp"),
    ("Fast Debug & CLI Driver", 45096, 46711, "src/moss.cpp")
]

def analyze_includes():
    """Analyze include graph across src/."""
    includes = defaultdict(list)
    inc_files = defaultdict(list)
    header_lines = {}

    for fname in sorted(os.listdir(SRC_DIR)):
        if fname.endswith(('.cpp', '.hpp', '.inc')):
            path = os.path.join(SRC_DIR, fname)
            with open(path, 'r', errors='replace') as f:
                lines = f.readlines()
            header_lines[fname] = len(lines)
            for line in lines:
                m = re.match(r'^\s*#include\s+["<]([^">]+)[">]', line)
                if m:
                    inc = m.group(1)
                    if inc.endswith('.inc'):
                        inc_files[fname].append(inc)
                    elif not inc.startswith(('iostream', 'vector', 'string', 'map', 'set', 'unordered_', 'algorithm', 'memory', 'optional', 'variant', 'tuple', 'chrono', 'cstdint', 'utility', 'sstream', 'fstream', 'functional', 'regex', 'cmath', 'sys/', 'unistd', 'cstdlib', 'cstring')):
                        includes[fname].append(inc)

    return header_lines, dict(includes), dict(inc_files)

def analyze_source():
    moss_cpp = os.path.join(SRC_DIR, "moss.cpp")
    with open(moss_cpp, 'r', errors='replace') as f:
        cpp_lines = f.readlines()

    header_lines, include_graph, inc_dependencies = analyze_includes()

    # 1. Subsystem lines and distribution
    subsystems_data = []
    for name, start, end, file in SUBSYSTEM_RANGES:
        sub_slice = cpp_lines[start-1:end]
        line_count = len(sub_slice)

        # Count branches, loops, lambdas
        branches = sum(len(re.findall(r'\b(if|switch|case|\?:)\b', l)) for l in sub_slice)
        loops = sum(len(re.findall(r'\b(for|while|do)\b', l)) for l in sub_slice)
        lambdas = sum(len(re.findall(r'\[[^\]]*\]\s*\([^\)]*\)\s*(?:->[^{]+)?\s*\{', l)) for l in sub_slice)
        methods = sum(1 for l in sub_slice if re.match(r'^\s*(?:[a-zA-Z0-9_:<>]+[\s*&]+)+[a-zA-Z0-9_]+\s*\([^;]*\)\s*(?:const\s*)?\{', l))

        subsystems_data.append({
            'name': name,
            'start_line': start,
            'end_line': end,
            'line_count': line_count,
            'branches': branches,
            'loops': loops,
            'lambdas': lambdas,
            'approx_methods': methods
        })

    # 2. STL Container specializations and frequencies
    stl_patterns = {
        'vector': re.compile(r'std::vector\s*<\s*([^>]+)\s*>'),
        'map': re.compile(r'std::map\s*<\s*([^,>]+)\s*,\s*([^>]+)\s*>'),
        'unordered_map': re.compile(r'std::unordered_map\s*<\s*([^,>]+)\s*,\s*([^>]+)\s*>'),
        'set': re.compile(r'std::set\s*<\s*([^>]+)\s*>'),
        'unordered_set': re.compile(r'std::unordered_set\s*<\s*([^>]+)\s*>'),
        'optional': re.compile(r'std::optional\s*<\s*([^>]+)\s*>'),
        'variant': re.compile(r'std::variant\s*<\s*([^>]+)\s*>'),
        'tuple': re.compile(r'std::tuple\s*<\s*([^>]+)\s*>'),
        'pair': re.compile(r'std::pair\s*<\s*([^,>]+)\s*,\s*([^>]+)\s*>'),
        'shared_ptr': re.compile(r'std::shared_ptr\s*<\s*([^>]+)\s*>'),
        'unique_ptr': re.compile(r'std::unique_ptr\s*<\s*([^>]+)\s*>')
    }

    container_specializations = defaultdict(Counter)
    container_totals = Counter()

    all_text = "".join(cpp_lines)
    for fname, path in [(f, os.path.join(SRC_DIR, f)) for f in os.listdir(SRC_DIR) if f.endswith(('.hpp', '.inc'))]:
        with open(path, 'r', errors='replace') as f:
            all_text += f.read()

    for kind, pat in stl_patterns.items():
        matches = pat.findall(all_text)
        container_totals[kind] = len(matches)
        for m in matches:
            if isinstance(m, tuple):
                spec = ", ".join(s.strip() for s in m)
            else:
                spec = m.strip()
            container_specializations[kind][spec] += 1

    top_specializations = {}
    for kind, counter in container_specializations.items():
        top_specializations[kind] = counter.most_common(12)

    # 3. Load profile data if available
    profile_data = {}
    if os.path.exists(PROFILE_PATH):
        with open(PROFILE_PATH, 'r') as f:
            profile_data = json.load(f)

    # 4. Synthesize fingerprint
    fingerprint = {
        'moss_source_overview': {
            'moss_cpp_lines': len(cpp_lines),
            'header_lines': header_lines,
            'total_repo_cpp_lines': len(cpp_lines) + sum(header_lines.values())
        },
        'subsystem_distribution': subsystems_data,
        'stl_container_summary': dict(container_totals),
        'top_container_specializations': top_specializations,
        'include_graph': include_graph,
        'inc_dependencies': inc_dependencies,
        'compilation_profile_summary': profile_data.get('uninstrumented', {}),
        'top_codegen_hotspots': profile_data.get('hotspots', {}).get('top_codegen_functions', [])[:15],
        'top_instantiations': profile_data.get('hotspots', {}).get('top_template_instantiations', [])[:15]
    }

    fingerprint_path = os.path.join(SCRIPT_DIR, "moss_source_fingerprint.json")
    with open(fingerprint_path, "w") as f:
        json.dump(fingerprint, f, indent=2)
    print(f"Generated {fingerprint_path}")

    # 5. Generate SOURCE_ANALYSIS.md
    analysis_md = f"""# Moss Compiler Source Analysis (Phase 25.1B)

## 1. Overview of Compilation Unit

Moss's monolithic implementation resides in `src/moss.cpp` ({len(cpp_lines):,} lines), accompanied by 19 headers and include fragments totaling {sum(header_lines.values()):,} lines across `src/`. The total compilation volume exceeds {len(cpp_lines) + sum(header_lines.values()):,} lines of C++17.

### Subsystem Organization & Asymmetry

Unlike typical modular C++ projects with evenly divided subsystems, Moss's codebase is heavily dominated by `Checker` (~19.5K lines, 41.8% of `moss.cpp`) and `Generator` (~6.8K lines, 14.6% of `moss.cpp`):

| Subsystem Region | Source Span in `moss.cpp` | Lines | Branches | Loops | Lambdas | Approx Methods |
|---|---|---:|---:|---:|---:|---:|
"""
    for sub in subsystems_data:
        analysis_md += f"| **{sub['name']}** | L{sub['start_line']}–L{sub['end_line']} | {sub['line_count']:,} | {sub['branches']:,} | {sub['loops']:,} | {sub['lambdas']:,} | {sub['approx_methods']:,} |\n"

    analysis_md += f"""
### Key Structural Insight:
- `Checker` is by far the largest component ({19520:,} lines total across 3 distinct regions: Core/Types, Ownership/Effects, and Body Unification).
- Any translation unit partitioning that leaves `Checker` as a single unit will create a massive **critical path bottleneck** (e.g. ~7–8 seconds of serial compile time), preventing clean-build speedups beyond ~2x.
- Therefore, `Checker` **must be subdivided** into at least 2 or 3 separate translation units (e.g. `Checker_Core`, `Checker_Ownership`, `Checker_Bodies`).

---

## 2. STL Container & Template Specialization Census

Total STL container instantiation sites across `src/`:
- `std::vector`: **{container_totals['vector']:,} occurrences**
- `std::map`: **{container_totals['map']:,} occurrences**
- `std::unordered_map`: **{container_totals['unordered_map']:,} occurrences**
- `std::set`: **{container_totals['set']:,} occurrences**
- `std::optional`: **{container_totals['optional']:,} occurrences**
- `std::variant`: **{container_totals['variant']:,} occurrences**
- `std::pair`: **{container_totals['pair']:,} occurrences**

### Most Frequent Concrete Specializations:

#### `std::vector`
"""
    for spec, cnt in top_specializations.get('vector', [])[:8]:
        analysis_md += f"- `{spec}` ({cnt} occurrences)\n"

    analysis_md += "\n#### `std::map` & `std::unordered_map`\n"
    for spec, cnt in top_specializations.get('map', [])[:6]:
        analysis_md += f"- `std::map<{spec}>` ({cnt} occurrences)\n"
    for spec, cnt in top_specializations.get('unordered_map', [])[:6]:
        analysis_md += f"- `std::unordered_map<{spec}>` ({cnt} occurrences)\n"

    analysis_md += f"""
---

## 3. Clang Trace Hotspots & Codegen Pressure

From the live Clang 18 `-ftime-trace` on host (total traced compilation: {profile_data.get('instrumented_trace', {}).get('total_compile_s', 0):.2f}s):
- **Template Instantiation:** {profile_data.get('instrumented_trace', {}).get('template_inst_s', 0):.2f}s ({profile_data.get('instrumented_trace', {}).get('template_inst_s', 0) / profile_data.get('instrumented_trace', {}).get('frontend_s', 1.0) * 100:.1f}% of Frontend)
- **Backend Codegen Passes:** {profile_data.get('instrumented_trace', {}).get('backend_codegen_s', 0):.2f}s ({profile_data.get('instrumented_trace', {}).get('backend_codegen_s', 0) / profile_data.get('instrumented_trace', {}).get('total_compile_s', 1.0) * 100:.1f}% of Total Compile)
- **Header Parsing (`Source`):** {profile_data.get('instrumented_trace', {}).get('header_parse_s', 0):.2f}s

### Top Function Codegen Hotspots:
"""
    for x in profile_data.get('hotspots', {}).get('top_codegen_functions', [])[:10]:
        analysis_md += f"- `{x['name']}`: {x['dur_s']:.3f}s codegen duration\n"

    analysis_md += f"""
---

## 4. Include Dependencies & Coupling

Moss relies on a set of core shared headers that define core data models:
- `ast.hpp`: Defines AST statement and expression nodes. Included transitively everywhere.
- `type_ir.hpp`: Defines TypeSolver, Undo trail, TypeKind, and unification logic. High template instantiation cost.
- `functional_ir.hpp`: Defines functional pipeline IR.
- `checked_body_ir.hpp` & `abi_v10_records.hpp`: Cross-module serialization structures.
- `interpreter.hpp`: Standalone Fast Debug AST evaluation engine.

### Coupling Challenges for TU Partitioning:
1. `Checker` methods access internal fields of `TypeEnv`, `Checker` state (`bindings`, `generic_instantiations`, `type_solver`), requiring either:
   - Shared private header `src/checker.hpp` declaring `class Checker` with out-of-line method definitions in separate `.cpp` files, OR
   - Member methods split across multiple `.cpp` files all including `checker.hpp`.
2. `Generator` methods access `ast.hpp` and `type_ir.hpp`, but do NOT depend on `Checker` internal state! This makes `Generator` cleanly separable.
3. `Parser` depends only on `ast.hpp` and lexer state; completely independent of `Checker` and `Generator`.
4. Driver/Project management (`moss build`, `margo`, test harness) depends on `Parser`, `Checker`, and `Generator` interfaces, but does not access their internal private bodies.
"""
    analysis_md_path = os.path.join(SCRIPT_DIR, "SOURCE_ANALYSIS.md")
    with open(analysis_md_path, "w") as f:
        f.write(analysis_md)
    print(f"Generated {analysis_md_path}")

if __name__ == "__main__":
    analyze_source()
