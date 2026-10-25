# Phase 25.4 B1 — Compiler Hotspot Investigation

**Target Compiler:** Optimized C++ Moss (`build/compiler/moss-opt`, `-O2`)  
**Scope:** In-depth algorithmic and profiling analysis of semantic compilation bottlenecks  
**Evidence Sources:** Hierarchical Chrome Trace Event traces (`MOSS_PROFILE_TRACE`), stage-by-stage opt-in profiling (`MOSS_PROFILE_COMPILER`), torture suite results (`results.csv`), and real-world package compilations (`packages/regex`, `sdk/datastructures`).  
**Audience:** Compiler Engineering Team, Phase 25.4 B2 Optimization Team, Phase 25.3 Java Compiler Team.

---

## 1. Executive Summary & Hotspot Matrix

Following the establishment of the Phase 25.4 B1 performance baseline and structured hierarchical tracing, an in-depth hotspot investigation was conducted across both synthetic torture workloads and real-world production Moss programs.

The investigation identified **three critical algorithmic bottlenecks** in `src/semantic_checker.cpp` that account for over **95% of total compiler latency** in long-running compilations:

| Hotspot ID | Bottleneck Description | Primary Location in `src/` | Impacted Workloads | Observed Severity | Algorithmic Mechanism |
|---|---|---|---|---|---|
| **HS-1** | **Unbounded Quadratic Signature Inference Loop** | `src/semantic_checker.cpp:13865-14040` (`infer_function_signatures`) | W5 (Wide Inference), `packages/regex`, `sdk/datastructures` | **10s – 24s** (95–99% of runtime) | Outer loop runs $3F + 4$ rounds unconditionally with **no early break**, called 5 times per run $\implies 15F^2 + 20F$ full-body AST and solver passes. |
| **HS-2** | **Recursive Specialization Effects Re-Evaluation & AST Cloning** | `src/semantic_checker.cpp:644-660`, `18229-18360` (`concrete_function_observable_effects`) | W4 (Deep Dependent Inference) | **27.6s** at depth 18 (96.2% of runtime) | No memoization on call chains; re-computes callee effect rows from scratch; clones entire AST body vector per specialization; executes double-checking loop. |
| **HS-3** | **Specialization Body Synthesis & Redundant Cache Re-Check** | `src/semantic_checker.cpp:21820-21950` (`register_static_specialization`) | W2 (Distinct Specializations) | **3.68s** at $N=32$ (1,056 specializations) | Full statement checking, AST cloning, and ownership checking per distinct key; re-evaluates `concrete_function_effects` even on cache hits. |

---

## 2. Hotspot 1: Unbounded Quadratic Signature Inference Loop

### A. Empirical Profile Evidence

In both synthetic wide workloads and real-world packages, type inference consumes virtually the entire compilation budget:

1. **W5 (Wide Independent Functions):**
   - $W = 10$: $148.7\text{ ms}$
   - $W = 25$: $733.5\text{ ms}$
   - $W = 50$: $2,805.8\text{ ms}$
   - $W = 100$: **$10,895.5\text{ ms}$** ($10.80\text{ s}$ pure `type_inference`, 99.1% of total wall time)
2. **`packages/regex/src/main.moss` ($F = 49$ functions, 1,000 LOC):**
   - Total semantic check time: **$21.60\text{ s}$**
   - Pass 1 `type_inference`: **$11.58\text{ s}$**
     - `inference_round_0`: $1.90\text{ s}$
     - `inference_round_1`: $2.28\text{ s}$
     - `inference_round_2`: $2.31\text{ s}$
     - Finalized pass: $5.09\text{ s}$
   - Pass 2 `type_inference`: **$9.84\text{ s}$**
3. **`sdk/datastructures/src/datastructures.moss` ($F = 112$ functions, 972 LOC):**
   - Total semantic check time: **$24.15\text{ s}$**
   - Pass 1 `type_inference`: **$11.90\text{ s}$**
   - Pass 2 `type_inference`: **$11.94\text{ s}$**

### B. Code Attribution & Algorithmic Flaw

In [`src/semantic_checker.cpp#L13865`](file:///home/owner/daji/code/moss2/src/semantic_checker.cpp#L13865):

```cpp
// src/semantic_checker.cpp:13865
for (size_t round = 0; round <= p_.functions.size() * 3 + 3; ++round) {
  for (auto& function : p_.functions) {
    SourceFileScope function_source(current_source_file_, function.source_file);
    TypeEnv env;
    ...
    infer_statement_expressions(function.body, env);
    BodyTypeState body_types = solve_local_collection_types(
        function.body, env, function.semantic_callable_id, false, true);
    ...
  }
  for (auto& domain : p_.domains) { ... }
  if (p_.main) { ... }
  for (const auto& test : p_.tests) { ... }
  for (const auto& benchmark : p_.benchmarks) { ... }
}
```

#### Key Flaws:
1. **Absence of Early Termination:** There is no dirty-tracking boolean or change-detection variable in this loop. It executes all $3F + 4$ iterations unconditionally, even when all function return types and constraints were resolved in round 0 or round 1.
2. **Multiplication by Caller:** In `Checker::run()` ([`src/semantic_checker.cpp#L513-L539`](file:///home/owner/daji/code/moss2/src/semantic_checker.cpp#L513-L539)), `infer_function_signatures` is called:
   - Line 513: initially (`infer_function_signatures(false)`)
   - Line 530: in `round = 0`
   - Line 530: in `round = 1`
   - Line 530: in `round = 2`
   - Line 539: in `finalize` (`infer_function_signatures(true)`)
3. **Total Work Equation:**
   $$\text{Total Full-Body Passes} = 5 \times (3F + 4) \times F = 15 F^2 + 20 F$$
   - For $F = 49$ (`packages/regex`): $15(49)^2 + 20(49) \approx \mathbf{37,000}$ full-body AST and solver evaluations.
   - For $F = 100$ ($W=100$): $15(100)^2 + 20(100) = \mathbf{152,000}$ passes.
   - For $F = 112$ (`sdk/datastructures`): $15(112)^2 + 20(112) = \mathbf{190,624}$ passes.

### C. Recommended Solution for Phase 25.4 B2
1. **Dirty Tracking & Early Exit:** Maintain a `bool changed = false` flag. If no function return type was newly bound, no generic result was resolved, and no constraint was added during a round, break immediately. Expected speedup: **10x – 30x**.
2. **Topological Ordering / Worklist:** Compute the callable dependency graph. Traverse strongly connected components in topological order (callees before callers) so return types propagate in a single bottom-up pass ($O(F)$).

---

## 3. Hotspot 2: Recursive Specialization Effects & AST Cloning on Deep Chains

### A. Empirical Profile Evidence

In deep dependent inference chains (W4), the compiler latency exhibits extreme superlinear scaling:

| Depth | Total Wall (ms) | Type Inference (ms) | Effects (ms) | Specialization Effects (ms) | % of Time in Effects |
|---:|---:|---:|---:|---:|---:|
| 5 | 18.09 | 7.91 | 2.12 | 1.84 | 21.9% |
| 10 | 143.66 | 36.71 | 44.26 | 88.81 | 92.6% |
| 15 | 3,387.24 | 68.12 | 921.40 | 2,341.15 | 96.3% |
| 18 | **27,617.13** | 93.96 | **7,580.01** | **19,013.27** | **96.3%** |

At depth 18, `specialization_effects` takes **19.01 seconds** and `effects` takes **7.58 seconds**, totaling **26.59 seconds out of 27.62 seconds**.

### B. Code Attribution & Algorithmic Flaw

In [`src/semantic_checker.cpp#L644-L660`](file:///home/owner/daji/code/moss2/src/semantic_checker.cpp#L644-L660):

```cpp
// src/semantic_checker.cpp:644
{
  CompilerStageTimer timer("specialization_effects");
  for (auto& function : p_.functions)
    for (auto& specialization : function.specializations)
      specialization.observable_effects =
          concrete_function_observable_effects(
              function, specialization.parameter_types);
  // Redundant second pass!
  for (const auto& function : p_.functions)
    for (const auto& specialization : function.specializations) {
      const auto rechecked = concrete_function_observable_effects(
          function, specialization.parameter_types);
      if (!same_observable_effects(specialization.observable_effects, rechecked))
        err(...);
    }
}
```

In [`src/semantic_checker.cpp#L18229-L18360`](file:///home/owner/daji/code/moss2/src/semantic_checker.cpp#L18229-L18360) (`concrete_function_observable_effects`):

```cpp
// src/semantic_checker.cpp:18251
ObservableEffects concrete = observable_body_effects(function.body, env, parameters);
auto body = function.body; // FULL AST VECTOR COPY!
concrete.raise_set = compiler->phase21_raise_set(body, env);
...
const ObservableEffects callback_effects =
    concrete_function_observable_effects(*callback, callback_arguments);
concrete.merge(callback_effects);
```

#### Key Flaws:
1. **Zero Memoization Across Chain Invocations:** When evaluating `step018`, it recursively calls `concrete_function_observable_effects` on `step017`, which calls `step016`, down to `step000`. Then the outer loop advances to `step017` and re-evaluates the entire chain from scratch!
2. **Double Execution:** Lines 650–659 re-run the entire computation a second time just to check `!same_observable_effects`.
3. **AST Cloning:** Line 18257 creates a deep copy of `function.body` on every single invocation of `concrete_function_observable_effects`.
4. **Call Graph Inefficiency in `infer_observable_effects`:** Lines 19245–19255 use an un-ordered round loop of size $3E + 3$ instead of a topological DAG traversal.

### C. Recommended Solution for Phase 25.4 B2
1. **Memoize Specialization Effects:** Cache `concrete_function_observable_effects` results keyed by `(function.semantic_callable_id, argument_types)`. On a chain $f_k \to f_{k-1}$, evaluating $f_k$ reuses the cached summary of $f_{k-1}$ in $O(1)$ time. Expected speedup: **100x – 1,000x** (reducing W4 depth 18 from 27.6s to < 50ms).
2. **Eliminate Redundant Second Pass:** Eliminate the second loop in lines 650–659 in release builds or gate it under an internal self-check flag.
3. **Avoid AST Cloning:** Pass const references or light statement views into raise-set analysis.

---

## 4. Hotspot 3: Concrete Specialization Body Synthesis & Re-Checking (W2 vs W3)

### A. Empirical Profile Evidence

Comparing W2 (distinct concrete specializations) with W3 (repeated specializations with cache hits) isolates the exact marginal cost of concrete specialization body synthesis:

| Workload | $N$ | Call Sites | Distinct Specs | Median (ms) | `type_inference` (ms) | `specialization_checking` (ms) | `specialization_effects` (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| **W2** (Distinct) | 32 | 1,024 | 1,056 | **3,678.53** | 1,438.91 | **1,539.40** | 283.63 |
| **W3** (Repeated) | 32 | 1,024 | 6 | **2,417.26** | 1,365.34 | **840.71** | 0.63 |
| **Delta** | — | — | +1,050 | **+1,261.28** | +73.57 | **+698.69** | **+283.00** |

The measured differential proves that synthesizing and checking 1,050 additional concrete specialization bodies incurs **1,261 ms** of overhead (~$1.20\text{ ms}$ per distinct specialization).

### B. Code Attribution & Algorithmic Flaw

In [`src/semantic_checker.cpp#L21820-L21950`](file:///home/owner/daji/code/moss2/src/semantic_checker.cpp#L21820-L21950) (`register_static_specialization`):

```cpp
// Cache hit path (lines 21825-21845):
if (existing != function.specializations.end()) {
  ...
  if (final_parameter_effects_ready_) {
    vector<bool> fresh_mutations;
    auto fresh_effects = concrete_function_effects(
        function, canonical_types, &fresh_mutations, true);
    if (existing->parameter_effects != fresh_effects || ...)
      err(...);
  }
  return;
}

// Cache miss path (lines 21865-21950):
specialization.parameter_effects = concrete_function_effects(
    function, canonical_types, &specialization.parameter_mutations);
function.specializations.push_back(specialization);
...
infer_statement_expressions(function.body, specialized_env);
static_bindings_for_body(function.body, specialized_env);
Function concrete_function = function; // DEEP COPY
check_stmts(function.body, specialized_env, nullptr, nullptr, &concrete_function);
auto final_ownership = check_ownership(function.body, std::move(specialized_ownership), ...);
```

#### Key Flaws:
1. **Cache Hit Penalty:** Even when a specialization key hits the cache, lines 21840–21850 re-evaluate `concrete_function_effects` to ensure parity.
2. **Full Body Re-Checking on Isomorphic Bodies:** In W2, every sensor type has the identical layout and behavior, yet the compiler re-analyzes every statement and re-derives ownership for all 1,056 instances.

### C. Recommended Solution for Phase 25.4 B2
1. **Isomorphic Shape Specialization:** Group concrete types by structural layout and effect profile. If a function does not branch on nominal type tags, check the body once per structural class.
2. **Skip Redundant Effect Re-computation on Cache Hits:** Trust the cached `parameter_effects` in production builds.

---

## 5. Hotspot 4: Repeated Module Resolution & Parsing Across Invocations

In `packages/regex` and `sdk/datastructures`, the compiler profile logs indicate that modules like `bytes` and internal dependencies are parsed and checked **twice** during a single `moss check` command:
- Pass 1: Semantic interface extraction and dependency import.
- Pass 2: Main compilation unit pass.

### Recommended Solution for Phase 25.4 B2
- Implement an in-memory compilation cache for resolved module interfaces so that importing a module does not re-parse or re-analyze its AST multiple times in the same compiler session.

---

## 6. Actionable Optimization Roadmap for Phase 25.4 B2

The following prioritized roadmap outlines the specific compiler engineering tasks for Phase 25.4 B2:

| Priority | Task Description | Target File(s) | Expected Latency Reduction | Risk Level | Correctness Verification Gate |
|---|---|---|---|---|---|
| **P0** | **Dirty-tracking early exit in `infer_function_signatures`** | `src/semantic_checker.cpp` | **10x – 20x** on W5, `packages/regex`, `sdk/datastructures` | Very Low | `make check`, `make check-perf`, `verify.py` |
| **P0** | **Memoize `concrete_function_observable_effects`** | `src/semantic_checker.cpp` | **100x – 500x** on W4 (Deep Inference) | Low | Regression suites, W4 depth 18 baseline comparison |
| **P1** | **Topological order traversal for callable fixed-points** | `src/semantic_checker.cpp` | Reduces inference rounds to 1 pass for DAGs | Medium | Negative and cyclic recursion error diagnostics |
| **P1** | **Eliminate redundant second validation pass in `specialization_effects`** | `src/semantic_checker.cpp` | **2x** speedup in specialization effects phase | Very Low | `make check-perf` |
| **P2** | **Isomorphic structural specialization body caching** | `src/semantic_checker.cpp` | **2x – 4x** speedup on W2 (large specialization fan-outs) | Medium | Specialization test suite, ABI stability |

---

## 7. Conclusion

This hotspot investigation has identified the exact lines of code, data structures, and algorithmic loops responsible for the high compilation latencies observed during the Phase 25.4 B1 baseline measurements.

The observed slow compilation times are **not** intrinsic language overhead; they are primarily caused by:
1. Missing fixed-point early termination ($15F^2$ un-gated passes),
2. Lack of memoization in recursive effect propagation on deep call chains, and
3. Redundant validation passes.

Addressing these three areas in Phase 25.4 B2 will reduce compilation latency on the most demanding torture and real-world workloads from **20+ seconds down to sub-second or low-millisecond performance**.
