# moss.tables

`moss.tables` is the Phase 24 typed columnar table core. It intentionally has no
package dependencies; CSV adapters and relational operations are separate later
workstreams.

## Data contract

`Column` is a statically closed record. `kind_int()`, `kind_float()`,
`kind_bool()`, and `kind_string()` select exactly one of its `ints`, `floats`,
`bools`, or `strings` vectors. Every unselected vector must be empty. `validity`
has the same length as the selected vector, where `true` means present. Missing
slots use canonical storage fillers: `0`, `0.0`, `false`, or `""`.

`Table` owns a vector of uniquely named, nonempty-name columns and an explicit
nonnegative `row_count`. Every column must have exactly that many values. The
explicit count means a zero-column table or projection still represents rows.

Construct columns with `int_column`, `float_column`, `bool_column`, or
`string_column`, then use `add_column` to build a table created by `new_table`.
The `nullable` flag controls whether a false validity bit is legal. Constructors
canonicalize the backing value of a missing slot and reject missing values in a
non-nullable column.

Public records can be forged, so every public operation that accepts a `Column`
or `Table` validates it before indexing. `validate_column` and `validate_table`
provide non-raising checks; other failures raise a closed `TableError` variant.

`table_row_count` is the validated row-count accessor. The longer spelling is
required by the current Moss module namespace: a top-level `row_count` function
collides with the public `Table.row_count` constructor field.

## Ownership

Constructors copy input vectors. `clone`/`clone_table`, `add_column`, `remove_column`,
`replace_column`, `select_columns`, `select_rows`, `filter_mask`,
`drop_missing_rows`, and every `fill_missing_*` operation return independent
owned tables. `int_values`, `float_values`, `bool_values`, and `string_values`
return explicit owned values-plus-validity records. No API is a borrowed view or
copy-on-write value.

`select_rows` preserves caller index order and permits duplicate indices.
`filter_mask` requires exactly one mask entry per row. `drop_missing_rows`
removes a row when any column is missing; therefore all rows of a zero-column
table are retained. Filling a missing value sets its validity bit to true and
leaves the column's nullable schema flag unchanged.

## Complexity

Let `r` be row count, `c` column count, and `n` the number of selected output
rows. Column construction and typed access are `O(r)`. Validation is `O(c*r +
c^2)` because public records require complete storage checks and duplicate-name
validation. Name lookup is `O(c)`. Whole-table cloning and add/remove/replace are
`O(c*r)`. Column projection is `O(k*c + k*r)` for `k` requested columns. Row
selection is `O(c*n)`, while mask filtering and missing-row dropping are
`O(c*r)`. Missing-value fill is `O(c*r)` because the returned table owns all
column storage.

This package does not yet implement sorting, grouping, joins, aggregation, CSV,
or JSON adapters.
