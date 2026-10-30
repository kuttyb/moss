# Phase 27.3 widened-lint exemptions

Each entry is keyed by source path, enclosing function, and reader API. The lint fails closed if the function disappears or if it contains more than one site for that API.

- `src/semantic_checker.cpp | phase20_is_nontransferable_capability | split_top_level` — Splits canonical type argument spellings; this is type syntax, not expression or pattern text.
- `src/semantic_checker.cpp | builtin_equality_type | split_top_level` — Splits canonical type argument spellings; this is type syntax, not expression or pattern text.
- `src/semantic_checker.cpp | contains_float_type | split_top_level` — Splits canonical type argument spellings; this is type syntax, not expression or pattern text.
