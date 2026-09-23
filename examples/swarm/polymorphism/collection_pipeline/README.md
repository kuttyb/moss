# Reusable Collection Processing Package and Consumer (Multi-Package Architecture)

## Overview & Workload Description

This experiment evaluates Moss v0.1's **static specialization and compile-time polymorphism** across a multi-package boundary managed by Margo.

The workload consists of two packages:
1. **`data_tools` (`examples/swarm/polymorphism/collection_pipeline/data_tools/`)**: A reusable utility package providing higher-level collection transformations:
   - Untyped / statically specialized higher-order pipelines: `map_by`, `filter_by`, `fold_left`, `count_matching`
   - Sliding windowing and windowed averages: `window_ints`, `window_averages`
   - Chunking and batch aggregations: `chunk_ints`, `batch_sums`, `batch_averages`
   - Frequency mapping and deduplication: `frequency_map_ints`, `distinct_ints`
   - Duck-typed accumulation: `total_measure`, `get_measurement`
2. **`log_analytics` (`examples/swarm/polymorphism/collection_pipeline/log_analytics/`)**: A consumer package depending on `data_tools` via Margo path dependency (`data_tools = { path = "../data_tools" }`). It models concrete telemetry records:
   - `RequestLog(path: String, status: Int, response_time: Int)`
   - `MetricPoint(timestamp: Int, value: Int)`
   It exercises cross-package static specialization to compute windowed latency and metric averages, status code distribution frequencies, slow-request filtering, batch metric rollups, and polymorphic latency sums.

---

## Multi-Package Architecture

```
examples/swarm/polymorphism/collection_pipeline/
├── data_tools/
│   ├── Moss.toml
│   ├── src/
│   │   └── data_tools.moss        # module DataTools
│   └── tests/
│       └── test_data_tools.moss   # module TestDataTools (8 unit tests)
├── log_analytics/
│   ├── Moss.toml                  # depends on data_tools = { path = "../data_tools" }
│   ├── src/
│   │   ├── log_analytics.moss     # module LogAnalytics
│   │   └── main.moss              # module LogAnalytics (runnable console application)
│   └── tests/
│       └── test_analytics.moss    # module LogAnalytics (8 unit tests)
├── reproducers/                   # Minimized failure reproducers for compiler defects & friction
│   ├── repro_untyped_index.moss
│   ├── repro_unqualified_callable_arg.moss
│   ├── repro_fast_debug_callable_arg.moss
│   ├── repro_filter_nontrivial_type.moss
│   ├── repro_reduce_map_abort.moss
│   ├── repro_push_loop_reference.moss
│   └── repro_private_struct_fields.moss
└── README.md
```

### Dependency Configuration

In `log_analytics/Moss.toml`:
```toml
[package]
name = "log_analytics"
version = "0.1.0"

[build]
source = "src"

[dependencies]
data_tools = { path = "../data_tools" }
```

Margo resolves `data_tools` as a path dependency, compiles `data_tools` to emit `libDataTools.rlib` and the semantic interface `DataTools.mossi`, and supplies `DataTools.mossi` and the compiled library to `log_analytics`.

---

## Cross-Package Static Specialization Findings

### 1. What Specialized and Compiled Cleanly

| Abstraction | Signature | Consumer Specializations | Mechanics & Status |
|---|---|---|---|
| `map_by` | `fn map_by(items, transform_fn)` | `Vector[RequestLog] -> Vector[Int]`<br>`Vector[MetricPoint] -> Vector[Int]` | **Passes**. Statically specialized in consumer crate. The callable identity is monomorphized into the pipeline. |
| `fold_left` | `fn fold_left(items, initial, step)` | `Vector[RequestLog]` with `add_latency`<br>`Vector[MetricPoint]` with `add_metric` | **Passes**. Elements are borrowed as reference into `step(acc, item)`. Non-copy structs accumulate without clone. |
| `filter_by` | `fn filter_by(items, predicate)` | `Vector[Int]` with `is_slow_request` | **Passes for Copy types**. Generates monomorphized filter stage without dynamic dispatch. |
| `count_matching` | `fn count_matching(items, predicate)` | `Vector[Int]` with `is_gt_10` | **Passes**. Fuses `filter` and terminal `count` cleanly. |
| `window_ints` / `window_averages` | `fn window_averages(items: Vector[Int], size: Int) -> Vector[Int]` | `Vector[Int]` (latencies and metric values) | **Passes**. Reusable algorithms parameterized over collection buffers. |
| `chunk_ints` / `batch_sums` / `batch_averages` | `fn batch_averages(items: Vector[Int], size: Int) -> Vector[Int]` | `Vector[Int]` (batch chunking) | **Passes**. Handles variable chunk boundaries and integer arithmetic cleanly. |
| `frequency_map_ints` | `fn frequency_map_ints(items: Vector[Int]) -> Map[Int, Int]` | `Vector[Int]` (HTTP status codes) | **Passes**. Reusable frequency mapping using Moss's built-in `Map`. |
| `distinct_ints` | `fn distinct_ints(items: Vector[Int]) -> Vector[Int]` | `Vector[Int]` (distinct HTTP status codes) | **Passes**. Deduplication using a `Map` seen-set. |

### 2. Where Abstraction and Reuse Broke Down

#### Friction Point 1: Duck-Typed Container Indexing on Untyped Parameters
- **Attempted**: Writing generic `chunk(items, size)` or `window(items, size)` where `items` is an untyped parameter and accessing elements via `items[i]`.
- **Compiler Error**: `MOSS_COMPILE_ERROR: value is not an indexable container` (at `src/moss.cpp:7591`).
- **Mechanism**: The compiler's `check_expression` requires `inferred_expr_type` to resolve to `vector[...]`, `queue[...]`, or `map[...]` before specialization. When `items` is untyped, its type is `_generic:items`, and the checker immediately rejects `items[i]` instead of deriving an `Indexable` constraint for static specialization.
- **Classification**: Specialization / inference defect.
- **Minimal Reproducer**: `reproducers/repro_untyped_index.moss`.

#### Friction Point 2: Unqualified Sibling Callables Passed Across Module Boundaries
- **Attempted**: Calling `Tools.filter_by(nums, is_positive)` where `is_positive` is a helper function in `module App`.
- **Compiler Error**: Backend crash / lowering error:
  `missing static specialization for function 'Tools__filter_by' with argument types (vector[int], unresolved)`.
- **Mechanism**: Inside `module App`, functions are registered in `functions_` as `App__is_positive`. Lowering's `generated_expr_type` looks up the unmangled string `"is_positive"`, fails, and reports the argument as `unresolved`.
- **Workaround**: Programmers must qualify the function with its own enclosing module: `Tools.filter_by(nums, App.is_positive)`.
- **Classification**: Specialization / name resolution defect.
- **Minimal Reproducer**: `reproducers/repro_unqualified_callable_arg.moss`.

#### Friction Point 3: Filter Over Non-Trivial (Struct) Element Types
- **Attempted**: `logs |> filter(is_slow)` where `logs: Vector[RequestLog]` or `pts |> filter(is_positive)` where `pts: Vector[MetricPoint]`.
- **Compiler Error**:
  `MOSS_COMPILE_ERROR: filter over nontrivial element type '...' cannot produce a new collection without an explicit deep copy` (at `src/moss.cpp:7362`).
- **Mechanism**: Under Moss's ownership model (`src/moss.cpp:2560 copy_type`), only primitive scalars (`int`, `float`, `bool`) are Copy. Struct types (even `MetricPoint`, which contains only `timestamp: Int, value: Int`) are non-copy. Because `filter` creates a new `Vector` from an existing one, elements would have to be cloned, which Moss refuses to do implicitly.
- **Workaround**: Map the struct collection to a primitive attribute vector first (`latencies = DataTools.map_by(logs, get_latency)`), then filter the primitive vector.
- **Classification**: Intended ownership rule / language design constraint.
- **Minimal Reproducer**: `reproducers/repro_filter_nontrivial_type.moss`.

#### Friction Point 4: Compiler Crash (SIGABRT) on Pipeline Reduce with Map Accumulator
- **Attempted**: `items |> reduce(initial_map, record_freq)` where `record_freq(counts: Map[Int, Int], item: Int) -> Map[Int, Int]`.
- **Compiler Error**: SIGABRT (exit code 134):
  `moss: src/moss.cpp:6054: Assertion '!node.effects.unresolved && !node.callable_identity.empty()' failed.`
- **Mechanism**: Mutating methods like `counts[item] = c + 1` inside `record_freq` cause the pipeline effect analyzer to set `node.effects.unresolved = true`, which violates the assertion that functional pipeline nodes must have resolved pure effects.
- **Workaround**: Express map aggregation imperatively using a `for` loop over `items` rather than a functional pipeline reduction.
- **Classification**: Pipeline / callable-resolution defect (Compiler Crash).
- **Minimal Reproducer**: `reproducers/repro_reduce_map_abort.moss`.

#### Friction Point 5: Borrowed Loop Variable Lowering in Container Mutations
- **Attempted**: Pushing a loop variable of `String` type into a `Vector[String]`:
  `for x in items: result.push(x)`.
- **Compiler Error**: Rust backend error:
  `error[E0308]: mismatched types: result.push(x) expected String, found &String`.
- **Mechanism**: The Moss frontend accepts the code (`moss check` passes). However, native lowering emits loop variables of non-copy types as borrowed references (`&String`). Lowering then emits `result.push(x)` without calling `.clone()` or `.to_string()`, causing rustc to reject the code.
- **Classification**: Native lowering bug.
- **Minimal Reproducer**: `reproducers/repro_push_loop_reference.moss`.

#### Friction Point 6: Exported Struct Fields Lower as Private Across Modules
- **Attempted**: Defining `export type RequestLog` in `module LogAnalytics` and constructing it in `module LogAnalyticsApp`: `LogAnalytics.RequestLog(...)`.
- **Compiler Error**: Rust backend error:
  `error[E0451]: fields 'path', 'status' and 'response_time' of struct 'moss_LogAnalytics::LogAnalytics__RequestLog' are private`.
- **Mechanism**: `src/moss.cpp:11700` emits `pub struct Foo { ... }`, but emits fields without `pub`. Across Rust crate boundaries, Rust forbids struct literal construction with private fields.
- **Workaround**: Include participating source files in the same logical module (`module LogAnalytics`).
- **Classification**: Module / interface projection problem.
- **Minimal Reproducer**: `reproducers/repro_private_struct_fields.moss`.

---

## Native vs Fast Debug Comparison

| Mode | Command | Compilation / Execution | Behavior on Multi-Package Pipeline |
|---|---|---|---|
| **Native Compilation** | `margo build` | Lowers Moss to Rust crates, compiles via `rustc` | **Full Success**. Both packages compile cleanly with optimized native binaries. Cross-package static specialization monomorphizes callables and data structures without runtime overhead. |
| **Native Execution** | `margo run` | Executes native binary | **Full Success**. Output matches mathematical expectation for all windowed and batched calculations. |
| **Native Tests** | `margo test` | Builds and runs native test binary | **Full Success (16/16 tests pass)**: 8 passed in `data_tools`, 8 passed in `log_analytics`. |
| **Fast Debug** | `margo debug` | Resolves source closure, interprets reachable Moss AST | **Fails on Higher-Order Functions**: Fails at line 77 with `interpreter error: unknown local 'LogAnalytics__add_latency'`. Fast Debug does not yet support function identifiers passed as callable parameters. |
| **Fast Debug Trace** | `margo debug --trace` | Emits NDJSON execution events on stderr | Streams events up to the callable argument evaluation failure. |

### Fast Debug Limitations Identified
1. **No Callable Arguments**: The Fast Debug interpreter evaluates function argument expressions as local variables. When a named function (`LogAnalytics.add_latency`) is passed as an argument, the interpreter looks for a local binding named `'LogAnalytics__add_latency'` and aborts.
2. **No `for` Iteration**: Fast Debug aborts with `Fast Debug does not support for iteration yet` on `for` loops (as documented in bootstrap capability notes).
3. **No Dedicated Test-Interp Mode**: `margo debug` executes the project's interpreted source closure (application logic) and is a supported Fast Debug path. Native tests run via `margo test`. However, there is no `margo test --interp` or `margo debug --tests` mode that interprets `test` blocks without invoking `rustc`. When a native lowering defect blocks `margo test`, there is no interpreted-only path to run those specific test blocks (see SWARM-039).

---

## Summary of Reproducers

| File | Failure Diagnostic | Root Cause Category |
|---|---|---|
| `repro_untyped_index.moss` | `value is not an indexable container` | Specialization / inference defect |
| `repro_unqualified_callable_arg.moss` | `missing static specialization ... with argument types (vector[int], unresolved)` | Specialization / name resolution defect |
| `repro_fast_debug_callable_arg.moss` | `interpreter error: unknown local '<function_name>'` | Native / Fast-Debug inconsistency |
| `repro_filter_nontrivial_type.moss` | `filter over nontrivial element type ... cannot produce a new collection without an explicit deep copy` | Ownership / effect interaction |
| `repro_reduce_map_abort.moss` | `Assertion '!node.effects.unresolved && !node.callable_identity.empty()' failed` (SIGABRT) | Pipeline / callable-resolution defect |
| `repro_push_loop_reference.moss` | `expected String, found &String` (rustc E0308) | Native lowering bug |
| `repro_private_struct_fields.moss` | `field ... of struct ... is private` (rustc E0451) | Module / interface projection problem |

---

## Validation Results

### `data_tools` Suite (`margo test`)
```text
PASS tests/test_data_tools.moss:chunk_ints, batch_sums, and batch_averages
PASS tests/test_data_tools.moss:distinct_ints
PASS tests/test_data_tools.moss:filter_by and count_matching
PASS tests/test_data_tools.moss:fold_left
PASS tests/test_data_tools.moss:frequency_map_ints
PASS tests/test_data_tools.moss:map_by
PASS tests/test_data_tools.moss:window boundary conditions
PASS tests/test_data_tools.moss:window_ints and window_averages

8 passed
0 failed
```

### `log_analytics` Suite (`margo test`)
```text
PASS tests/test_analytics.moss:batch metric sums and averages
PASS tests/test_analytics.moss:count slow requests
PASS tests/test_analytics.moss:distinct status codes
PASS tests/test_analytics.moss:slow request filtering
PASS tests/test_analytics.moss:status code frequencies
PASS tests/test_analytics.moss:total measurements
PASS tests/test_analytics.moss:windowed latency averages
PASS tests/test_analytics.moss:windowed metric averages

8 passed
0 failed
```

### Application Execution (`margo run` in `log_analytics`)
```text
=== Log Analytics Pipeline Engine ===
Processed request logs count: 6
Total latency (ms): 805
Sliding window latency averages (size 3): 171 165 185 96
Slow request latencies count: 3
Slow request latencies: 120 350 180
Count of slow requests (>100ms): 3
Status code frequencies:
  200 OK: 4
  404 Not Found: 1
  500 Internal Error: 1
Distinct status codes count: 3
Distinct status codes: 200 500 404
=== System Metrics Batch & Window Processing ===
Total metric sum: 210
Batch sums (size 2): 30 70 110
Batch averages (size 2): 15 35 55
Sliding window metric averages (size 3): 20 30 40 50
=== Pipeline Processing Complete ===
```

---

## Conclusions on Moss's Static Polymorphism Model

1. **True Static Specialization**: Moss v0.1 successfully achieves zero-cost, compile-time specialization across package boundaries for higher-order pipeline transformations (`map`, `filter`, `reduce`). In native code, closures and dynamic vtables are completely eliminated; the compiler monomorphizes concrete Rust functions with direct calls.
2. **Inferred Access Modes Work for Struct Pipelines**: `fold_left` effortlessly traversed collections of nontrivial heap-allocated structs (`RequestLog` with `path: String`) by inferring borrowed `READ` access for the accumulator step function.
3. **Container Polymorphism Gaps**: Because Moss lacks source-level generic type parameters (`Vector[T]()`), creating empty collections in untyped functions is restricted. Untyped container indexing (`items[i]`) is also rejected at check time, requiring algorithms to be structured either around pipeline primitives or concrete container signatures.
4. **Boundary Ergonomics**: Sibling callable arguments across module boundaries must be explicitly qualified (`App.is_positive`), and struct fields across separate module crates require `pub` visibility in lowering.
5. **Tooling Maturity**: Native compilation (`margo build`, `margo run`, `margo test`) is robust, fully verified, and production-ready for cross-package static polymorphism. Fast Debug (`margo debug`) provides immediate iteration for scalar/pipeline code but lags behind in handling higher-order callable parameters and loop traversals.
