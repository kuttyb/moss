# Team H Host Services — independent Architect final review

**Reviewer:** independent Team H L6 Architect

**Reviewed candidate:** `db41c09d3086629bddc37c5aeabb585161dfdd53`

**Branch:** `phase24-host`

**Base:** `2845390534272e330a0ecc7c9e970d1815eb0588`

**Verdict:** **GO** for Team I integration

## Verdict

The exact candidate is internally consistent with the source-only Team H design
freeze. It delivers a pure POSIX-lexical `moss.path`, a deliberately narrow
regular-file `moss.fs`, and executable feasibility evidence instead of fake
`moss.io`, `moss.env`, or `moss.process` packages. No compiler, runtime, unsafe
FFI, shell, or Executor substitution is present in the candidate diff.

Fresh adversarial path and FileIO cases passed in native debug and release.
Compiled-only ABI-v9 consumers for both delivered packages passed. FileIO and
Range stay internal to provider operations; public results are owned values.
The permanent negative probes reproduce the current cross-crate FileIO type
identity blocker, dynamic request-bound rejection, missing Env/Process surface,
and expected Fast Debug FileIO rejection.

No blocking correctness or scope defect was found at the reviewed SHA.

## Independent findings

### API and correctness

- Path normalization agrees with the frozen lexical contract for empty/root,
  excess `..`, repeated separators, Unicode components, absolute replacement
  during join, and component-boundary prefix/suffix cases.
- The implementation does not claim host canonicalization, symlink, Windows,
  metadata, existence, or security semantics.
- `moss.fs` returns owned `Vector[Int]` values. Borrowed Range values are copied
  or written while both FileIO owners remain live and never cross the package
  boundary.
- Fresh file cases covered zero bytes, 4096 bytes, 4097 bytes, a read spanning
  the 4096 boundary, and an offset beyond EOF. `read_all` and `read_prefix`
  returned the exact expected sizes.
- A directory path raised `FileError.NotRegularFile`; negative offsets remain a
  distinct `FsError.InvalidOffset`; the permanent suite also rechecked missing
  files and native-only Fast Debug behavior.
- `copy_prefix` was tested against a pre-existing 5000-byte destination. It
  replaced only the first 4096 bytes and preserved the tail exactly as the
  README warns; it does not masquerade as truncate or atomic replacement.
- `read_all` has allocation proportional to file size and no caller cap. That
  is a disclosed limitation, not a bounded-memory API. Consumers handling
  untrusted files should use `read_prefix` until a capped API is provided.

### Feasibility boundary

- A provider that opens FileIO internally and returns an ordinary owned/scalar
  value works source-backed and compiled-only.
- A public FileIO capability parameter still reaches a native E0308 type
  mismatch because provider and consumer crates lower distinct FileIO types.
  The feasibility report accurately treats this as an unresolved ABI/compiler
  boundary and recommends an earlier structured diagnostic if the surface
  remains forbidden.
- Env and Process probes remain `UNKNOWN_SYMBOL_OR_TYPE`. No single guessed
  spelling is used alone as evidence: bootstrap, canonical Phase 20/21 docs,
  and the absence of a capability surface support the conclusion.
- The report correctly distinguishes the Moss Executor from OS processes,
  tasks, futures, descriptors, and pipes.

### Provenance and project records

- `git merge-base` is the declared base
  `2845390534272e330a0ecc7c9e970d1815eb0588`.
- Candidate history is the architecture freeze, path/fs implementation, I/O
  feasibility evidence, and coordinator closeout only. A base-to-candidate
  name audit found no `src/`, runtime, include, or tool implementation changes.
- Feedback validation passes. The trail includes positive tooling/language
  evidence and candid ecosystem/ABI friction without duplicating SWARM issues.
- The aggregate Team H efficiency record uses observable counts and explicitly
  leaves unavailable usage data unknown. The leaf I/O efficiency note is a
  contemporaneous pre-handoff record and therefore contains pending/unknown
  fields; the aggregate Team record is the closeout authority.

## Non-blocking documentation observations

1. `sdk/fs/README.md` says both that `read_all` returns the complete file with
   proportional memory and later that the API does not claim to read an
   unbounded file in one call. The latter sentence should be narrowed to
   `read_prefix`; the implementation and primary table are otherwise candid.
2. `path.stem("archive.")` currently returns `"archive."` while `extension`
   returns empty. This is stable in the fresh probe but the final-dot convention
   is not explicitly documented. Freeze it in a follow-up API clarification.
3. `git diff --check <base>..<candidate>` reports Markdown hard-break trailing
   spaces in two reports and final blank lines in closeout ledgers. A plain
   `git diff --check` on the clean candidate worktree succeeds because it has no
   working-tree diff. This is formatting debt, not a source or generated-code
   defect, but future validation reports should state which range was checked.

These items do not change runtime behavior, public safety boundaries, or the GO
verdict.

## Commands and results

Executed independently in a clean review worktree whose initial HEAD was the
exact candidate:

```text
./moss agent bootstrap --json
  PASS: language_version=moss-0.1, agent protocol 1

python3 sdk/path/tests/check_differential.py
  PASS: POSIX lexical normalization differential cases
python3 sdk/path/tests/check_source_free.py
  PASS: compiled-only ABI-v9 moss.path consumer

python3 sdk/fs/tests/check_negative.py
  PASS: FILEIO_UNBOUNDED_REQUEST
python3 sdk/fs/tests/check_native.py
  PASS: native debug/release consumer and Fast Debug boundary
python3 sdk/fs/tests/check_source_free.py
  PASS: compiled-only ABI-v9 moss.fs consumer

python3 sdk/host/io/check_feasibility.py
  PASS: same-unit/path-provider positives and capability blocker reproduced

python3 tests/tooling/check_agent_feedback.py
  PASS
python3 tests/tooling/check_agent_skills.py
  PASS

python3 tmp/team-h-final-adversarial.py
  PASS: fresh path and FileIO edge cases in native debug and release

git merge-base HEAD 2845390534272e330a0ecc7c9e970d1815eb0588
  2845390534272e330a0ecc7c9e970d1815eb0588
```

The coordinator records successful `TMPDIR="$PWD/tmp" make check` and
`make examples` at the candidate. Those full gates were not rerun in this
focused independent review; this verdict does not relabel them as independent
Architect executions.

## Remaining limitations accepted by this GO

- FileIO execution is native-only; Fast Debug rejection is expected.
- `read_all` is not capped and may allocate the size of the file.
- `copy_prefix` copies at most 4096 bytes, does not truncate, and is not atomic.
- FileIO capability parameters are not a supported cross-package ABI.
- General metadata, directory mutation, environment, arguments, current
  directory, process creation/wait/status, and host-native paths remain
  primitive-gated.

The GO applies only to candidate
`db41c09d3086629bddc37c5aeabb585161dfdd53`. Any functional change requires a
new review; cherry-picking this report alone does not alter the reviewed code.
