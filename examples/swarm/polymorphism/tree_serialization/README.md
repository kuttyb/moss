# Generic Tree / Hierarchical Data Algorithms & Serialization

Experiment conducted in Moss v0.1 as part of the Swarm Polymorphism workload.

## 1. Executive Summary & Workload Scope

This dogfood experiment explores generic tree and hierarchical data algorithms in Moss v0.1:
- **Workload**: Tree and hierarchy processing and serialization operating abstractly across distinct concrete node types.
- **Node Types Implemented**:
  1. `OrgNode`: `id: Int`, `name: String`, `budget: Int`, `children: Vector[OrgNode]`
  2. `DocNode`: `tag: String`, `text: String`, `children: Vector[DocNode]`
  3. `CategoryNode`: `code: String`, `item_count: Int`, `children: Vector[CategoryNode]`
- **Generic Algorithms Implemented**:
  1. Iterative Breadth-First Search (BFS) / level-order traversal using `Queue()`.
  2. Iterative Depth-First Search (DFS) / pre-order stack traversal using an explicit `Vector` stack.
  3. Abstract Tree Fold and Aggregations (`tree_size`, `tree_max_depth`, `tree_leaf_count`, `tree_sum_values`, `tree_sum_filtered`, `tree_collect_values`).
  4. Structural Serializers:
     - Record-based serializer producing `Vector[TreeEntry]`.
     - Text serializer formatting indented bullet trees (`Vector[String]`).
     - Structured JSON-like serializer formatting object arrays (`Vector[String]`).
     - Display helpers (`print_indented_tree`, `print_json_tree`).
- **Language Constraints Handled**:
  - No ordinary recursion: tree traversals are strictly iterative.
  - No runtime trait objects / dynamic dispatch: polymorphism is purely compile-time structural duck typing.
  - Ownership and borrow checker constraints: vector and stack operations require precise value lifecycle tracking.

---

## 2. Architecture & Design Decisions

### Structural Duck Typing via Named Trait `TreeNode`

Moss v0.1 does not support nominal interfaces (`implements Trait`) or runtime trait objects (`dyn Trait`). Instead, traits are compile-time structural predicates. We define:

```moss
trait TreeNode:
  fn child_count() -> Int
  fn child_at(i: Int)
  fn label() -> String
  fn value() -> Int
```

Any type that defines these four methods statically satisfies `TreeNode`. When a generic algorithm specifies `root: TreeNode`, the Moss compiler emits a distinct, monomorphized specialization per concrete type at each call site before generating Rust backend code.

### Explicit Iterative Traversal

Because recursion is unsupported in Moss v0.1, all traversals use explicit iteration:
- **BFS Traversal**: Uses built-in `Queue()`. Because `Queue` provides only `push` and `pop` (with no cardinality method or pipeline integration), queue size is tracked explicitly with `var q_len = 0` updated on every push/pop.
- **DFS Traversal**: Uses an explicit `Vector` stack initialized as `[make_owned(root)]`. Children are pushed in reverse order (`k - 1` down to `0`), ensuring canonical pre-order traversal. Stack size is tracked explicitly (`var stack_len = 1`) to avoid `while (stack |> count) > 0:`, which triggers formatter and inference bugs.

### Ownership & The Identity Helper `make_owned`

In Moss v0.1, passing an object into a collection consumes it. If a traversal popped a borrowed node and directly accessed its children, or if a method returned a field directly, the compiler inferred `CONSUME self`.

To allow repeated traversal in loops without consuming the receiver, we introduced the universal identity helper:

```moss
fn make_owned(node):
  return node
```

When an untyped function takes a borrowed reference and returns it, Moss monomorphizes it to return a fresh clone: `(node).clone()`.
Using `make_owned(children[i])` inside `child_at(i)` ensures the receiver effect of `child_at` is inferred as `READ self`, allowing repeated method invocations across loop iterations.

### Text Formatting & String Utilities

Direct binary String concatenation `a + b` fails in native lowering (`rustc` E0308, `expected &str, found String`). To support serialization into formatted lines, we use the `concat` utility:

```moss
fn concat(a: String, b: String) -> String:
  var out = ""
  out = out + a
  out = out + b
  return out
```

Because `a` and `b` are parameters, they lower to `&String` in Rust, which dereferences to `&str` when added to `out: String`. This executes cleanly in both Native and Fast Debug environments.

---

## 3. Tension Log & Friction Summary

| # | Desired Abstraction | Workaround Required | Root Cause | Classification | Cost |
|---|---|---|---|---|---|
| **T1** | Duck-typing fields on untyped parameter: `node.name` across unrelated types | Method duck typing: `node.label()` | Untyped field access binds parameter type nominally to first call site | Insufficiently documented static-polymorphism rule | 4 |
| **T2** | Storing untyped parameter into collection: `var q = Queue(); q.push(root)` | Annotate parameter with structural trait: `root: TreeNode` | Type inferencer unifies untyped parameter with first call site's collection element type | Specialization / inference defect | 4 |
| **T3** | Trait method returning child node: `fn get_child(i: Int) -> TreeNode` | Omit return type annotation: `fn child_at(i: Int)` | Trait return types are checked nominally; Moss lacks `Self` or covariant trait returns | Missing language feature / intended rule | 3 |
| **T4** | Direct indexed read from struct vector field: `return children[i]` | Indirection via `return make_owned(children[i])` | Direct extraction of struct field in method infers `CONSUME self`, causing use-after-consume in loops | Ownership / effect interaction | 4 |
| **T5** | Generic vector of trait instances: `Vector[TreeNode]()` | Inferred collections (`Queue()`) or single-element seed (`[make_owned(root)]`) | Traits have no runtime representation or nominal backend type in Rust | Intentionally unsupported dynamic abstraction | 3 |
| **T6** | Binary String concatenation: `line = indent + "- " + label` | `concat(a, b)` multi-statement helper | Moss lowers `+` to `(String) + (String)`, rejected by rustc | Native-lowering defect (SWARM-053) | 3 |
| **T7** | Pipeline with callable parameter in Fast Debug: `items \|> filter(pred)` | Inlined loop logic in Fast Debug, or static lambda placeholder `_ >= 70` | Fast Debug fails to resolve function symbols passed as arguments | Fast Debug interpreter limitation (SWARM-048) | 2 |
| **T8** | Loop condition with pipeline: `while (stack \|> count) > 0:` | Explicit length counter `stack_len` | Formatter rejects expressions beginning with `(`; pipeline inference in arithmetic | Formatter / syntax defect (SWARM-031, SWARM-032) | 2 |
| **T9** | Escaped quotes in function argument: `concat(ind, "{\"label\": \"")` | Format strings without escaped quotes: `concat(ind, "{label: ")` | Fast Debug interpreter argument splitter misparses `\"` | Fast Debug interpreter parser defect | 2 |
| **T10** | `assertEqual(actual, "{literal}")` | Bind expected string with braces to local variable before `assertEqual` | Moss embeds string literal directly into Rust's `format!` string without escaping `{` -> `{{` | Native-lowering defect | 2 |

---

## 4. Defect Repro & Classification Ledger

All minimal reproducers are archived in `reproducers/`:

### REPRO-01: Untyped Field Access Monomorphization (`repro1_field_duck_typing.moss`)
- **Observed**: An untyped function `fn get_name(node): return node.name` called on two unrelated types `A(name: String)` and `B(name: String)` fails with:
  `TYPE_MISMATCH: argument 1 to function 'get_name' has type 'B', expected 'A'`.
- **Classification**: Insufficiently documented static-polymorphism rule. Untyped parameters are duck-typed strictly on **methods**, not on fields. Field accesses bind the parameter to the first concrete type.
- **Resolution**: Expose fields through methods (`fn label() -> String`).

### REPRO-02: Collection Storage Monomorphizes Untyped Functions (`repro2_collection_param_monomorphization.moss`)
- **Observed**: A function `fn traverse(root): var q = Queue(); q.push(root)` called on `NodeA` and `NodeB` fails with `TYPE_MISMATCH` on the second call site.
- **Classification**: Specialization / type inference defect. Unification of the untyped parameter with the collection element type operates globally across the function definition instead of per static specialization.
- **Resolution**: Annotate the parameter with a structural trait (`fn traverse(root: TreeNode)`).

### REPRO-03: Recursive Trait Return Type Check (`repro3_recursive_trait_return.moss`)
- **Observed**: Declaring `trait TreeNode: fn get_child(i: Int) -> TreeNode` fails checking against `OrgNode` whose method returns `OrgNode` with:
  `trait method 'get_child' has incompatible result type`.
- **Classification**: Intentionally unsupported dynamic abstraction / missing `Self` syntax. Moss v0.1 traits do not support recursive/covariant self-types.
- **Resolution**: Leave the trait method untyped (`fn child_at(i: Int)`).

### REPRO-04: Vector Field Indexing Infers `CONSUME self` (`repro4_method_field_vector_indexing_consumes.moss`)
- **Observed**: Method `fn child_at(i: Int) -> Node: return children[i]` infers `ownership: [{"name": "self", "effect": "CONSUME"}]`. Calling `curr.child_at(i)` in a loop causes `rustc E0382: use of moved value: curr` and Fast Debug `interpreter error: method receiver is not a Moss object`.
- **Classification**: Ownership / effect interaction. Direct element extraction from a struct field in a method is treated as moving out of `self`.
- **Resolution**: Route through `fn make_owned(node): return node`, changing inferred effect of `self` to `READ`.

### REPRO-05: Trait-Typed Vector Construction (`repro5_trait_typed_vector.moss`)
- **Observed**: `var v = Vector[TreeNode]()` passes `moss check` but fails native compilation with rustc E0425: `cannot find type TreeNode in this scope`.
- **Classification**: Intentionally unsupported dynamic abstraction. Traits are compile-time structural predicates with no runtime trait object or nominal Rust struct.
- **Resolution**: Use inferred collections (`Queue()`) or initialize a vector from an owned seed value (`var stack = [make_owned(root)]`).

### REPRO-06: Binary String Concatenation Failure (`repro6_string_concat_native.moss`)
- **Observed**: `s = s + " world"` passes `moss check` and runs in Fast Debug, but fails native compilation under rustc E0308 (`expected &str, found String`).
- **Classification**: Native-lowering defect (SWARM-053). Moss lowers binary `+` to `(left) + (right)`, but Rust requires `String + &str`.
- **Resolution**: Use `concat(a, b)` helper where arguments are parameters lowering to `&String`.

### REPRO-07: Fast Debug Function Argument Resolution (`repro7_fast_debug_function_argument.moss`)
- **Observed**: `items |> filter(is_even)` works natively but fails in Fast Debug with `interpreter error: unknown local 'is_even'`.
- **Classification**: Native/Fast-Debug inconsistency (SWARM-048).
- **Resolution**: In Fast Debug pipelines, prefer static lambda placeholders (`_ >= 70`) or explicit loops.

### REPRO-08: Fast Debug Escaped Quote Argument Splitting (`repro8_fast_debug_escaped_quote_in_arg.moss`)
- **Observed**: `concat(ind, "{\"label\": \"")` fails in Fast Debug with `unsupported expression 'concat(ind, "{"label": "")'`.
- **Classification**: Fast Debug interpreter parser defect. The interpreter's argument splitter does not recognize `\"` as an escaped character.
- **Resolution**: Avoid escaped quotes inside function argument strings.

### REPRO-09: Unescaped Braces in `assertEqual` Literals (`repro9_assert_equal_unescaped_brace.moss`)
- **Observed**: `assertEqual(actual, "{key: val}")` triggers rustc `invalid format string: expected '}'`.
- **Classification**: Native-lowering defect. Moss embeds expected string literals directly into Rust's `format!` string template.
- **Resolution**: Bind expected strings containing `{` or `}` to a local variable prior to `assertEqual`.

---

## 5. Native vs Fast Debug Matrix

| Feature | `moss check` | Native (`margo build/run`) | Fast Debug (`margo debug`) | Fast Debug Trace (`--trace`) | Notes |
|---|---|---|---|---|---|
| **Structural Trait Specialization** | PASS | PASS | PASS | PASS | Emits monomorphized functions per type |
| **Iterative BFS (Queue)** | PASS | PASS | PASS | PASS | Clean execution with `q_len` tracking |
| **Iterative DFS (Vector stack)** | PASS | PASS | PASS | PASS | Clean execution with `stack_len` tracking |
| **Tree Fold & Reductions** | PASS | PASS | PASS | PASS | Sum, filtered sum, max depth, leaf count |
| **Functional Pipeline over Values** | PASS | PASS | PASS | PASS | `values \|> filter(_ >= 70) \|> sum` |
| **Structural Serializer (Entries)** | PASS | PASS | PASS | PASS | Returns `Vector[TreeEntry]` |
| **Indented & JSON-like Display** | PASS | PASS | PASS | PASS | Clean formatted tree outputs |
| **Direct String `a + b`** | PASS | FAIL (E0308) | PASS | PASS | Workaround: `concat` helper |
| **Escaped Quotes in Args** | PASS | PASS | FAIL (parse) | N/A | Workaround: avoid `\"` in arg literals |
| **`for` Traversal in Interp** | PASS | PASS | FAIL (unsupported) | N/A | Workaround: explicit `while` loops |
| **Named Callable Args in Interp** | PASS | PASS | FAIL (unknown local) | N/A | Workaround: placeholder `_ > x` |
| **Braces in `assertEqual` literal** | PASS | FAIL (format!) | PASS | PASS | Workaround: bind to local variable |

---

## 6. Verification & Test Suite

The project includes a comprehensive test suite in `tests/tree_tests.moss` verifying:
1. Boundary condition: single-node leaf trees.
2. Linear linked-list trees (single-child chains).
3. Full branched `OrgNode` corporate hierarchy.
4. Full branched `DocNode` DOM/document hierarchy.
5. Full branched `CategoryNode` catalog hierarchy with functional value pipelines.
6. Structural serializer entries (`TreeEntry` depth, value, label, child count).
7. Indented and JSON line formatting.

### Verification Commands & Results

1. **Static Analysis & Check**:
   ```bash
   ./moss check src/main.moss --json
   ./moss check tests/tree_tests.moss --json
   ```
   *Result*: Both files checked cleanly with 0 diagnostics (`"ok": true`).

2. **Formatting**:
   ```bash
   ./moss fmt --check src/main.moss tests/tree_tests.moss
   ```
   *Result*: Idempotent canonical formatting (`exit code 0`).

3. **Native Build & Run**:
   ```bash
   ../../../../margo build
   ../../../../margo run
   ```
   *Result*: Clean build and execution. Output matches expected tree traversals, metrics, and indented/JSON formatting.

4. **Native Unit Tests**:
   ```bash
   ../../../../margo test
   ```
   *Result*: **7 passed, 0 failed** (`exit code 0`).

5. **Fast Debug Execution**:
   ```bash
   ../../../../margo debug
   ```
   *Result*: Clean execution without Rust compilation, matching native output line-for-line.

6. **Fast Debug Trace**:
   ```bash
   ../../../../margo debug --trace 2>&1 >/dev/null | wc -l
   ```
   *Result*: **6,976 structured NDJSON execution trace events** emitted without error.

---

## 7. Conclusions on Moss's Static Polymorphism Model

1. **Structural Traits are the Pillar of Compile-Time Reuse**:
   Moss v0.1 does not provide runtime trait objects, dynamic dispatch, or user-written generic type parameters. However, named structural traits provide an exceptionally clean and zero-cost mechanism for static duck typing. Algorithms written against `root: TreeNode` specialize perfectly across completely unrelated record types.

2. **Duck Typing is Method-Based, Not Field-Based**:
   Programmers coming from Python might expect duck-typing on field names (`node.children`, `node.name`). In Moss, field accesses bind nominally to concrete types. True structural polymorphism requires method contracts (`node.child_at(i)`, `node.label()`).

3. **Iterative Data Structures Require Explicit Length Tracking**:
   Because collections like `Queue` have no `.len()` method, and because pipeline operations like `while (stack |> count) > 0:` trigger parser and inference friction, explicit length tracking (`q_len`, `stack_len`) is the most robust, high-performance idiom in Moss.

4. **Ownership Requires Mindful Method Boundaries**:
   In systems without user-written lifetime annotations, the compiler's inferred effects (`READ`, `WRITE`, `CONSUME`) dictate dataflow. Accessing sub-objects within a method can inadvertently infer `CONSUME self`. The `make_owned` identity pattern is a critical idiom for keeping recursive/hierarchical data readable across loop iterations.

With these idioms in place, Moss v0.1 demonstrates expressive, type-safe, and zero-overhead hierarchical data processing and serialization.
