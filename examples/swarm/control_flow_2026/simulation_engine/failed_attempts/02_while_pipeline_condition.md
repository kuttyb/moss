# Unparenthesized pipeline in a `while` comparison

After replacing the native-broken `for` with a `while`, the natural condition
`while day < demands |> count:` failed the Moss checker with structured
`UNKNOWN_SYMBOL_OR_TYPE` at the condition and the message `unknown local
function 'count'`. The same issue is reduced in
`02_while_pipeline_condition.moss`.

This is a precedence/syntax-sugar omission, not evidence that vector cardinality
or `while` loops are unavailable: the live source surface documents `vec |> count`.
The narrow repair is to parenthesize the pipeline before comparing it.
