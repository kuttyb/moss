#!/usr/bin/env python3
"""Run deterministic independent ndarray and table semantic oracles."""

import os
from pathlib import Path
import random
import subprocess
import tempfile


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MARGO = REPO / "margo"
SCRATCH = REPO / "tmp"
SEED = 240024


def moss_bool(value: bool) -> str:
    return "true" if value else "false"


def moss_value(value) -> str:
    if isinstance(value, bool):
        return moss_bool(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, str):
        return f'"{value}"'
    return str(value)


def moss_vector(values) -> str:
    return "[" + ", ".join(moss_value(value) for value in values) + "]"


def product(shape: list[int]) -> int:
    result = 1
    for extent in shape:
        result *= extent
    return result


def strides(shape: list[int]) -> list[int]:
    result = [1] * len(shape)
    running = 1
    for axis in range(len(shape) - 1, -1, -1):
        result[axis] = running
        running *= shape[axis]
    return result


def coordinates(shape: list[int]):
    for flat in range(product(shape)):
        remainder = flat
        coord = []
        for stride, extent in zip(strides(shape), shape):
            coord.append((remainder // stride) % extent)
        yield coord


def at(data: list[float], shape: list[int], coord: list[int]) -> float:
    return data[sum(index * stride for index, stride in zip(coord, strides(shape)))]


def broadcast_add(
    left: list[float], left_shape: list[int], right: list[float], right_shape: list[int]
) -> list[float]:
    offset = len(left_shape) - len(right_shape)
    result = []
    for coord in coordinates(left_shape):
        right_coord = []
        for axis, extent in enumerate(right_shape):
            source = coord[offset + axis]
            right_coord.append(0 if extent == 1 else source)
        result.append(at(left, left_shape, coord) + at(right, right_shape, right_coord))
    return result


def reduce_axis(data: list[float], shape: list[int], axis: int, operation: str):
    output_shape = shape[:axis] + shape[axis + 1 :]
    output = []
    for output_coord in coordinates(output_shape):
        lane = []
        for selected in range(shape[axis]):
            source = output_coord[:axis] + [selected] + output_coord[axis:]
            lane.append(at(data, shape, source))
        if operation == "sum":
            output.append(sum(lane))
        elif operation == "min":
            output.append(min(lane))
        elif operation == "max":
            output.append(max(lane))
        else:
            output.append(sum(lane) / len(lane))
    return output_shape, output


def sliced(
    data: list[float],
    shape: list[int],
    starts: list[int],
    stops: list[int],
    steps: list[int],
):
    ranges = [list(range(start, stop, step)) for start, stop, step in zip(starts, stops, steps)]
    output_shape = [len(values) for values in ranges]
    output = []
    for coord in coordinates(output_shape):
        source = [ranges[axis][index] for axis, index in enumerate(coord)]
        output.append(at(data, shape, source))
    return output_shape, output


def permuted(data: list[float], shape: list[int], axes: list[int]):
    output_shape = [shape[axis] for axis in axes]
    output = []
    for coord in coordinates(output_shape):
        source = [0] * len(shape)
        for output_axis, source_axis in enumerate(axes):
            source[source_axis] = coord[output_axis]
        output.append(at(data, shape, source))
    return output_shape, output


def table_reference():
    row_ids = list(range(8))
    keys = [2, 1, 0, 3, 1, 2, 3, 0]
    key_valid = [True, True, False, True, True, True, True, False]
    values = [8, 3, 9, 4, 7, 2, 6, 5]
    value_valid = [True, False, True, True, True, True, False, True]
    labels = ["b", "a", "c", "b", "a", "c", "b", "c"]

    sorted_rows = sorted(
        row_ids,
        key=lambda row: (not key_valid[row], keys[row] if key_valid[row] else 0),
    )

    distinct_rows = []
    seen = set()
    for row in row_ids:
        semantic_key = keys[row] if key_valid[row] else None
        if semantic_key not in seen:
            seen.add(semantic_key)
            distinct_rows.append(row)

    group_keys = []
    group_validity = []
    group_rows = []
    positions = {}
    for row in row_ids:
        semantic_key = keys[row] if key_valid[row] else None
        if semantic_key not in positions:
            positions[semantic_key] = len(group_rows)
            group_keys.append(keys[row] if key_valid[row] else 0)
            group_validity.append(key_valid[row])
            group_rows.append([])
        group_rows[positions[semantic_key]].append(row)

    counts = [len(rows) for rows in group_rows]
    present_counts = [sum(value_valid[row] for row in rows) for rows in group_rows]
    sums = []
    sum_validity = []
    means = []
    extremes_min = []
    extremes_max = []
    for rows in group_rows:
        present = [values[row] for row in rows if value_valid[row]]
        sum_validity.append(bool(present))
        sums.append(sum(present) if present else 0)
        means.append(float(sum(present)) / len(present) if present else 0.0)
        extremes_min.append(min(present) if present else 0)
        extremes_max.append(max(present) if present else 0)

    categories = ["c", "b", "a"]
    ranks = {value: index for index, value in enumerate(categories)}
    category_rows = sorted(row_ids, key=lambda row: ranks[labels[row]])

    right_keys = [1, 1, 2, 4]
    weights = [10, 11, 20, 40]
    inner_left_rows = []
    inner_weights = []
    left_join_rows = []
    left_join_weights = []
    left_join_validity = []
    for row in row_ids:
        matches = []
        if key_valid[row]:
            matches = [index for index, key in enumerate(right_keys) if key == keys[row]]
        for right_row in matches:
            inner_left_rows.append(row)
            inner_weights.append(weights[right_row])
            left_join_rows.append(row)
            left_join_weights.append(weights[right_row])
            left_join_validity.append(True)
        if not matches:
            left_join_rows.append(row)
            left_join_weights.append(0)
            left_join_validity.append(False)

    string_keys = []
    string_counts = []
    for label in labels:
        if label in string_keys:
            string_counts[string_keys.index(label)] += 1
        else:
            string_keys.append(label)
            string_counts.append(1)

    return locals()


def generated_source() -> str:
    rng = random.Random(SEED)
    shape = [2, 3, 4]
    data = [float(rng.randint(-6, 9)) for _ in range(product(shape))]
    rhs_shape = [1, 3, 1]
    rhs = [float(rng.randint(-3, 4)) for _ in range(product(rhs_shape))]
    added = broadcast_add(data, shape, rhs, rhs_shape)
    sum_shape, sums = reduce_axis(data, shape, 2, "sum")
    _, minima = reduce_axis(data, shape, 2, "min")
    _, maxima = reduce_axis(data, shape, 2, "max")
    _, means = reduce_axis(data, shape, 2, "mean")
    starts, stops, steps = [0, 0, 1], [2, 3, 4], [1, 2, 2]
    slice_shape, slice_values = sliced(data, shape, starts, stops, steps)
    axes = [2, 0, 1]
    permuted_shape, permuted_values = permuted(data, shape, axes)
    table = table_reference()

    lines = [
        "module advanced_data_semantic_oracle",
        "",
        "import linalg",
        "import ndarray",
        "import tables",
        "",
        "fn main():",
        "  try:",
        f"    source = ndarray.from_flat({moss_vector(shape)}, {moss_vector(data)})",
        f"    rhs = ndarray.from_flat({moss_vector(rhs_shape)}, {moss_vector(rhs)})",
        f"    assertEqual(ndarray.flatten(ndarray.add(source, rhs)), {moss_vector(added)})",
        f"    assertEqual(ndarray.shape_copy(ndarray.sum_axis(source, 2)), {moss_vector(sum_shape)})",
        f"    assertEqual(ndarray.flatten(ndarray.sum_axis(source, 2)), {moss_vector(sums)})",
        f"    assertEqual(ndarray.flatten(ndarray.min_axis(source, 2)), {moss_vector(minima)})",
        f"    assertEqual(ndarray.flatten(ndarray.max_axis(source, 2)), {moss_vector(maxima)})",
        f"    assertEqual(ndarray.flatten(ndarray.mean_axis(source, 2)), {moss_vector(means)})",
        f"    sliced = ndarray.slice_copy(source, {moss_vector(starts)}, {moss_vector(stops)}, {moss_vector(steps)})",
        f"    assertEqual(ndarray.shape_copy(sliced), {moss_vector(slice_shape)})",
        f"    assertEqual(ndarray.flatten(sliced), {moss_vector(slice_values)})",
        f"    permuted = ndarray.permute_axes(source, {moss_vector(axes)})",
        f"    assertEqual(ndarray.shape_copy(permuted), {moss_vector(permuted_shape)})",
        f"    assertEqual(ndarray.flatten(permuted), {moss_vector(permuted_values)})",
        "    matrix = ndarray.to_matrix(ndarray.sum_axis(source, 2))",
        f"    assertEqual(ndarray.flatten(ndarray.from_matrix(matrix)), {moss_vector(sums)})",
        "",
        "    table0 = tables.new_table(8)",
        f"    table1 = tables.add_column(table0, tables.int_column(\"row\", false, {moss_vector(table['row_ids'])}, {moss_vector([True] * 8)}))",
        f"    table2 = tables.add_column(table1, tables.int_column(\"key\", true, {moss_vector(table['keys'])}, {moss_vector(table['key_valid'])}))",
        f"    table3 = tables.add_column(table2, tables.int_column(\"value\", true, {moss_vector(table['values'])}, {moss_vector(table['value_valid'])}))",
        f"    table = tables.add_column(table3, tables.string_column(\"label\", false, {moss_vector(table['labels'])}, {moss_vector([True] * 8)}))",
        f"    assertEqual(tables.int_values(tables.sort_by(table, \"key\", true), \"row\").values, {moss_vector(table['sorted_rows'])})",
        f"    assertEqual(tables.int_values(tables.distinct_int(table, \"key\"), \"row\").values, {moss_vector(table['distinct_rows'])})",
        "    groups = tables.group_by_int(table, \"key\")",
        f"    assertEqual(tables.group_int_keys(groups).values, {moss_vector(table['group_keys'])})",
        f"    assertEqual(tables.group_int_keys(groups).validity, {moss_vector(table['group_validity'])})",
        f"    assertEqual(tables.group_count_rows(groups), {moss_vector(table['counts'])})",
        f"    assertEqual(tables.group_count_present(table, groups, \"value\"), {moss_vector(table['present_counts'])})",
        f"    assertEqual(tables.group_sum_int(table, groups, \"value\").values, {moss_vector(table['sums'])})",
        f"    assertEqual(tables.group_sum_int(table, groups, \"value\").validity, {moss_vector(table['sum_validity'])})",
        f"    assertEqual(tables.group_mean_int(table, groups, \"value\").values, {moss_vector(table['means'])})",
        f"    assertEqual(tables.group_min_int(table, groups, \"value\").values, {moss_vector(table['extremes_min'])})",
        f"    assertEqual(tables.group_max_int(table, groups, \"value\").values, {moss_vector(table['extremes_max'])})",
        "    string_groups = tables.group_by_string(table, \"label\")",
        f"    assertEqual(tables.group_string_keys(string_groups).values, {moss_vector(table['string_keys'])})",
        f"    assertEqual(tables.group_count_rows(string_groups), {moss_vector(table['string_counts'])})",
        f"    category_sorted = tables.sort_string_by_order(table, \"label\", {moss_vector(table['categories'])}, true)",
        f"    assertEqual(tables.int_values(category_sorted, \"row\").values, {moss_vector(table['category_rows'])})",
        "    right0 = tables.new_table(4)",
        f"    right1 = tables.add_column(right0, tables.int_column(\"right_key\", false, {moss_vector(table['right_keys'])}, {moss_vector([True] * 4)}))",
        f"    right = tables.add_column(right1, tables.int_column(\"weight\", false, {moss_vector(table['weights'])}, {moss_vector([True] * 4)}))",
        "    inner = tables.inner_join_int(table, right, \"key\", \"right_key\", \"_right\", 32)",
        f"    assertEqual(tables.int_values(inner, \"row\").values, {moss_vector(table['inner_left_rows'])})",
        f"    assertEqual(tables.int_values(inner, \"weight\").values, {moss_vector(table['inner_weights'])})",
        "    left = tables.left_join_int(table, right, \"key\", \"right_key\", \"_right\", 32)",
        f"    assertEqual(tables.int_values(left, \"row\").values, {moss_vector(table['left_join_rows'])})",
        f"    assertEqual(tables.int_values(left, \"weight\").values, {moss_vector(table['left_join_weights'])})",
        f"    assertEqual(tables.int_values(left, \"weight\").validity, {moss_vector(table['left_join_validity'])})",
        f"    echo \"ndarray/table semantic oracle passed: seed {SEED}\"",
        "  recover:",
        "    assert(false)",
        "",
    ]
    return "\n".join(lines)


def run(command: list[str], cwd: Path, env: dict[str, str]) -> None:
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
    assert result.returncode == 0, (
        f"oracle command failed: {' '.join(command)}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    expected = f"ndarray/table semantic oracle passed: seed {SEED}\n"
    assert result.stdout == expected, result.stdout


def main() -> None:
    SCRATCH.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(
        MOSS=str(REPO / "moss"),
        MARGO_HOME=str(SCRATCH / "margo-advanced-data-semantic-oracle"),
        TMPDIR=str(SCRATCH),
        PYTHONDONTWRITEBYTECODE="1",
    )
    with tempfile.TemporaryDirectory(
        prefix="advanced-data-semantic-oracle-", dir=SCRATCH
    ) as temporary:
        oracle = Path(temporary)
        (oracle / "src").mkdir()
        (oracle / "Moss.toml").write_text(
            '[package]\nname = "advanced-data-semantic-oracle"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n\n'
            f'[dependencies]\n"moss.ndarray" = {{ path = "{REPO / "sdk" / "ndarray"}" }}\n'
            f'"moss.tables" = {{ path = "{REPO / "sdk" / "tables"}" }}\n'
            f'"moss.linalg" = {{ path = "{REPO / "packages" / "linalg"}" }}\n',
            encoding="utf-8",
        )
        (oracle / "src" / "main.moss").write_text(
            generated_source(), encoding="utf-8"
        )
        run([str(MARGO), "run"], oracle, env)
        run([str(MARGO), "--release", "run"], oracle, env)
        run([str(MARGO), "debug"], oracle, env)
    print(
        f"Independent ndarray/table semantic oracle passed in three modes: seed {SEED}"
    )


if __name__ == "__main__":
    main()
