# Team H host SDK architecture freeze

**Independent reviewer:** Team H L6 Architect  
**Backplane:** `2845390534272e330a0ecc7c9e970d1815eb0588`  
**Language/tool contract:** `moss-0.1`, agent protocol `moss-agent-1`, native ABI v9  
**Status:** API boundary frozen for source-only work; host/runtime expansion deferred

## Decision

Team H can implement a useful, honest source-only subset without changing the
compiler or runtime:

- `moss.path`: explicitly **lexical, forward-slash, String-based** path
  manipulation;
- `moss.io`: regular-file byte reads that keep `FileIO` and `Range` inside the
  provider and return owned `Vector[Int]` values, preferably with an explicit
  maximum-size policy.

The current language does not support a general filesystem, environment, or OS
process SDK. Those APIs require runtime primitives. They must not be simulated
with lossy `FileIO.open` probes, `echo`, or the Moss `Executor`.

This is a design freeze, not authorization to add primitives. Team H should
ship only the green source-only subset and document the red boundary. A later
host-runtime phase can adopt the primitive requests below.

## Classification

- **Supported** means a compiler-recognized Moss operation exists now. It may
  still be unsuitable as a public package boundary.
- **Composable** means Team H can implement the operation in ordinary Moss on
  this backplane with a precise restricted contract.
- **Requires primitive** means the general operation cannot be implemented
  faithfully from the current source surface. One failed guessed spelling was
  not used as evidence; the classification combines bootstrap discovery,
  canonical documents, semantic queries, and minimized probes.

## Existing contracts that control the design

`FileIO` is synchronous, blocking, regular-file-only, explicitly offset based,
and pinned to a root-local binding or domain field. The supported modes are
`ro`, `rw`, and `create`; none truncates. Reads, batches, chunks, and writes need
compiler-proven finite per-request bounds. `Range` is a borrowed read-only view
whose owner must remain open. It cannot be returned, stored normally, sent, or
used as a concrete cross-crate value.

The language does provide an important composition route: iterate
`file.chunks(4096)`, copy each borrowed Range into an owned `Vector[Int]`, close
the file, and return the vector. A source-backed and source-free provider probe
did exactly this. Its imported ABI-v9 effect summary retained `external_io`,
`may_panic`, `may_diverge`, and all six standard `std.FileError` variants.

The inverse route is absent. `FileIO.write` accepts only a statically bounded
`String` or `Range`, not `Vector[Int]`. There is no arbitrary byte-vector to
String constructor, and the concrete Range ABI is deliberately fail-closed.
Consequently a general package-level byte writer is not source-composable.

Raised errors may contain only bounded, heap-free scalar/enum payloads. Host
errors therefore cannot attach a path String, captured output, or an OS error
message. Preserve `std.FileError` for existing FileIO failures. Any new raised
host error should use stable tag-only cases or bounded integer fields, with
human text emitted separately. Do not duplicate `FileError` behind a package
specific enum.

Fast Debug intentionally rejects FileIO. Native debug/release are the execution
oracles for `moss.io`; pure `moss.path` remains eligible for Fast Debug.

## Proposed operation matrix

### `moss.path`

The source-only API is lexical and uses `/` regardless of the host OS. Indices
are Unicode code-point indices because Moss String indices have that unit. It
does not validate filesystem names, resolve symlinks, inspect a current
directory, or preserve arbitrary non-Unicode OS path bytes.

| Proposed operation | Class | Frozen contract / reason |
| --- | --- | --- |
| `join(parts)` | Composable | Join nonempty lexical components with `/`; specify empty-component and repeated-separator behavior. Use `moss.text`, not quadratic character concatenation. |
| `normalize_lexical(path)` | Composable | Collapse `.` and repeated `/`, and resolve `..` without consulting a filesystem. Preserve leading-root status; never claim symlink safety. |
| `basename(path)` | Composable | Last lexical component; document root, trailing slash, and empty path results. |
| `dirname(path)` | Composable | Lexical parent only; document `""`, `"/"`, and relative singleton results. |
| `extension(path)` / `stem(path)` | Composable | Operate on the basename; freeze leading-dot and final-dot behavior. |
| `is_absolute(path)` / `is_relative(path)` | Composable | Only for the frozen `/` lexical grammar. A host-native answer is a different API. |
| `components(path)` | Composable | Return owned String components; no borrowed substring ABI. |
| `canonicalize(path)` / `realpath(path)` | Requires primitive | Needs current-directory, symlink, metadata, permissions, and host path semantics. |
| native separator/root/drive inspection | Requires primitive | Moss exposes no target-platform path model. Do not guess Windows behavior from `/`. |

Public naming should include `lexical` wherever users might otherwise infer a
filesystem lookup. A future native path type should be distinct from this
String utility layer rather than silently changing its contract.

### `moss.io`

| Proposed operation | Class | Frozen contract / reason |
| --- | --- | --- |
| `FileIO.open(path, mode)` | Supported | Compiler built-in for Solo regular files. Keep the capability inside the using module/root. |
| `read(offset, N)`, `chunks(N)`, Range traversal/slice | Supported | `N` must have a statically proven finite bound. Dynamic `size: Int` is rejected with `FILEIO_UNBOUNDED_REQUEST`. |
| direct `write(offset, String|Range)` | Supported | Payload needs a static byte bound. A general String parameter and `Vector[Int]` are not accepted payloads. |
| `sync`, `sync(dataonly)`, `close` | Supported | Existing explicit durability/lifecycle operations. `create` may owe a parent-directory sync. Close is not sync. |
| `read_all_bytes(path)` | Composable | Fixed-size chunk loop, copy into owned `Vector[Int]`, close before return. It can allocate without a global bound and its effect summary is `may_diverge`; prefer a limited form. |
| `read_bytes_limited(path, max_bytes)` | Composable | Read fixed-size chunks, stop/raise before the owned vector exceeds the caller limit. Per-request bound remains constant; use a tag-only `LimitExceeded`. |
| `read_prefix(path)` with a fixed package constant | Composable | A fixed request bound works. A public arbitrary `size: Int` does not. Name or document the fixed cap. |
| `read_utf8_text(path)` | Requires primitive today | Pure UTF-8 decoding returns scalar integers, but Moss has no scalar/byte-vector to String construction boundary. Do not report scalar output as a String reader. |
| `write_all_bytes(path, Vector[Int])` | Requires primitive | `FILEIO_INVALID_PAYLOAD`; no owned-byte write primitive or safe conversion route. |
| general `write_text(path, value)` | Requires primitive | A function parameter has no compiler-proven finite byte bound; no source-level bounded String contract exists. Literal writes at a call site are not a package API. |
| append, truncate, seek/cursor | Requires primitive | FileIO has explicit offsets and no stat/truncate/cursor operation. |
| stdin/stdout/stderr streams, read-line | Requires primitive | FileIO rejects non-regular files. `echo` is a statement, not a general stream capability. |
| async I/O or portable completion API | Requires primitive | Executor creates Moss roots; it is not an async task, future, descriptor, or pipe API. |

`read_bytes_limited` should be the main read API. Returning every byte from an
untrusted, concurrently growing file without a cap makes allocation and
termination part of the public hazard. The API must validate negative limits
before opening and retain the exact `std.FileError` raise set.

### `moss.fs`

| Proposed operation | Class | Frozen contract / reason |
| --- | --- | --- |
| open existing regular file / open-or-create | Supported | These are the `ro`, `rw`, and `create` FileIO modes, not general filesystem objects. `create` neither truncates nor guarantees exclusive creation. |
| `exists(path)` | Requires primitive | Open/recover cannot distinguish absence from directory, permission, non-regular file, or the live-inode `InUse` registry; it is also race-prone. |
| `is_file`, `is_dir`, `file_type`, `metadata`, `size` | Requires primitive | No stat/lstat surface. FileIO only validates that an opened target is regular. |
| `list_dir`, `create_dir`, `create_dir_all` | Requires primitive | Directories are outside the Phase 20 surface. |
| `remove_file`, `remove_dir`, `rename` | Requires primitive | Explicitly deferred by the FileIO design. |
| `truncate`, `set_len` | Requires primitive | No open mode truncates and no truncate operation exists. |
| `copy` | Requires primitive for a general contract | Range chunks can copy bytes, but an existing longer destination keeps its tail and `create` cannot atomically require a fresh target. A helper with an unenforceable “destination is new” precondition is not a filesystem copy API. |
| atomic replace/write | Requires primitive | Needs exclusive temp creation, truncate, rename, and directory sync control. |
| permissions, timestamps, links, symlinks, canonicalization | Requires primitive | No metadata or link surface. |
| watch/notify | Requires primitive | No host event source or descriptor capability. |

Do not implement `exists` as `try FileIO.open` followed by `false` on every
error. That would turn access denial, directories, and a currently owned inode
into false negatives and would erase typed-error information.

### `moss.env`

| Proposed operation | Class | Frozen contract / reason |
| --- | --- | --- |
| `get(name)`, `has(name)`, environment snapshot | Requires primitive | No process-environment source or byte/String conversion policy exists. Missing value should not be conflated with invalid host encoding. |
| command-line `args()` | Requires primitive | No root argument surface exists. |
| `current_dir()` / `set_current_dir()` | Requires primitive | No current-directory surface. Mutation would also be process-global and needs an explicit concurrency contract. |
| host name, user/home/temp discovery | Requires primitive | No host metadata source; environment-variable conventions are not a portable substitute. |

If later added, environment and argument APIs must state how non-Unicode Unix
bytes and invalid host strings are represented. Returning Moss String silently
cannot preserve every host value. Prefer a byte-preserving lower layer and an
explicit UTF-8 conversion policy.

### `moss.process`

| Proposed operation | Class | Frozen contract / reason |
| --- | --- | --- |
| `run` / `spawn` | Requires primitive | No OS process creation, command representation, or child handle exists. Moss `Executor` is unrelated. |
| wait/status/signal/kill | Requires primitive | Requires a runtime-owned child capability and lifecycle/ownership rules. |
| child cwd/environment | Requires primitive | Depends on missing path/environment host interfaces and byte encoding contracts. |
| stdin/stdout/stderr inherit/file/pipe/capture | Requires primitive | FileIO accepts regular files only; pipes and captured byte streams are outside the surface. |
| `exit(code)` | Requires primitive | Needs explicit Root cleanup, pending FileIO, domain, and executor shutdown semantics; it cannot be a normal helper return. |
| pid/process information | Requires primitive | No host process metadata source. |

A minimal future synchronous process primitive still needs a byte-oriented
argument/environment representation, bounded output policy, typed spawn/wait
errors, child cleanup on raised exits, and a clear interaction with the one
active Moss Executor. Adding only a `run(String)` spelling would leave these
contracts unresolved.

## Package and ABI rules

1. `moss.path` may depend on `moss.text` for code-point slicing/search and the
   segment builder. It must document that it is lexical String processing.
2. `moss.io` may depend on `moss.bytes`. Copy Range contents while the FileIO
   owner is live, then return only owned `Vector[Int]`.
3. Do not export or import a concrete `Range`, `RangeBatch`, or FileIO-bearing
   record. Concrete cross-crate Range fails closed; generic Range helpers such
   as `bytes.from_range` specialize in the consumer crate.
4. Cross-package consumers must recover the inferred standard FileError set.
   Source-free ABI-v9 preserves that set and binds the `.mossi` contract to the
   selected native provider artifact.
5. Fast Debug tests cover pure path logic. Native debug and release cover all
   FileIO code. A Fast Debug FileIO rejection is expected evidence, not a test
   failure to hide.

## Primitive requests for a later host-runtime phase

The smallest coherent expansion is not a collection of guessed Rust wrappers.
It is a byte-preserving host boundary with explicit owned values:

1. owned byte write plus truncating/exclusive file creation;
2. stat/lstat, directory iteration, mkdir/remove/rename, and canonicalization;
3. environment/argument/current-directory snapshots with raw-byte and UTF-8
   policies;
4. synchronous child spawn/wait with bounded byte capture before any async
   process API;
5. stable tag/int host-error payloads and separate diagnostic text.

Each primitive needs native debug/release, source-free ABI-v9, failure-path
cleanup, and platform-contract tests before an SDK wrapper claims support.

## Task DAG and acceptance focus

The source-only work can proceed in parallel:

1. freeze lexical path edge cases and implement `moss.path`;
2. implement capped owned-byte reading with FileIO/Range hidden internally;
3. independently test path empty/root/dot/dot-dot/repeated separators and
   Unicode code-point behavior;
4. independently test I/O empty, exact-chunk, multi-chunk, embedded NUL,
   missing/permission/non-regular failures, size limit, native debug/release,
   imported effects, and source-free ABI-v9;
5. review documentation to ensure no filesystem, portability, Fast Debug,
   truncation, or text-decoding overclaim remains.

Filesystem mutation, environment, and process implementation have no dependency
edge into this source-only DAG because they are deferred pending primitives.

## Evidence

Commands executed from the exact backplane worktree included:

```text
./moss agent bootstrap --json
./moss agent capabilities --json
./margo run --json                         # source-backed FileIO provider/consumer
./margo build --release --json
./moss effects entity-v1:function:hostprobe__read_all_bytes --source ... --json
MOSS_MODULE_PATH=<provider/build/debug> ./margo run --json
./moss check <dynamic-size-read> --json
./moss check <Vector[Int]-write> --json
./moss check <Range-return> --json
./margo debug                              # expected FileIO rejection
python3 tests/tooling/check_phase20_fileio_semantics.py ./moss <scratch>
python3 tests/tooling/check_phase20_cross_crate_range_abi.py ./moss
python3 tests/tooling/check_phase21_imported_effects.py ./moss <scratch>
```

Observed results:

- native debug and release source-backed consumer: `5`, `5`, `5` for prefix
  length, whole-file byte count, and capped byte count;
- source-free ABI-v9 consumer using only provider `.mossi`/`.rlib`: the same
  `5`, `5`, `5` result;
- imported effects: `external_io=true`, six exact `std.FileError` variants,
  `may_panic=true`; whole-file chunk traversal additionally `may_diverge=true`;
- `FILEIO_UNBOUNDED_REQUEST` for a dynamic read size;
- `FILEIO_INVALID_PAYLOAD` for direct `Vector[Int]` write;
- `FILEIO_PINNED_OWNERSHIP` for returning a borrowed Range;
- Fast Debug: “FileIO execution is intentionally unsupported in Phase 20”;
- all three focused Phase 20/21 tooling validators passed.

Canonical basis: `docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md`,
`docs/MOSS_PHASE_21_ERROR_HANDLING.md`, `docs/MOSS_STANDARD_ERRORS.md`,
`docs/PHASE_21_SHARED_ABI.md`, `docs/MOSS_SOURCE_FREE_ABI_V9.md`, and the live
bootstrap `source_surface.fileio` / `source_surface.executor` contracts.
