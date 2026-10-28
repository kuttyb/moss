# Phase 26.2 inference signature completeness audit

Scope: the three non-final passes called in `Checker::run()` in this order:
`infer_domain_state_fields(false)`, `infer_function_signatures(false)`, and
`infer_handler_reply_types(false)`. “Captured” means represented in
`inferred_checker_signature()` and therefore compared at the cross-family
round boundary.

## Finding and criterion

All 16 written semantic members were already represented in
`inferred_checker_signature()` when `34082a83` introduced the early exit. The
early exit was sound as merged. A missing item is a defect only when it is
written, read by a later round, and able to change that round's semantic output.

The parse-cache dependency has now been audited and proven pure: each of the
five cache miss paths calls only a free parser or static helper, and those
helpers and their callees read only their arguments and local state. Cached
results therefore depend only on their keys, and `parse_cache_entries_` is not
semantic convergence state; it has been removed from the signature. The
signature dependency worklist is scheduling state: it determines what gets
revisited, not what is inferred; the
`MOSS_VERIFY_SIGNATURE_WORKLIST` check independently verifies its completeness.
The shared `trim` helper and the parser/checker token helpers now use explicit
ASCII classification, including whitespace (`space`, `\t`, `\n`, `\r`, `\f`,
`\v`). The core parser, semantic checker, and numeric-literal parser no longer
use locale-dependent ctype classification. Parser purity no longer depends on
the process locale.
`inferred_checker_signature()` is also used by inner convergence loops (around
`semantic_checker.cpp` lines 13564, 13923, and 14059). Default and verify-mode
results were identical for all 77 inputs in the verify gate. The parser purity
proof was checked at these implementation sites:

| Cache | Checker miss path | Pure implementation and transitive helpers | Verdict |
| --- | --- | --- | --- |
| Binary | [semantic_checker.cpp:10698](/home/owner/daji/code/moss/src/semantic_checker.cpp:10698) calls static `split_binary_uncached` at [semantic_checker.cpp:10709](/home/owner/daji/code/moss/src/semantic_checker.cpp:10709) | `split_binary_uncached` uses only its two arguments and local counters/iteration state. | Pure: key is `(expression, ordered operators)`. |
| Simple-call | [semantic_checker.cpp:10553](/home/owner/daji/code/moss/src/semantic_checker.cpp:10553) | [moss.cpp:1000](/home/owner/daji/code/moss/src/moss.cpp:1000) calls `trim` ([moss.cpp:118](/home/owner/daji/code/moss/src/moss.cpp:118), [moss.cpp:110](/home/owner/daji/code/moss/src/moss.cpp:110)) and `split_top_level` ([moss.cpp:214](/home/owner/daji/code/moss/src/moss.cpp:214)), whose only helper is `trim`. | Pure: key is `text`; output mutation is deterministic from `text`. |
| Member-call | [semantic_checker.cpp:10607](/home/owner/daji/code/moss/src/semantic_checker.cpp:10607) | [moss.cpp:1073](/home/owner/daji/code/moss/src/moss.cpp:1073) calls `trim` and `split_top_level` ([moss.cpp:214](/home/owner/daji/code/moss.cpp:214)); those use only arguments and local state. | Pure: key is `text`; output flags and mutations are deterministic from `text`. |
| Top-level split | [semantic_checker.cpp:10656](/home/owner/daji/code/moss/src/semantic_checker.cpp:10656) | [moss.cpp:214](/home/owner/daji/code/moss/src/moss.cpp:214) calls only `trim` ([moss.cpp:118](/home/owner/daji/code/moss/src/moss.cpp:118)); `trim` calls local-only `ltrim` ([moss.cpp:105](/home/owner/daji/code/moss/src/moss.cpp:105)) and `rtrim` ([moss.cpp:114](/home/owner/daji/code/moss/src/moss.cpp:114)). | Pure: key is `(text, delimiter)`. |
| Outer parentheses | [semantic_checker.cpp:10675](/home/owner/daji/code/moss/src/semantic_checker.cpp:10675) | [moss.cpp:1241](/home/owner/daji/code/moss/src/moss.cpp:1241) calls `trim` and `matching_paren` ([moss.cpp:1219](/home/owner/daji/code/moss/src/moss.cpp:1219)); both use only arguments and local state. | Pure: key is `text`. |

## Program inference state

| Written field/member | Write sites | Signature coverage |
| --- | --- | --- |
| `Function.params[].type`, `type_ref` | `parameter.set_type`: [semantic_checker.cpp:11686](/home/owner/daji/code/moss/src/semantic_checker.cpp:11686), [semantic_checker.cpp:13522](/home/owner/daji/code/moss/src/semantic_checker.cpp:13522); setter writes both fields at [ast.hpp:79](/home/owner/daji/code/moss/src/ast.hpp:79)–[ast.hpp:89](/home/owner/daji/code/moss/src/ast.hpp:89) | Yes: [semantic_checker.cpp:14150](/home/owner/daji/code/moss/src/semantic_checker.cpp:14150)–[semantic_checker.cpp:14155](/home/owner/daji/code/moss/src/semantic_checker.cpp:14155), called at [semantic_checker.cpp:14165](/home/owner/daji/code/moss/src/semantic_checker.cpp:14165). |
| `Function.return_type`, `return_type_ref` | `set_return_type` / `bind_return_type` calls at [semantic_checker.cpp:8513](/home/owner/daji/code/moss/src/semantic_checker.cpp:8513), [semantic_checker.cpp:8597](/home/owner/daji/code/moss/src/semantic_checker.cpp:8597), [semantic_checker.cpp:8615](/home/owner/daji/code/moss/src/semantic_checker.cpp:8615), [semantic_checker.cpp:8623](/home/owner/daji/code/moss/src/semantic_checker.cpp:8623), [semantic_checker.cpp:14558](/home/owner/daji/code/moss/src/semantic_checker.cpp:14558), [semantic_checker.cpp:14673](/home/owner/daji/code/moss/src/semantic_checker.cpp:14673), [semantic_checker.cpp:14840](/home/owner/daji/code/moss/src/semantic_checker.cpp:14840), [semantic_checker.cpp:14846](/home/owner/daji/code/moss/src/semantic_checker.cpp:14846), [semantic_checker.cpp:14855](/home/owner/daji/code/moss/src/semantic_checker.cpp:14855), [semantic_checker.cpp:14872](/home/owner/daji/code/moss/src/semantic_checker.cpp:14872); setters: [ast.hpp:590](/home/owner/daji/code/moss/src/ast.hpp:590), [ast.hpp:595](/home/owner/daji/code/moss/src/ast.hpp:595) | Yes: [semantic_checker.cpp:14166](/home/owner/daji/code/moss/src/semantic_checker.cpp:14166)–[semantic_checker.cpp:14168](/home/owner/daji/code/moss/src/semantic_checker.cpp:14168). |
| `Function.generic`, `static_dispatch` | Prelude and constraint derivation writes at [semantic_checker.cpp:8547](/home/owner/daji/code/moss/src/semantic_checker.cpp:8547)–[semantic_checker.cpp:8725](/home/owner/daji/code/moss/src/semantic_checker.cpp:8725) and [semantic_checker.cpp:14452](/home/owner/daji/code/moss/src/semantic_checker.cpp:14452)–[semantic_checker.cpp:14639](/home/owner/daji/code/moss/src/semantic_checker.cpp:14639) | Yes: [semantic_checker.cpp:14169](/home/owner/daji/code/moss/src/semantic_checker.cpp:14169)–[semantic_checker.cpp:14170](/home/owner/daji/code/moss/src/semantic_checker.cpp:14170). |
| `Function.generic_results`, `constraints` | Constraint writes: [semantic_checker.cpp:8506](/home/owner/daji/code/moss/src/semantic_checker.cpp:8506), [semantic_checker.cpp:8541](/home/owner/daji/code/moss/src/semantic_checker.cpp:8541), [semantic_checker.cpp:8564](/home/owner/daji/code/moss/src/semantic_checker.cpp:8564), [semantic_checker.cpp:8582](/home/owner/daji/code/moss/src/semantic_checker.cpp:8582), [semantic_checker.cpp:8676](/home/owner/daji/code/moss/src/semantic_checker.cpp:8676), [semantic_checker.cpp:8722](/home/owner/daji/code/moss/src/semantic_checker.cpp:8722), [semantic_checker.cpp:14572](/home/owner/daji/code/moss/src/semantic_checker.cpp:14572), [semantic_checker.cpp:14587](/home/owner/daji/code/moss/src/semantic_checker.cpp:14587), [semantic_checker.cpp:14608](/home/owner/daji/code/moss/src/semantic_checker.cpp:14608), [semantic_checker.cpp:14635](/home/owner/daji/code/moss/src/semantic_checker.cpp:14635); generic-result writes at [semantic_checker.cpp:14557](/home/owner/daji/code/moss/src/semantic_checker.cpp:14557), [semantic_checker.cpp:14588](/home/owner/daji/code/moss/src/semantic_checker.cpp:14588), [semantic_checker.cpp:14613](/home/owner/daji/code/moss/src/semantic_checker.cpp:14613) | Yes: sorted generic results at [semantic_checker.cpp:14171](/home/owner/daji/code/moss/src/semantic_checker.cpp:14171)–[semantic_checker.cpp:14177](/home/owner/daji/code/moss/src/semantic_checker.cpp:14177), constraints at [semantic_checker.cpp:14178](/home/owner/daji/code/moss/src/semantic_checker.cpp:14178)–[semantic_checker.cpp:14187](/home/owner/daji/code/moss/src/semantic_checker.cpp:14187). |
| `ObjectType.fields[].type` | Constructor constraints: [semantic_checker.cpp:11724](/home/owner/daji/code/moss/src/semantic_checker.cpp:11724) | Yes: shared projection at [semantic_checker.cpp:14297](/home/owner/daji/code/moss/src/semantic_checker.cpp:14297). |
| `Domain.state[].type`, `inferred` | Initializer inference: [semantic_checker.cpp:13943](/home/owner/daji/code/moss/src/semantic_checker.cpp:13943), [semantic_checker.cpp:13947](/home/owner/daji/code/moss/src/semantic_checker.cpp:13947); handler inference: [semantic_checker.cpp:14007](/home/owner/daji/code/moss/src/semantic_checker.cpp:14007)–[semantic_checker.cpp:14015](/home/owner/daji/code/moss/src/semantic_checker.cpp:14015) | Yes: [semantic_checker.cpp:14312](/home/owner/daji/code/moss/src/semantic_checker.cpp:14312)–[semantic_checker.cpp:14313](/home/owner/daji/code/moss/src/semantic_checker.cpp:14313). |
| `Handler.params[].type`, `type_ref`, `inferred` | Message/invoke inference: [semantic_checker.cpp:13472](/home/owner/daji/code/moss/src/semantic_checker.cpp:13472)–[semantic_checker.cpp:13473](/home/owner/daji/code/moss/src/semantic_checker.cpp:13473), [semantic_checker.cpp:13506](/home/owner/daji/code/moss/src/semantic_checker.cpp:13506)–[semantic_checker.cpp:13507](/home/owner/daji/code/moss/src/semantic_checker.cpp:13507); setter: [ast.hpp:79](/home/owner/daji/code/moss/src/ast.hpp:79) | Yes: [semantic_checker.cpp:14317](/home/owner/daji/code/moss/src/semantic_checker.cpp:14317), using the parameter projection at lines 14150–14155. |
| `Handler.reply_type`, `reply_type_ref` | [semantic_checker.cpp:14114](/home/owner/daji/code/moss/src/semantic_checker.cpp:14114); setter: [ast.hpp:469](/home/owner/daji/code/moss/src/ast.hpp:469) | Yes: [semantic_checker.cpp:14318](/home/owner/daji/code/moss/src/semantic_checker.cpp:14318)–[semantic_checker.cpp:14320](/home/owner/daji/code/moss/src/semantic_checker.cpp:14320). |

These are 16 distinct written semantic members, and all 16 are represented in
the signature. Representative append statements are:

```cpp
append(parameter.type);
append(parameter.type_ref ? parameter.type_ref->to_canonical_string() : string());
append(parameter.inferred ? "1" : "0");
```

## Persistent checker state

| Written state | Write / later-read sites | Signature coverage and result |
| --- | --- | --- |
| Binary, simple-call, member-call, top-level, and outer-parentheses parse caches | Insertions: [semantic_checker.cpp:10563](/home/owner/daji/code/moss/src/semantic_checker.cpp:10563), [semantic_checker.cpp:10623](/home/owner/daji/code/moss/src/semantic_checker.cpp:10623), [semantic_checker.cpp:10658](/home/owner/daji/code/moss/src/semantic_checker.cpp:10658), [semantic_checker.cpp:10677](/home/owner/daji/code/moss/src/semantic_checker.cpp:10677), [semantic_checker.cpp:10701](/home/owner/daji/code/moss/src/semantic_checker.cpp:10701); later lookups at [semantic_checker.cpp:10536](/home/owner/daji/code/moss/src/semantic_checker.cpp:10536), [semantic_checker.cpp:10584](/home/owner/daji/code/moss/src/semantic_checker.cpp:10584), [semantic_checker.cpp:10644](/home/owner/daji/code/moss/src/semantic_checker.cpp:10644), [semantic_checker.cpp:10666](/home/owner/daji/code/moss/src/semantic_checker.cpp:10666), [semantic_checker.cpp:10686](/home/owner/daji/code/moss/src/semantic_checker.cpp:10686) | **Proven pure; intentionally omitted from the signature.** The keys determine all cached results; see the purity table above. |
| `signature_function_indices_`, `signature_callers_by_function_`, `signature_callees_by_function_`, `signature_unresolved_consumers_` | Built/updated by [semantic_checker.cpp:18156](/home/owner/daji/code/moss/src/semantic_checker.cpp:18156)–[semantic_checker.cpp:18258](/home/owner/daji/code/moss/src/semantic_checker.cpp:18258); the old callee edges are read at [semantic_checker.cpp:14727](/home/owner/daji/code/moss/src/semantic_checker.cpp:14727)–[semantic_checker.cpp:14731](/home/owner/daji/code/moss/src/semantic_checker.cpp:14731), and the refreshed graph/consumer set is read at [semantic_checker.cpp:14935](/home/owner/daji/code/moss/src/semantic_checker.cpp:14935)–[semantic_checker.cpp:14940](/home/owner/daji/code/moss/src/semantic_checker.cpp:14940) | **Previously omitted; defect fixed.** Index mapping, caller/callee edges, and unresolved consumers are appended at [semantic_checker.cpp:14392](/home/owner/daji/code/moss/src/semantic_checker.cpp:14392)–[semantic_checker.cpp:14410](/home/owner/daji/code/moss/src/semantic_checker.cpp:14410). |
| `signature_dependency_graph_changed_` | Reset, set, and read at [semantic_checker.cpp:14723](/home/owner/daji/code/moss/src/semantic_checker.cpp:14723), [semantic_checker.cpp:14882](/home/owner/daji/code/moss/src/semantic_checker.cpp:14882), [semantic_checker.cpp:14927](/home/owner/daji/code/moss/src/semantic_checker.cpp:14927) | Not appended: it is reset before each inner worklist step and is not carried as an input to the next cross-family round. |
| Parse/profile counters, wave counts/ranges, and inference-pass round counters | Updated during the passes; emitted after checking at [semantic_checker.cpp:1725](/home/owner/daji/code/moss/src/semantic_checker.cpp:1725)–[semantic_checker.cpp:1826](/home/owner/daji/code/moss/src/semantic_checker.cpp:1826) | Not appended: they report work and are not read by a later inference round to determine inferred types or scheduling. |
| `current_source_file_`, `current_body_types_` | Scoped by `SourceFileScope` / `BodyTypeScope` around pass bodies, e.g. [semantic_checker.cpp:13938](/home/owner/daji/code/moss/src/semantic_checker.cpp:13938), [semantic_checker.cpp:14081](/home/owner/daji/code/moss/src/semantic_checker.cpp:14081) | Not appended: each scope restores the prior value before the pass returns; the next round establishes its own scope. |

The persistent parser cache and worklist state were added conservatively to the
convergence comparison; neither was an omitted semantic output dependency.
No semantic Program field remains outside the original signature.

## Backlog

- Confirm parse-cache purity: every input a cached parse result depends on
  must be represented in that cache's key.
- When cross-family rounds are replaced by a single worklist, shrink the
  convergence signature back to semantic state only.
- The Emacs xref fixture is flaky only under parallel `make check`; it passes
  in isolation.
