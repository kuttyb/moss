# Phase 21 external peer-review Float counterexamples B and F

The `p21_open_at_7775049.zip` archive was not available in this checkout.
These sources reconstruct the two counterexamples from the L7 addendum;
they are not claimed to be byte-identical to the external reviewer's files.

- `computed_float_output.moss` (B / EXT-12) must print
  `-2.0 -1.0 -2.0 -2.0 -1.0 -0.0 -1e16`, one value per line.
- `sqrt_float_locals.moss` (F / EXT-12) must print
  `4.0 4.0 1.4142135623730951` on one line.
- `sqrt_float_forms.moss` supplements F with direct Float literals and an
  inferred helper; it must print `4.0 3.0`.

Both must pass `moss check`, native O0, optimized native, and Fast Debug.

At `7775049`, both pass `moss check --json`. B prints `-2`, `-1`,
`-2.0`, `-2`, `-1.0`, `-0.0`, `-1e16` under native O0 and O3, while Fast Debug
prints the expected canonical forms. F fails rustc E0689 in both native modes;
Fast Debug prints the expected line. The `sqrt_typed` effects query at that
checkpoint reports an empty raise set and no panic effect, as expected.
