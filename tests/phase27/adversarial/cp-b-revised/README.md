# CP-B revised independent probes

Run from the candidate worktree (`dacf2fab1ab252968dc974b180e7279e46cb22e9`):

```sh
make --jobs="$(nproc)" moss-dev
python3 tests/tooling/check_phase27_est_mossi.py build/compiler/moss-dev
python3 tests/phase27/adversarial/cp-b-revised/cross_compiler_probe.py \
  build/compiler/moss-dev \
  ../phase27-baseline-4d6d0c8/build/compiler/moss-dev
```

The baseline worktree must be at `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`
and built with `make --jobs="$(nproc)" moss-dev`. The same-binary gate runs
both library and app packages at fixed paths per run and compares raw bytes.
The cross compiler probe copies each compiler in turn to one fixed absolute
path, leaves Margo's compiler identity intact, builds both packages, checks
that each artifact records the fingerprint recomputed from its own compiler
identity, recomputes the baseline identity fingerprint and dependent body hash,
and compares all remaining file bytes.

All temporary build projects and reports are created below the candidate
worktree's `tmp/` directory.
