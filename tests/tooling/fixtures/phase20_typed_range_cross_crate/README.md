# Phase 20 cross-crate typed `Range` ABI reproducer

The exported provider accepts a concrete `Range`. The consumer passes a
`Range` returned by `FileIO.read` through an ordinary helper call; no Branch
pipeline is involved.

From the repository root, build the provider, then set `MOSS_MODULE_PATH` to
the absolute `provider/build/debug` directory while checking and building the
consumer with the repository-local `moss` compiler. Keep any generated build
files under the repository. The current checker accepts the consumer with no
diagnostics, but native `moss build` fails at rustc E0308: the provider expects
`moss_mapper::Range` and receives the consumer's `moss_fileio::Range`.

This is a recorded correctness gap. The eventual fix needs a shared native
`Range` ABI across crate boundaries or a source diagnostic that rejects this
call before Rust generation. Once fixed, the program should print `65`.
