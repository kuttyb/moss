# Phase 27.3 widened-lint exemptions

Each entry is keyed by exact source path, line, and reader API. If code movement invalidates a key, the lint fails closed until the exemption is reviewed again.

- `src/semantic_checker.cpp:102 | split_top_level` — Splits canonical type argument spellings inside `phase20_is_nontransferable_capability`; this is type syntax, not expression text.
- `src/semantic_checker.cpp:8248 | split_top_level` — Splits canonical type argument spellings inside `builtin_equality_type`; this is type syntax, not expression text.
- `src/semantic_checker.cpp:8368 | split_top_level` — Splits canonical type argument spellings inside `contains_float_type`; this is type syntax, not expression text.
- `src/semantic_checker.cpp:12411 | split_top_level` — Splits comma-separated match-arm pattern alternatives; EST has no match-pattern node.
- `src/semantic_checker.cpp:16832 | split_top_level` — Splits comma-separated match-arm pattern alternatives; EST has no match-pattern node.
- `src/semantic_checker.cpp:17857 | split_top_level` — Splits comma-separated match-arm pattern alternatives; EST has no match-pattern node.
- `src/semantic_checker.cpp:19085 | split_top_level` — Splits comma-separated recovery selector patterns; EST has no match-pattern node.
- `src/semantic_checker.cpp:19196 | split_top_level` — Splits comma-separated match-arm pattern alternatives; EST has no match-pattern node.
- `src/semantic_checker.cpp:22220 | split_top_level` — Splits comma-separated match-arm pattern alternatives; EST has no match-pattern node.
