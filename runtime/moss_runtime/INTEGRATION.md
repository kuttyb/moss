# `moss_runtime` integration contract

This crate owns the shared native representation of `MossMap<K,V>` and
`MossSet<K>`. It has no key-operation policy in either value type. The operation
methods accept a generic, monomorphized equality hook only for the duration of
the call. Team S's selected `key-protocol-v1` targets must supply both the hash
and equality in consumer-owned specialized free functions. No Rust `Eq`/`Hash`
implementation or fallback is used for nominal keys.

At the single ABI-v10 cutover, compiler build/link plumbing must:

1. Compile this crate once to `libmoss_runtime.rlib` per compatible runtime
   source/toolchain/profile set. Pass that exact artifact to every generated
   provider, consumer, specialization, and executable crate with
   `--extern moss_runtime=<artifact>` and a matching dependency search path.
2. Add a `binary_contract` runtime identity containing the name
   `moss_runtime`, source version `0.1.0`, and the SHA-256 of the exact rlib
   artifact used. Check the provider and consumer identities before invoking
   rustc. Include the identity in cache keys. A mismatch must be a Moss
   diagnostic; it must not proceed to a Rust type error.
3. Lower Map and Set values to `moss_runtime::MossMap<K,V>` and
   `moss_runtime::MossSet<K>`. Lower operations to consumer-owned static free
   functions that call the canonical Team S key-proof targets and pass their
   result to `get_with`, `insert_with`, or `remove_with`. The hook is transient;
   the stored value carries only keys, values, hashes, buckets, and length.
4. Move Team S's `MossString` and scalar key-protocol modules into this crate
   at the coordinated cutover. Team S owns their code and semantics. Include
   the ABI-v10 String representation record in the binary contract.

The current v9 compiler still embeds its runtime prelude. This crate is an
isolated implementation and proof until the coordinated ABI-v10 cutover; it
does not switch production linkage on its own. Run
`python3 tests/phase25/runtime/check_runtime.py` to compile unit tests and
one provider with two separate consumer crates against the same runtime rlib.
