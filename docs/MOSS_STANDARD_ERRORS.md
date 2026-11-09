# Compiler-defined standard errors

Phase 21 defines three closed, nominal error enums in the compiler-owned
`std` namespace. Applications can use `std.FileError`, `std.ParseError`, and
`std.ConversionError` directly, without an import or declaration. Their
unqualified names are shorthand when the source module has no declaration of
the same name. `import std` is accepted for clarity and does not load a
provider artifact. The exact tag-only variants are:

| Type | Variants |
| --- | --- |
| `std.FileError` | `NotFound`, `PermissionDenied`, `NotRegularFile`, `InUse`, `Full`, `IO` |
| `std.ParseError` | `Invalid`, `Overflow` |
| `std.ConversionError` | `NonFinite`, `Overflow` |
| `std.CollectionError` | `Empty`, `MissingKey`, `InvalidIndex` |

`FileIO` operations, `parse_int`, `parse_float`, and `to_int` always raise
these canonical types. An imported `provider.ParseError`, for example, is a
different nominal type even when it has a case named `Invalid`. An exported
provider enum with a standard short name remains provider-owned. Its unqualified
name within that provider denotes the provider type; use `std.ParseError` to
name the built-in type there. Recovery against the provider type cannot consume
a built-in raise. Root and `main` totality checking still require every
remaining built-in alternative to be handled.

For migration, an exact, unexported legacy declaration of one of the three
enums aliases the corresponding standard type. It must have the complete
tag-only variant set shown above. A malformed declaration cannot replace the
standard contract and receives `BUILTIN_ERROR_TYPE_SHAPE` when used as an
implicit-module standard declaration. Explicitly exported declarations keep
their own nominal identity, including when their shape happens to match the
standard type. This rule avoids silently changing a provider's public ABI.

The compiler's internal semantic and ABI identity is `std__FileError`,
`std__ParseError`, or `std__ConversionError`. Generated module crates each
contain the same closed standard definitions; source-free raised outcomes
translate tag-only values at crate boundaries while preserving the canonical
Moss identity. The provider `.mossi` carries raised identities but does not
export a synthetic `std` module.

Native provider and consumer crates have separate Rust declarations for each
standard enum. The compiler converts values at imported function, object
method, and handler boundaries. READ parameters use a temporary provider value,
CONSUME parameters transfer an owned converted value, and WRITE parameters
copy the final provider value back to the caller before either normal return
or typed-raise propagation. This preserves the caller's observable mutation
through recovery.

`Vector.remove(index)` and `Vector.swap_remove(index)` raise
`CollectionError.InvalidIndex` for an invalid index. `Map.remove(key)` raises
`CollectionError.MissingKey` when the key is absent. The operations return the
removed value on success. Empty `pop()`, absent strict Map indexing, and
out-of-range Vector indexing panic; they do not raise `CollectionError`.

## Ownership diagnostics

These are compile-time diagnostic codes, not variants of the standard error
enums:

| Code | Meaning |
| --- | --- |
| `IMPLICIT_COPY` | A non-primitive value would be copied without `copy(...)`. |
| `COPY_REQUIRES_MOSS_COPY` | The argument to `copy(...)` is not a `Moss::Copy` type. |
| `IMPLICIT_FIELD_MOVE` | A non-primitive field value leaves its field without an allowed `take(...)` or `copy(...)`. |
| `TAKE_REQUIRES_FIELD_PATH` | `take(...)` does not name an assignable field-only path. |
| `TAKE_PATH_CONTAINS_INDEX` | The `take(...)` path contains an index. |
| `TAKE_UNSUPPORTED_FIELD_TYPE` | The target field is not a Vector, Map, or option. |
| `RESERVED_NAME` | A declaration uses a reserved builtin name such as `copy` or `take`. |
