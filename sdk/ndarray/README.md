# moss.ndarray

`moss.ndarray` provides owned, contiguous, multidimensional `Float` arrays for
Moss v0.1. The A1 core includes shape validation, checked indexing, reshaping,
axis permutation, two-dimensional transpose, and `moss.linalg` Matrix
interoperability. Broadcasting, slicing, arithmetic, and reductions are
deliberately deferred to later Team A workstreams.

Add the package to a consumer manifest:

```toml
[dependencies]
ndarray = { path = "../sdk/ndarray" }
```

Then use `import ndarray` in Moss source.

## Representation and ownership

`NdArray` has public `shape`, `strides`, and `data` vectors. Storage is C-style
row-major and canonical: the last stride is `1`, and each preceding stride is
the checked product of dimensions to its right. Every public operation validates
an input array before indexing it, so a malformed directly constructed record
raises a typed `NdArrayError` rather than indexing invalid storage.

Constructors copy caller vectors. `shape_copy`, `flatten`, `array_clone`,
`reshape`, `permute_axes`, `transpose`, and Matrix conversions also return owned
copies. This package does not claim views, borrowing, copy-on-write storage, or
zero-copy Matrix conversion.

## Shape contract

- `shape=[]` is a rank-zero scalar with one element and no strides.
- Any zero dimension makes the array empty. `[2, 0, 3]` has canonical strides
  `[0, 3, 1]` and zero elements.
- Negative dimensions raise `NdArrayError.InvalidShape`.
- Any required dimension or suffix product larger than signed Int64 raises
  `NdArrayError.Overflow` before multiplication.
- `get` and `set` require exactly one nonnegative, in-range index per axis.
- `reshape` preserves element count and copies storage.
- `permute_axes` requires each existing axis exactly once. `transpose` is the
  rank-two convenience operation and otherwise raises `InvalidPermutation`.

## Core API

| Function | Contract |
| --- | --- |
| `validate(array) -> Bool` | Checks shape, canonical strides, overflow safety, and data length without indexing data. |
| `from_flat(shape, data)` | Constructs from copied row-major data. |
| `scalar(value)` | Constructs a rank-zero scalar. |
| `zeros(shape)`, `ones(shape)`, `full(shape, value)` | Construct filled owned arrays. |
| `rank`, `shape_copy`, `size`, `element_count` | Validated metadata access. |
| `get`, `set` | Checked multidimensional access. `set` requires a mutable array. |
| `array_clone`, `flatten` | Independent owned copies. |
| `reshape` | Copy into a new shape with the same element count. |
| `permute_axes`, `transpose` | Copy while reordering axes. |
| `from_matrix`, `to_matrix` | Copy row-major data to or from `linalg.Matrix`. |

Matrix conversion requires two positive extents because `linalg.Matrix` does
not represent empty dimensions. `to_matrix` rejects non-rank-two arrays with
`MatrixRankMismatch` and empty axes with `MatrixEmptyDimension`.

## Validation

From this directory:

```sh
../../margo test
../../margo --release test
../../margo --interp test
git diff --check
```
