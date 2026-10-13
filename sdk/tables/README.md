# moss.tables

`moss.tables` is the Phase 24 typed columnar table and relational-operations
package. It intentionally has no package dependencies. CSV adapters remain a
separate later workstream.

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

## Filtering and ordering

Typed filters are `filter_int_equal`, `filter_int_range`,
`filter_float_equal`, `filter_float_range`, `filter_bool_equal`, and
`filter_string_equal`. Ranges are inclusive. Missing values never match a typed
predicate; use `filter_missing` or `filter_present` explicitly. Every filter is
stable and returns an owned table.

`sort_by` is a stable bottom-up mergesort for Int, Float, and Bool columns.
Missing values are always last in both directions. Float NaNs follow all
non-NaN values and precede missing values in both directions. Equal values,
including signed zero and NaNs, retain input order. Native String ordering is
not defined by Moss v0.1, so String `sort_by` raises
`TableError.UnsupportedOrdering`. `sort_string_by_order` provides explicit
categorical ordering; duplicate categories and present values absent from the
category list are typed errors.

`distinct_int` and `distinct_string` retain the first row for every key. All
missing keys form one distinct value at their first occurrence.

## Grouping and aggregation

`group_by_int` and `group_by_string` return a validated `GroupIndex` whose
groups occur in first-seen order and whose linked rows remain in source order.
All missing keys form one group. `group_count_rows` counts every linked row;
`group_count_present` counts present values in any named column.

Typed numeric aggregate functions provide Int and Float sum, mean, minimum, and
maximum. Missing inputs are ignored. An all-missing group has a canonical zero
payload and a false validity bit. Int sum raises `TableError.IntegerOverflow`
instead of wrapping. Int mean returns `FloatColumnValues`; Float aggregates
preserve IEEE-754 NaN propagation.

Because `GroupIndex` is a public record, `validate_group_index` checks storage
lengths, key uniqueness, the single missing-key group, exact row ownership,
link bounds, cycles, and source-row coverage before public consumers traverse
it. Aggregation also requires the table row count to match the grouped source.

## Joins

`inner_join_int`, `left_join_int`, `inner_join_string`, and
`left_join_string` retain all left columns and append every right column except
the right key. Output order is stable left-row then right-row order. Duplicate
keys produce the full Cartesian set of matches. Missing keys never match,
including missing-to-missing. A left join emits one row with missing right-side
values for each unmatched left row and marks appended right columns nullable.

When an appended right name collides with a left name, the supplied
`right_suffix` is required. The suffixed result must itself be unique; otherwise
the join raises `DuplicateColumnName`. `max_output_rows` is checked before each
row pair is appended. Negative limits and exceeded limits are distinct typed
errors.

## Complexity

Let `r` be row count, `c` column count, and `n` the number of selected output
rows. Column construction and typed access are `O(r)`. Validation is `O(c*r +
c^2)` because public records require complete storage checks and duplicate-name
validation. Name lookup is `O(c)`. Whole-table cloning and add/remove/replace
are `O(c*r)`. Column projection is `O(k*c + k*r)` for `k` requested columns.
Row selection is `O(c*n)`, while mask and typed filtering are `O(c*r)` because
the returned table owns every selected column. Missing-value fill is `O(c*r)`.

Sort is `O(r log r + c*r)` time and `O(r + c*r)` owned output space. Distinct
is expected `O(r + c*n)` time from typed hash maps. Group construction is
expected `O(r)` plus public validation; validating a forged `GroupIndex` is
`O(r + g^2)` because it proves key uniqueness without trusting map state.
Aggregates are `O(r + g^2 + c*r)` including table/group validation and the
owned column snapshot. For fixed schemas, joins are expected `O(left + right +
output)` for matching, followed by `O(c*output)` owned result construction;
schema validation and collision checks add `O(c^2)`. No API claims borrowed or
zero-copy output.

CSV and JSON adapters are not implemented by this workstream.
