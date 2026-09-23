# GraphKit (Moss core-language exercise)

A directed graph with `String` node labels, built from an edge list, with:

- BFS order from a start node
- topological sort (Kahn) with cycle detection
- Dijkstra shortest paths with `Int` weights (`-1` = unreachable)
- removing an edge (one live `src -> dst` edge per call, the earliest added)

Output is deterministic even though `Map` iteration order is unspecified.

Only the core language is used: no domains, handlers, `message`, `domainroutes`,
or concurrency.

## Layout

```text
Moss.toml
src/main.moss    # Graph type, algorithms, demo main, 9 test blocks
README.md
```

Scratch material (probes, reduced reproducers, logs, the Fast Debug test
harness) is in `tmp/surface_graphkit/` at the repository root.

## Intent (what I would have written in Python)

```python
class Graph:
    def __init__(self):
        self.adj = defaultdict(list)          # label -> [(dst, weight)]
    def add_edge(self, u, v, w): self.adj[u].append((v, w)); self.adj[v]
    def remove_edge(self, u, v): ...          # del the first (v, _) in adj[u]
    def bfs(self, s):   deque, visited set, neighbours in sorted order
    def topo(self):     Kahn with a heap of ready labels -> (order, has_cycle)
    def dijkstra(self, s): heapq, dist dict
    # printing: ", ".join(order), f"{u} -> {v} = {d}"
```

## Design decisions (what the Moss version does)

- **Node ids by first appearance.** `intern(label)` assigns ids 0, 1, 2, ...
  in edge-list order; `ids: Map[String, Int]` and `labels: Map[Int, String]`
  translate. Every traversal breaks ties by node id and by edge insertion
  order, and no `Map` is ever iterated, so the output never depends on Map
  order. (Sorting labels alphabetically was the original intent; see T2.)
- **Edges as parallel vectors** `edge_src / edge_dst / edge_weight /
  edge_alive`, plus `out: Vector[Vector[Int]]` adjacency of edge indices.
  A `Vector[Edge]` with `edges[i].dst` checks and runs in Fast Debug but
  does not compile natively (T12).
- **Removal is a tombstone** (`edge_alive[e] = false`); Map has no deletion
  in v0.1, and tombstones keep edge indices stable for the adjacency lists.
- **Queues are `Vector[Int]` + a head index** (T5).
- **Dijkstra is the O(V^2 + E) array version** (no heap in the standard
  surface; ties to the smallest id).
- **All loops are `while`, all boolean logic is nested `if`**, so the same
  source runs natively and in Fast Debug (T8, T13).
- **Output is one echo per node** instead of a joined string (T11).

## Tension log

Sorted by cost (edit/check cycles consumed), highest first.

### T9 — copying a String out of a Vector field (COST 4)
- WANTED: `order.push(names[u])` where `names: Vector[String]` is a field.
- HAD TO: store labels in `labels: Map[Int, String]` and push `labels.get(u, "")`.
- WHY: `OWNERSHIP_USE_AFTER_CONSUME: value 'self' was transferred ...` on the
  second use; the diagnostic says "create an explicit deep copy" but no doc names
  a copy spelling. `names[u] + ""` passed check and Fast Debug but hit the
  native String-concatenation bug (D6). `map.get(k, default)` returns an owned
  value and works in all engines.
- CLASS: docs gap (no documented explicit-copy spelling), plus native-lowering bug (D6)
- COST: 4

### T2 — deterministic order by sorting labels (COST 3)
- WANTED: `for v in sorted(adj[u])` / a heap of ready labels ordered by label.
- HAD TO: order everything by first-appearance id.
- WHY: no sort in the surface (would hand-roll it, fine), but String `<` is
  wrong in Fast Debug (always `false`, D2), and `x = "a" < "b"` with two
  literals is lexed as one string literal (D1). A label sort would give
  different answers natively and in Fast Debug.
- CLASS: Fast Debug gap (D2) / frontend bug (D1)
- COST: 3

### T3 — mutating a nested collection element (COST 3)
- WANTED: `out[s].push(edge_index)` (also `g.out.push(...)` from outside the type).
- HAD TO: `fn push_into(row: Vector[Int], item: Int)` and `push_into(out[s], e)`.
- WHY: `MOSS_COMPILE_ERROR: local member calls are not implemented; use a
  top-level function`. `var row = out[0]; row.push(..); out[0] = row` instead
  consumes all of `out`. The helper form (WRITE argument) works in all engines.
- CLASS: missing surface (the diagnostic's hint was right and helpful)
- COST: 3

### T4 — no-value method ending with `push` (COST 3)
- WANTED: `fn add_edge(...):` whose last statement is `push_into(...)` / `items.push(e)`.
- HAD TO: add a bare `return` as the last statement.
- WHY: `TYPE_INFERENCE_FAILED: cannot infer return type for method 'Graph.add_edge'`;
  the trailing expression statement seems to be treated as an implicit return
  value. `legal_alternatives` ("add a concrete type annotation") did not hint
  at the fix; there is no unit type name to annotate with.
- CLASS: frontend bug + diagnostic quality (matches SWARM-034)
- COST: 3

### T5 — `Queue` for BFS/Kahn (COST 2)
- WANTED: `ready = Queue()`, pushes inside an `if` in a loop, `while ready:`.
- HAD TO: `ready = Vector[Int]()` plus a `head` index.
- WHY: `binding 'ready' has conflicting concrete types across control-flow
  paths: queue[int] and queue`; `Queue[Int]()` is `UNKNOWN_SYMBOL_OR_TYPE`
  (bootstrap only lists inferred `Queue()`). Queue also has no emptiness/count
  query, so a size counter would be needed anyway.
- CLASS: frontend bug (local-variant of fixed SWARM-018) / missing surface
- COST: 2

### T6 — early return of an owned local (COST 2)
- WANTED: `if s < 0: return order` early in `bfs`, and `if u < 0: return dist`
  inside Dijkstra's `while true`.
- HAD TO: return a fresh `Vector[String]()` / `filled_ints(n, -1)` early, and
  drive Dijkstra's loop with a `settling` flag and a single final return.
- WHY: `OWNERSHIP_USE_AFTER_CONSUME: value 'order' was transferred ...` — the
  `return` inside the branch is treated as consuming `order` on the fall-through
  path too (reproduced in `d13_early_return_consumes.moss`).
- CLASS: frontend bug (flow-insensitive consumption across a terminating return)
- COST: 2

### T13 — `and` / `or` (COST 2)
- WANTED: `if not done[i] and dist[i] >= 0:` and `if s < 0 or d < 0:`.
- HAD TO: nested `if`s everywhere.
- WHY: checker and native accept them, Fast Debug rejects every `and`/`or`
  (`unsupported expression 'a and b'`; `a and (x >= 0)` even reports
  `unsupported or unresolved callable 'a and'`). `and`/`or` are not listed in
  `source_surface.operators`.
- CLASS: Fast Debug gap + docs gap (matches SWARM-037)
- COST: 2

### T14 — `return -1` (COST 2)
- WANTED: `return -1` in `distance`.
- HAD TO: `unreachable = -1` then `return unreachable`.
- WHY: `moss check` accepts it, but `moss fmt` fails with
  `FORMAT_PARSE_ERROR/TYPE_INFERENCE_FAILED: cannot infer the type of this
  return expression` and formats nothing.
- CLASS: formatter bug (same family as SWARM-033 / SWARM-003)
- COST: 2

### T7 — `for e in out[u]` (COST 1)
- WANTED: iterate a row of the adjacency list.
- HAD TO: `k` index loop with `out[u][k]`.
- WHY: `OWNERSHIP_USE_AFTER_CONSUME: value 'self' ...` — `for` over an indexed
  field element consumes the receiver. Moot anyway because of T8.
- CLASS: intended rule (I think; not verified further)
- COST: 1

### T8 — `for` loops (COST 1, large rewrite)
- WANTED: `for i in range(0, n)` and `for edge in edges`.
- HAD TO: `while` loops with manual counters everywhere.
- WHY: documented: "Fast Debug does not support for iteration yet" — even
  `range`.
- CLASS: Fast Debug gap (documented)
- COST: 1

### T10 — moving a Vector into a constructor, then counting it (COST 1)
- WANTED: `TopoResult(order: order, has_cycle: (order |> count) < n)`.
- HAD TO: `sorted_count = order |> count` first.
- WHY: `OWNERSHIP_USE_AFTER_CONSUME` — arguments are evaluated left to right
  and the first moves `order`.
- CLASS: intended rule
- COST: 1

### T11 — joining labels for output (COST 1)
- WANTED: `" ".join(order)` / f-strings.
- HAD TO: one `echo` line per node.
- WHY: first `text = items[i]` consumed the parameter (intended rule); then
  every String `+` fails native compilation (`expected &str, found String`,
  D6). `echo vector` also fails natively (D7).
- CLASS: native-lowering bug
- COST: 1

### T12 — Vector of a user type (COST 1, large rewrite)
- WANTED: `type Edge(src, dst, weight, alive)`, `edges: Vector[Edge]`,
  `edges[i].alive = false`.
- HAD TO: four parallel vectors.
- WHY: passes check and Fast Debug; native fails with
  `the type [Edge] cannot be indexed by i64` (no `as usize` on the index when
  a field is accessed).
- CLASS: native-lowering bug (D5)
- COST: 1

### T15 — Fast Debug test parity (COST 1)
- WANTED: run the `test` blocks in Fast Debug.
- HAD TO: generate `tmp/surface_graphkit/fd_tests.moss`, rewriting each
  `test` into a function called from `main`.
- CLASS: no project-wide interpreted test discovery/orchestration through Margo;
  standalone test sources can use `moss test --interp` (matches SWARM-039)
- COST: 1

### T1 — deleting from a Map (COST 0)
- WANTED: `del adj[u][v]`.
- HAD TO: tombstone flag per edge.
- WHY: bootstrap `Map.deletion_supported: false`.
- CLASS: missing surface (documented)
- COST: 0

### T16 — docs inconsistency (COST 0)
- TESTING.md examples use lowercase `int` and implicit trailing-expression
  returns (`left + right`); the Gentle Introduction uses `Int` and `return`.
  Not tried, just noted.
- CLASS: docs gap
- COST: 0

## Defect candidates (minimal reproducers)

All in `tmp/surface_graphkit/repro/`; `matrix.sh` regenerates `matrix.txt`.

| Repro | check --json | fmt | run --interp | native margo run | SWARM |
|---|---|---|---|---|---|
| d01_string_literal_compare (`x = "a" < "b"`) | ok (type string!) | ok | prints `a" < "b` | rustc E0308 | new |
| d02_interp_string_less (`s < t`) | ok | ok | `false` (wrong) | `true` | new |
| d03_interp_and_or | ok | ok | unsupported expression | ok | SWARM-037 |
| d04_interp_for | ok | ok | unsupported (documented) | ok | documented limit |
| d05_native_struct_index_field (`edges[i].dst`) | ok | ok | ok | rustc E0277 i64 index | new (cousin of SWARM-027) |
| d06_native_string_concat (`a + b`) | ok | ok | ok | rustc E0308 | new |
| d07_native_echo_vector | ok | ok | `[p, q]` | rustc E0277 Display | new |
| d08_fmt_return_negative (`return -1`) | ok | FORMAT_PARSE_ERROR | ok | ok | SWARM-033 family |
| d09_trailing_push_return_inference | TYPE_INFERENCE_FAILED | same | same | same | SWARM-034 |
| d10_indexed_member_call (`rows[0].push`) | MOSS_COMPILE_ERROR not implemented | same | same | same | new (missing surface) |
| d12_queue_branch_inference | conflicting types queue[int]/queue | same | same | same | new (SWARM-018 variant) |
| d13_early_return_consumes | OWNERSHIP_USE_AFTER_CONSUME | same | same | same | new |
| d14_string_field_element_push | OWNERSHIP_USE_AFTER_CONSUME on self | same | same | same | docs gap (copy spelling) |

## Validation results (final source)

Run from this directory (log: `tmp/surface_graphkit/final_validation.txt`):

| Command | Result |
|---|---|
| `moss fmt` | exit 0, no changes |
| `moss check src/main.moss --json` | ok, 0 diagnostics |
| `margo test` | 9 passed, 0 failed |
| `margo build` | exit 0 |
| `margo run` | exit 0 (output below) |
| `moss run --interp src/main.moss` | exit 0, byte-identical to native output |
| `margo debug` | byte-identical to native output |
| `moss run --interp --trace src/main.moss` | exit 0, 3676 NDJSON events (LocalRead, LocalWrite, BranchTaken, LoopIteration, Method/Function Enter/Exit, Return) |
| Fast Debug test harness (`tmp/surface_graphkit/fd_tests.moss`) | 9/9 PASS (negative control confirms interp `assertEqual` fails loudly) |
| `moss test --affected --json` | after a rename: 9 selected (conservative fallback); unchanged: 0 selected |

```text
bfs from shirt ( 4 nodes )
   1 shirt
   2 tie
   3 belt
   4 jacket
has cycle: false
topological order ( 7 nodes )
   1 shirt
   2 pants
   3 socks
   4 tie
   5 belt
   6 shoes
   7 jacket
dijkstra from shirt
   shirt 0
   tie 1
   jacket 3
   pants unreachable
   shoes unreachable
   belt 5
   socks unreachable
removed shirt->belt: true
bfs from shirt ( 3 nodes )
   1 shirt
   2 tie
   3 jacket
dijkstra from shirt
   shirt 0
   tie 1
   jacket 3
   pants unreachable
   shoes unreachable
   belt unreachable
   socks unreachable
after adding jacket->shirt, has cycle: true
partial topological order ( 4 nodes )
   1 pants
   2 socks
   3 belt
   4 shoes
```

## AI-assist log

| # | Invocation | Helped? |
|---|---|---|
| 1 | `moss agent bootstrap --json` | yes: `source_surface` told me Map has no deletion and Fast Debug has no `for` before I wrote code |
| 2 | moss-language + moss-agent-workflow skills | yes: collection ops, `Vector[T]()`, no recursion |
| 3 | Gentle Introduction sections 1-9, 12-17 | yes: types/methods, pipelines, `for` limit |
| 4 | TESTING.md (first 120 lines) | partly: test placement; lowercase `int` confused me |
| 5 | ~20 `moss check --json` runs | yes: codes/lines were accurate; `legal_alternatives` generic; `fixes` always empty |
| 6 | `moss type x --source probe2.moss` | yes: proved `"a" < "b"` was typed `string` (D1) |
| 7 | `moss ownership Graph.bfs` while failing | no: returns the same error, no partial facts |
| 8 | `moss inspect/effects/ownership/calls/why/cost Graph.dijkstra` | mildly: confirmed READ self; `why`/`cost` had little content for plain code |
| 9 | `moss ownership Graph.add_edge`, `push_into` | yes: confirmed the helper infers WRITE on `row` |
| 10 | `moss impact Graph.dijkstra` | informative (dependents list), not decision-changing |
| 11 | `moss edit rename filled_bools -> make_bool_vector -> back` | worked cleanly |
| 12 | `moss test --affected --json` x3 | worked; saved nothing at this size |
| 13 | `moss fmt` x5 | found the `return -1` formatter bug; otherwise no-op |
| 14 | `moss run --interp` ~25, `--trace` x2, `margo debug` x1 | yes: found the `and`/`or` and String `<` gaps |
| 15 | `margo test/build/run` ~8 | yes: native exposed D5/D6/D7 |
| 16 | `moss agent session-report-template --json` | used for the closeout questions |

Examples under `examples/` were not browsed at all. After the first
`moss check` pass I read `examples/swarm/FINDINGS.md` (read-only) to match
defects to SWARM ids.
