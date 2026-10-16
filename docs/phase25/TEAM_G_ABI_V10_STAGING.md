# Team G ABI-v10 staging status

The compiler still emits and consumes production ABI v9. Team S and Team G
agreed to switch only at the joint integration cut. This branch stages the
ABI-v10 container in `src/source_free_abi_v10.hpp` and tests it with
`tests/tooling/abi_v10_staging.cpp`.

## Implemented in the staged codec

- Exact `moss-module-interface-v3`, schema 10, native ABI 10 header parsing.
  Older magic or a v3 record claiming schema/native ABI 9 receives
  `SOURCE_FREE_ABI_REBUILD_REQUIRED`.
- Package-qualified `module-v1:<package>@<version>::<module>` identities and
  ordered imports with bound binary and semantic SHA-256 hashes.
- Bounded, length-framed canonical records in the binary, semantic, private
  dependency, and provenance sections. The parser checks UTF-8, order, record
  uniqueness, byte limits, counts, truncation, and trailing data.
- Domain-separated SHA-256 binary, semantic, and interface hashes. The provider
  digest covers the exact emitted interface bytes with its own line removed.
- Validation of the selected rlib archive for exactly one matching native object
  marker. Rust metadata copies do not satisfy the marker requirement.
- `src/abi_v10_records.hpp` defines a bounded tagged `TypeExpr` wire encoding
  with scoped bound-variable ordinals. Its nominal-template, open-summary,
  concrete-instance, specialization, and key-proof payload codecs validate record versions,
  structural closure, access modes, topologically ordered requirement parents,
  dependency ordering, canonical specialization keys, and key-proof IDs.
  An instance validator checks provider binary binding, template arity and
  representation hash, and exact recursive field substitution.
  Wire types are serialization values, not a second inference engine.

The interface hash excludes its own header field and the provider digest to
avoid self-reference. It includes the other header fields and all canonical
sections. This is the precise byte encoding for the staged line format.

## Integration still required

This codec is isolated from `src/moss.cpp` while the authoritative Type IR and
solver are being integrated. The container still treats payloads as opaque;
the compiler must dispatch each semantic record to the typed payload validator
before importing it. Method templates, full effect leaves, private semantic IR,
and provenance records still need typed codecs and
cross-record referential validation. The production loader does not accept v10
or emit a v10 rlib marker. Therefore no compiled-only
Moss provider/consumer, two-consumer identity, summary freshening, cache
invalidation, or source-free proof-matrix result is claimed here.

The next compiler integration must use the single G-A structured Type IR for
record payloads and make selected artifact validation precede semantic import.
Team S's v9 production branch and historical v9 migration fixtures remain
unchanged until the single joint ABI cutover.

## Validation

From the repository root of this worktree:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  tests/tooling/abi_v10_staging.cpp -o tmp/abi_v10_staging
tmp/abi_v10_staging
```

The test covers canonical roundtrip, sealed payload mutation, v9/schema/native
version rejection, trailing bytes, duplicate records, invalid UTF-8,
metadata-only marker rejection, and duplicate native-object markers.
It also checks nested tagged types, bound-variable scope, nominal field
uniqueness, requirement-parent order, closed specialization keys, and forged
key-proof identities. Concrete-instance cases reject a wrong provider binary
identity or field type substituted differently from the nominal template.
