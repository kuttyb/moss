#!/usr/bin/env python3
"""Phase 28 Independent Semantic Oracle Suite Harness.

Enforces the frozen Phase 28 ownership proposal (§10 and §12) across:
1. Checker semantic verification
2. Fast Debug interpreted execution
3. Native Debug compilation and execution
4. Native Optimized (-O) compilation and execution
5. Hidden clone budget audit (zero hidden clones for borrowed/moved places)
6. Exact rejection diagnostics for invalid programs

Specification Authority:
docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md (§10 and §12)
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"
DEFAULT_TMP = ROOT / "tmp/phase28_oracle"

# Complete fixture specification definitions per Proposal §10 & §12
ORACLE_FIXTURES = [
    # ─── NO-GO Clean-Room Review Concrete Failures (Items 1-10) ───────────────
    {
        "id": "ORACLE-01-VEC-WRITE-THROUGH",
        "name": "Write-through borrowed Vector elements",
        "file": "oracle_01_write_through_vector.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §10 row 'b = v[0]; b.cost = 9', §1.2 ('One owner, temporary borrows'), §5.3",
        "kind": "ACCEPTED",
        "expected_fast_debug": "9\n9",
        "expected_native": "9\n9",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Mutating a borrowed vector element writes through to the underlying vector; expected 9,9."
    },
    {
        "id": "ORACLE-02-INDEXED-FIELD-READ-NO-CLONE",
        "name": "Indexed field reads without hidden clones",
        "file": "oracle_02_indexed_field_read_no_clone.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §1.2, §1.3 ('Layout cannot change ownership'), §10, §12 ('Zero hidden clones')",
        "kind": "ACCEPTED",
        "expected_fast_debug": "hello",
        "expected_native": "hello",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Reading an aggregate field through an index projection must not clone the entire element."
    },
    {
        "id": "ORACLE-03-COLL-ELEM-CONSUME-REJECT",
        "name": "Rejection of consuming borrowed collection elements",
        "file": "oracle_03_collection_element_consume_rejected.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §10 row 'nxt.push(head[0])', §5.3, §12",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_COLLECTION_ELEMENT_CONSUME",
        "alternative_diagnostics": ["OWNERSHIP_ILLEGAL_CONSUME", "ILLEGAL_CONSUME_FROM_BORROWED"],
        "description": "Collection elements are borrowed; consuming them without remove/pop or explicit copy must be rejected."
    },
    {
        "id": "ORACLE-04-MAP-WRITE-THROUGH",
        "name": "Write-through borrowed Map entries",
        "file": "oracle_04_write_through_map_entry.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §10, §5.3 ('Collection ownership APIs'), §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "9\n9",
        "expected_native": "9\n9",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Mutating a borrowed map entry writes through to the map; expected 9,9."
    },
    {
        "id": "ORACLE-05-MAP-GET-BORROW",
        "name": "Borrowed Map.get(key) semantics",
        "file": "oracle_05_map_get_borrowed.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §5.3 ('Map.get(key): 1-argument borrowed lookup returning option[V]'), §10, §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "active",
        "expected_native": "active",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "1-argument Map.get(key) returns a borrowed option[V] without cloning map entries."
    },
    {
        "id": "ORACLE-06-MATCH-CONSUME-OPT-NATIVE",
        "name": "Native compilation and execution of match consume on options",
        "file": "oracle_06_match_consume_option_native.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §6.2 ('match consume: transfer payloads from an owned enum'), §10, §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "target",
        "expected_native": "target",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "match consume on option[T] must compile cleanly to rustc and execute without unlinked crate errors."
    },
    {
        "id": "ORACLE-07-CATCHABLE-COLL-ERRORS",
        "name": "Catchable errors for missing map keys and invalid removals",
        "file": "oracle_07_catchable_collection_errors.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §5.3, §7.2 ('Exception edges are effect-precise'), §12; Phase 21 spec §13",
        "kind": "ACCEPTED",
        "expected_fast_debug": "caught_vec_index\ncaught_map_key",
        "expected_native": "caught_vec_index\ncaught_map_key",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Out-of-bounds removal and missing key access raise catchable errors rather than uncatchable panics/SIGABRT."
    },
    {
        "id": "ORACLE-08-USER-COPY-RESOLUTION",
        "name": "Correct resolution of user-defined copy functions versus the built-in",
        "file": "oracle_08_user_defined_copy_resolution.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §10 row 'copy(v[0])', §11 Phase D ('explicit copy(x) only'), §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "105",
        "expected_native": "105",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "A user-defined function named copy must resolve to the user function, not the built-in intrinsic."
    },
    {
        "id": "ORACLE-09-USER-ENUM-SOME-NONE",
        "name": "User-defined enum cases named Some/None",
        "file": "oracle_09_user_enum_some_none.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §6 ('Applying Rule 4 to enums'), §10, §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "42",
        "expected_native": "42",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "User-defined enums with variant names Some or None must not collide with the built-in option type."
    },
    {
        "id": "ORACLE-10A-EXPLICIT-COPY-ONLY",
        "name": "Explicit-copy-only duplication",
        "file": "oracle_10a_explicit_copy_only.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §10 row 'copy(v[0]) for an eligible value', §11 Phase D",
        "kind": "ACCEPTED",
        "expected_fast_debug": "init\ninit",
        "expected_native": "init\ninit",
        "authorized_explicit_copies": 1,
        "max_hidden_clones": 0,
        "description": "copy(x) explicitly duplicates nonprimitive values and produces an independent copy."
    },
    {
        "id": "ORACLE-10B-CLONE-ALIAS-REJECTED",
        "name": "Rejection of source .clone() alias",
        "file": "oracle_10b_clone_alias_rejected.moss",
        "category": "NO-GO Review",
        "spec_citation": "Proposal §10 row 'v[0].clone() as a source-language duplication alias', §11 Phase D",
        "kind": "REJECTED",
        "expected_diagnostic": "CLONE_ALIAS_UNSUPPORTED",
        "alternative_diagnostics": ["UNKNOWN_MEMBER_FUNCTION"],
        "description": "Source-level .clone() is rejected; copy(x) is the sole explicit duplication syntax."
    },

    # ─── Assignment and Local Inference (§10) ─────────────────────────────────
    {
        "id": "ORACLE-11-ASSIGN-READ-BORROW",
        "name": "Assignment inference: READ borrow",
        "file": "oracle_11_assignment_read_borrow.moss",
        "category": "Assignment & Locals",
        "spec_citation": "Proposal §10 row 'b = a; echo b; echo a', §3",
        "kind": "ACCEPTED",
        "expected_fast_debug": "10\n10",
        "expected_native": "10\n10",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Local assignment without mutation infers a READ borrow; both bindings remain accessible."
    },
    {
        "id": "ORACLE-12-ASSIGN-WRITE-BORROW",
        "name": "Assignment inference: WRITE borrow",
        "file": "oracle_12_assignment_write_borrow.moss",
        "category": "Assignment & Locals",
        "spec_citation": "Proposal §10 row 'b = a; b.field = 1', §3",
        "kind": "ACCEPTED",
        "expected_fast_debug": "1\n1",
        "expected_native": "1\n1",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Local assignment with subsequent mutation infers a WRITE borrow on writable storage."
    },
    {
        "id": "ORACLE-13-ASSIGN-CONSUME-RETURN",
        "name": "Assignment inference: CONSUME return",
        "file": "oracle_13_assignment_consume_return.moss",
        "category": "Assignment & Locals",
        "spec_citation": "Proposal §10 row 'b = a; return b', §3",
        "kind": "ACCEPTED",
        "expected_fast_debug": "hello",
        "expected_native": "hello",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Returning a local infers a CONSUME demand, satisfied when source is owned and movable."
    },
    {
        "id": "ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT",
        "name": "Assignment inference: CONSUME from borrowed place rejected",
        "file": "oracle_14_assignment_consume_borrowed_rejected.moss",
        "category": "Assignment & Locals",
        "spec_citation": "Proposal §10 row 'b = a; return b' (reject borrowed non-movable place), §3",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_ILLEGAL_CONSUME",
        "alternative_diagnostics": ["OWNERSHIP_COLLECTION_ELEMENT_CONSUME", "ILLEGAL_MOVE_BORROWED"],
        "description": "CONSUME demand cannot be satisfied from a borrowed collection element."
    },
    {
        "id": "ORACLE-15-PLAIN-FIELD-CONSUME-REJECT",
        "name": "Plain field access cannot implicitly empty field",
        "file": "oracle_15_plain_field_access_consume_rejected.moss",
        "category": "Assignment & Locals",
        "spec_citation": "Proposal §10 row 'b = state.frontier; archive(b)', §5.2",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_ILLEGAL_CONSUME",
        "alternative_diagnostics": ["CANNOT_IMPLICITLY_EMPTY_FIELD", "TAKE_REQUIRED_FOR_FIELD_MOVE"],
        "description": "Accessing a field without take cannot implicitly empty it when passed to a consuming callee."
    },

    # ─── Take Moves and Restrictions (§10) ────────────────────────────────────
    {
        "id": "ORACLE-16-TAKE-FRONTIER-ARCHIVE",
        "name": "Explicit whole-field take",
        "file": "oracle_16_take_frontier_archive.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'b = take(state.frontier); archive(b)', §5.1, §5.2",
        "kind": "ACCEPTED",
        "expected_fast_debug": "3\n0",
        "expected_native": "3\n0",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "take(state.frontier) extracts the vector field and leaves an empty vector in place."
    },
    {
        "id": "ORACLE-17-TAKE-OPTION-FIELD",
        "name": "Whole option field take",
        "file": "oracle_17_take_option_field.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'b = take(job.pending)', §5.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "42\n999",
        "expected_native": "42\n999",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "take(job.pending) moves the option value and leaves None in place."
    },
    {
        "id": "ORACLE-18-TAKE-QUEUE-FIELD",
        "name": "Whole Queue field take",
        "file": "oracle_18_take_queue_field.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(state.queue)', §5.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "10\n100",
        "expected_native": "10\n100",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "take(state.queue) moves the queue and leaves an empty queue in place."
    },
    {
        "id": "ORACLE-19-TAKE-NESTED-VEC-OPTION",
        "name": "Nested take(v[i].pending)",
        "file": "oracle_19_take_nested_vector_option.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(v[i].pending)', §5.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "task\n1\ncleared",
        "expected_native": "task\n1\ncleared",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Takes eligible terminal option field; the containing element v[i] stays in place."
    },
    {
        "id": "ORACLE-20-TAKE-NESTED-MAP-VECTOR",
        "name": "Nested take(m[k].items)",
        "file": "oracle_20_take_nested_map_vector.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(m[k].items)', §5.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "2\nb1\n0",
        "expected_native": "2\nb1\n0",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Takes eligible terminal Vector field; entry m[k] stays in place with empty vector."
    },
    {
        "id": "ORACLE-21-TAKE-LOCAL-REJECTED",
        "name": "take(local) rejected",
        "file": "oracle_21_take_local_rejected.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(local)', §5.1",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_REQUIRES_FIELD_PLACE",
        "alternative_diagnostics": ["TAKE_INVALID_TARGET"],
        "description": "take is for surviving eligible fields; local moves are inferred per definition."
    },
    {
        "id": "ORACLE-22-TAKE-UNIFIED-FIELD-REJECT",
        "name": "take(job.report) unified field rejected",
        "file": "oracle_22_take_unified_field_rejected.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(job.report)', §5.1, §9",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_INELIGIBLE_TERMINAL",
        "alternative_diagnostics": ["TAKE_REQUIRES_FIELD_PLACE", "UNIFIED_FIELD_EXTRACT_REJECTED"],
        "description": "Unified record fields cannot be extracted with take."
    },
    {
        "id": "ORACLE-23-TAKE-VEC-ELEM-REJECTED",
        "name": "take(v[0]) collection element rejected",
        "file": "oracle_23_take_vector_element_rejected.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(v[0])', §5.1",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_REQUIRES_FIELD_PLACE",
        "alternative_diagnostics": ["COLLECTION_ELEMENT_TAKE_REJECTED"],
        "description": "Collection elements cannot be extracted with take; use remove/pop instead."
    },
    {
        "id": "ORACLE-24-TAKE-CUSTOM-SLOT-REJECT",
        "name": "take(my_array.slots[i]) custom slots rejected",
        "file": "oracle_24_take_custom_slot_rejected.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'take(my_array.slots[i])', §9",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_REQUIRES_FIELD_PLACE",
        "alternative_diagnostics": ["TAKE_INELIGIBLE_TERMINAL"],
        "description": "Custom array slots are not in the closed set of take-eligible terminal field types."
    },
    {
        "id": "ORACLE-25-TAKE-BEFORE-RAISE-MUTATION",
        "name": "take followed by raise and partial mutation",
        "file": "oracle_25_take_before_raise_no_rollback.moss",
        "category": "Take Moves & Restrictions",
        "spec_citation": "Proposal §10 row 'b = take(state.frontier); may_raise(); use(b)', §8, §9",
        "kind": "ACCEPTED",
        "expected_fast_debug": "0",
        "expected_native": "0",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Explicit destructive take is not transactional; state remains emptied on typed raise."
    },

    # ─── Rebinding and Loops (§10) ────────────────────────────────────────────
    {
        "id": "ORACLE-26-VEC-REMOVE-API",
        "name": "Vector remove API transfers ownership",
        "file": "oracle_26_vector_remove_api.moss",
        "category": "Rebinding & Loops",
        "spec_citation": "Proposal §10 row 'nxt.push(head.remove(0))', §5.3",
        "kind": "ACCEPTED",
        "expected_fast_debug": "alpha\n1\nbeta",
        "expected_native": "alpha\n1\nbeta",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Collection API remove(index) transfers owned element out of vector."
    },
    {
        "id": "ORACLE-27-REBIND-AFTER-BORROWS",
        "name": "Rebinding after outstanding borrows end",
        "file": "oracle_27_rebind_after_borrows_end.moss",
        "category": "Rebinding & Loops",
        "spec_citation": "Proposal §10 row 'b = a; b = new_value', §3.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "1\n2",
        "expected_native": "1\n2",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Rebinding a non-primitive local is legal after earlier borrows end."
    },
    {
        "id": "ORACLE-28-REBIND-WHILE-BORROWED-REJECT",
        "name": "Drop/rebind while borrowed rejected",
        "file": "oracle_28_rebind_while_borrowed_rejected.moss",
        "category": "Rebinding & Loops",
        "spec_citation": "Proposal §10 row 'c = b; b = make2(); echo c', §3.2",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_USE_AFTER_REBIND",
        "alternative_diagnostics": [
            "OWNERSHIP_SOURCE_BORROWED",
            "OWNERSHIP_CONFLICTING_ACCESS",
            "DROP_WHILE_BORROWED"
        ],
        "description": "Rebinding or dropping an owned local while an active borrow is live is rejected."
    },
    {
        "id": "ORACLE-29-LOOP-CARRIED-OWNERSHIP",
        "name": "Loop-carried ownership handled by fixed point",
        "file": "oracle_29_loop_carried_ownership_rebind.moss",
        "category": "Rebinding & Loops",
        "spec_citation": "Proposal §10 row 'frontier = expand(frontier) in a loop', §4.2, §7.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "3\n4",
        "expected_native": "3\n4",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Rebinding a non-primitive local across loop iterations is supported via effect fixed points."
    },

    # ─── Joins and Control Flow Widening (§10) ────────────────────────────────
    {
        "id": "ORACLE-30-JOIN-BORROW-FRESH-REJECT",
        "name": "Join borrowed element cannot promote to owned",
        "file": "oracle_30_join_borrowed_and_fresh_rejected.moss",
        "category": "Joins",
        "spec_citation": "Proposal §10 row 'if c: b = left[0] else: b = Fresh(); echo b', §4",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_MIXED_JOIN",
        "alternative_diagnostics": ["OWNERSHIP_ILLEGAL_PROMOTION", "ILLEGAL_JOIN_BORROWED_OWNED"],
        "description": "A borrowed collection element cannot promote to owned at a live control-flow join."
    },
    {
        "id": "ORACLE-31-JOIN-PROMOTED-CONSUME",
        "name": "Join promoted consume from owned local",
        "file": "oracle_31_join_promoted_consume.moss",
        "category": "Joins",
        "spec_citation": "Proposal §10 row 'if c: b = left else: b = Fresh(); echo b', §4",
        "kind": "ACCEPTED",
        "expected_fast_debug": "42\n99",
        "expected_native": "42\n99",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Promoted CONSUME from an owned local merges with a fresh owned constructor at a live join."
    },

    # ─── Enums and Pattern Borrows (§10) ──────────────────────────────────────
    {
        "id": "ORACLE-32-MATCH-PAYLOAD-WRITE",
        "name": "Pattern WRITE borrow",
        "file": "oracle_32_match_payload_write_borrow.moss",
        "category": "Patterns & Match Consume",
        "spec_citation": "Proposal §10 row 'match job.state and mutate a payload', §6.1",
        "kind": "ACCEPTED",
        "expected_fast_debug": "20",
        "expected_native": "20",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Pattern bindings on a writable scrutinee can infer WRITE borrows and mutate payloads."
    },
    {
        "id": "ORACLE-33-MATCH-LAST-USE-OVERWRITE",
        "name": "Pattern last-use overwrite",
        "file": "oracle_33_match_payload_last_use_overwrite.moss",
        "category": "Patterns & Match Consume",
        "spec_citation": "Proposal §10 row 'match job.state then overwrite state after last pattern use', §6.1, §8",
        "kind": "ACCEPTED",
        "expected_fast_debug": "old\nnow_idle",
        "expected_native": "old\nnow_idle",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Pattern borrow ends at last use inside arm, permitting whole-field overwrite afterward."
    },
    {
        "id": "ORACLE-34-MATCH-OVERWRITE-WHILE-BORROWED-REJECT",
        "name": "Overwrite matched state while payload borrowed rejected",
        "file": "oracle_34_match_payload_overwrite_while_borrowed_rejected.moss",
        "category": "Patterns & Match Consume",
        "spec_citation": "Proposal §10 row 'Overwrite matched state while its payload is still borrowed', §6.1",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_CONFLICTING_ACCESS",
        "alternative_diagnostics": [
            "MATCH_READ_BORROW_ACTIVE",
            "OVERWRITE_WHILE_BORROWED",
            "MATCH_PAYLOAD_BORROW_ACTIVE"
        ],
        "description": "Overwriting a matched state while its payload binding is still live is rejected."
    },
    {
        "id": "ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT",
        "name": "Ordinary match cannot consume",
        "file": "oracle_35_match_ordinary_consume_rejected.moss",
        "category": "Patterns & Match Consume",
        "spec_citation": "Proposal §10 row 'match job.state and transfer payload', §6.1",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_ILLEGAL_CONSUME",
        "alternative_diagnostics": [
            "BORROWED_ENUM_PAYLOAD_ACCESS",
            "MATCH_ORDINARY_CANNOT_CONSUME"
        ],
        "description": "Ordinary match produces borrowed payload views and cannot transfer or consume payloads."
    },
    {
        "id": "ORACLE-36-MATCH-CONSUME-QUEUE-POP",
        "name": "match consume queue.pop()",
        "file": "oracle_36_match_consume_queue_pop.moss",
        "category": "Patterns & Match Consume",
        "spec_citation": "Proposal §10 row 'match consume queue.pop()', §6.2",
        "kind": "ACCEPTED",
        "expected_fast_debug": "hello",
        "expected_native": "hello",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "match consume on an owned rvalue from queue.pop() dismantles the enum and transfers payloads."
    },
    {
        "id": "ORACLE-37-MATCH-CONSUME-UNIFIED-FIELD-REJECT",
        "name": "match consume unified field rejected",
        "file": "oracle_37_match_consume_unified_field_rejected.moss",
        "category": "Patterns & Match Consume",
        "spec_citation": "Proposal §10 row 'match consume job.state', §6.2, §9",
        "kind": "REJECTED",
        "expected_diagnostic": "MATCH_CONSUME_INTERIOR_PLACE",
        "alternative_diagnostics": ["MATCH_CONSUME_FIELD_REJECTED"],
        "description": "match consume on a unified field place is rejected; fields cannot be taken implicitly."
    },

    # ─── Collections and Iteration (§10) ──────────────────────────────────────
    {
        "id": "ORACLE-38-MAP-VALUES-BORROWED-VIEW",
        "name": "Map.values() bound temporarily then mutated after borrow ends",
        "file": "oracle_38_map_values_borrowed_view.moss",
        "category": "Collections & Iteration",
        "spec_citation": "Proposal §10 row 'Map.values() bound temporarily then source mutated after last use', §5.3",
        "kind": "ACCEPTED",
        "expected_fast_debug": "2\n3",
        "expected_native": "2\n3",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Map views end at last use, allowing source mutation after the borrow expires."
    },
    {
        "id": "ORACLE-39-VEC-FOR-READ",
        "name": "Vector iteration: READ borrow",
        "file": "oracle_39_vector_for_read.moss",
        "category": "Collections & Iteration",
        "spec_citation": "Proposal §10 row 'for item in items: echo item for a Vector', §5.3, §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "1\n2",
        "expected_native": "1\n2",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Vector for-loop elements are borrowed READ views without mutable iteration overhead."
    },
    {
        "id": "ORACLE-40-VEC-FOR-WRITE",
        "name": "Vector iteration: WRITE element mutation",
        "file": "oracle_40_vector_for_write.moss",
        "category": "Collections & Iteration",
        "spec_citation": "Proposal §10 row 'for item in items: item.bump() for writable Vector elements', §5.3, §12",
        "kind": "ACCEPTED",
        "expected_fast_debug": "11\n21",
        "expected_native": "11\n21",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "for-loop on writable vector elements infers WRITE and mutates elements in place."
    },
    {
        "id": "ORACLE-41-VEC-FOR-CONSUME-REJECT",
        "name": "Vector iteration: consume element rejected",
        "file": "oracle_41_vector_for_consume_rejected.moss",
        "category": "Collections & Iteration",
        "spec_citation": "Proposal §10 row 'for item in items: consume(item)', §5.3, §12",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_VECTOR_ELEMENT_CONSUME",
        "alternative_diagnostics": ["OWNERSHIP_ILLEGAL_CONSUME"],
        "description": "Loop elements are borrowed views and cannot be consumed."
    },
    {
        "id": "ORACLE-42-CUSTOM-WRAPPER-REMOVE",
        "name": "User-defined wrapper around Vector.remove",
        "file": "oracle_42_custom_wrapper_remove.moss",
        "category": "Collections & Iteration",
        "spec_citation": "Proposal §10 row 'User-defined wrapper fn remove_at(a, i): return a.elements.remove(i)', §9",
        "kind": "ACCEPTED",
        "expected_fast_debug": "y\n2\nz",
        "expected_native": "y\n2\nz",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Custom record types can wrap Vector and delegate element removal."
    },

    # ─── Handlers and Messages (§10 & §7.5) ───────────────────────────────────
    {
        "id": "ORACLE-43-HANDLER-PRIMITIVE-REPLY",
        "name": "Primitive incoming handler parameter reply permitted",
        "file": "oracle_43_handler_primitive_reply.moss",
        "category": "Handlers & Messages",
        "spec_citation": "Proposal §10 row 'reply n where n: Int', §7.5",
        "kind": "ACCEPTED",
        "expected_fast_debug": "42",
        "expected_native": "42",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Primitive Copy input values may be directly replied from handlers."
    },
    {
        "id": "ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT",
        "name": "Incoming handler parameter mutation rejected even when primitive",
        "file": "oracle_44_handler_primitive_mutate_rejected.moss",
        "category": "Handlers & Messages",
        "spec_citation": "Proposal §10 row 'n = n + 1 where n: Int', §7.5",
        "kind": "REJECTED",
        "expected_diagnostic": "IMMUTABLE_MESSAGE_PAYLOAD",
        "alternative_diagnostics": [
            "CANNOT_REBIND_PARAMETER",
            "IMMUTABLE_PARAMETER_MUTATION",
            "message payloads are immutable snapshots",
            "cannot WRITE incoming message payload"
        ],
        "description": "Handler parameter bindings are immutable snapshots even for primitive types."
    },
    {
        "id": "ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL",
        "name": "Independent primitive local copy and reply in handler",
        "file": "oracle_45_handler_primitive_rebind_local.moss",
        "category": "Handlers & Messages",
        "spec_citation": "Proposal §10 row 'result = n; result = result + 1; reply result', §7.5",
        "kind": "ACCEPTED",
        "expected_fast_debug": "11",
        "expected_native": "11",
        "authorized_explicit_copies": 0,
        "max_hidden_clones": 0,
        "description": "Copying primitive input into a mutable local and modifying it is legal."
    },
    {
        "id": "ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT",
        "name": "Non-primitive incoming handler parameter reply rejected",
        "file": "oracle_46_handler_nonprimitive_reply_rejected.moss",
        "category": "Handlers & Messages",
        "spec_citation": "Proposal §10 row 'reply payload where payload: Report', §7.5",
        "kind": "REJECTED",
        "expected_diagnostic": "OWNERSHIP_ILLEGAL_CONSUME",
        "alternative_diagnostics": ["IMMUTABLE_MESSAGE_PAYLOAD", "CANNOT_REPLY_NONPRIMITIVE_INPUT"],
        "description": "Non-primitive input parameters cannot be replied without explicit copy(x)."
    },
]


def run_process(cmd: list[str], cwd: Path | None = None, timeout: int = 15) -> tuple[int, str, str, float]:
    start = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            cwd=str(cwd or ROOT), timeout=timeout
        )
        elapsed = time.perf_counter() - start
        return proc.returncode, proc.stdout, proc.stderr, elapsed
    except subprocess.TimeoutExpired:
        elapsed = time.perf_counter() - start
        return -1, "", "TIMEOUT", elapsed


def count_clones_in_rust(rs_path: Path) -> int:
    if not rs_path.exists():
        return 0
    content = rs_path.read_text()
    # Find fn main() or function bodies, ignore standard library runtime blocks
    # Focus on lines after moss__main or in generated functions
    clone_count = 0
    in_user_code = False
    for line in content.splitlines():
        if "fn main()" in line or "moss__main" in line:
            in_user_code = True
        if in_user_code:
            # Match .clone()
            matches = re.findall(r"\.clone\(\)", line)
            clone_count += len(matches)
    return clone_count


def evaluate_fixture(fixture: dict, compiler: Path, tmp_dir: Path) -> dict:
    fid = fixture["id"]
    fname = fixture["file"]
    kind = fixture["kind"]
    fpath = FIXTURES_DIR / fname

    result = {
        "id": fid,
        "name": fixture["name"],
        "file": fname,
        "category": fixture["category"],
        "spec_citation": fixture["spec_citation"],
        "kind": kind,
        "status": "PASS",
        "modes": {},
        "failures": [],
        "hidden_clones": 0,
        "explicit_copies": fixture.get("authorized_explicit_copies", 0),
        "timings": {},
    }

    if not fpath.exists():
        result["status"] = "FAIL"
        result["failures"].append(f"Fixture file missing: {fpath}")
        return result

    # 1. Semantic Checker
    check_code, check_stdout, check_stderr, check_time = run_process(
        [str(compiler), "check", "--json", str(fpath)]
    )
    result["timings"]["checker"] = check_time
    combined_check = check_stdout + "\n" + check_stderr

    if kind == "REJECTED":
        exp_diag = fixture["expected_diagnostic"]
        alts = fixture.get("alternative_diagnostics", [])
        all_expected = [exp_diag] + alts
        if check_code == 0:
            result["status"] = "FAIL"
            result["modes"]["checker"] = "UNEXPECTED_ACCEPT"
            result["failures"].append(
                f"Checker unexpectedly ACCEPTED invalid program; expected rejection with [{exp_diag}]"
            )
        else:
            diag_matched = any(d in combined_check for d in all_expected)
            if diag_matched:
                result["modes"]["checker"] = "PASS"
            else:
                result["status"] = "FAIL"
                result["modes"]["checker"] = "WRONG_DIAGNOSTIC"
                result["failures"].append(
                    f"Checker rejected but expected diagnostic [{exp_diag}] not found; output: {combined_check.strip()}"
                )
        # REJECTED programs do not execute in Fast Debug or Native
        return result

    # For ACCEPTED programs:
    if check_code != 0:
        result["status"] = "FAIL"
        result["modes"]["checker"] = "UNEXPECTED_REJECT"
        result["failures"].append(f"Checker unexpectedly REJECTED valid program: {combined_check.strip()}")
    else:
        result["modes"]["checker"] = "PASS"

    # 2. Fast Debug Execution
    fd_code, fd_stdout, fd_stderr, fd_time = run_process(
        [str(compiler), "run", "--interp", str(fpath)]
    )
    result["timings"]["fast_debug"] = fd_time
    actual_fd = fd_stdout.strip()
    exp_fd = fixture["expected_fast_debug"]

    if fd_code != 0:
        result["status"] = "FAIL"
        result["modes"]["fast_debug"] = "CRASH"
        result["failures"].append(f"Fast Debug crashed (exit {fd_code}): {fd_stderr.strip()}")
    elif actual_fd != exp_fd:
        result["status"] = "FAIL"
        result["modes"]["fast_debug"] = "VALUE_MISMATCH"
        result["failures"].append(f"Fast Debug value mismatch: expected {exp_fd!r}, got {actual_fd!r}")
    else:
        result["modes"]["fast_debug"] = "PASS"

    # 3. Native Debug Lowering & Rustc Compilation
    out_rs = tmp_dir / f"{fid}.rs"
    out_bin = tmp_dir / f"{fid}_bin"
    emit_code, emit_stdout, emit_stderr, emit_time = run_process(
        [str(compiler), str(fpath), "-o", str(out_rs)]
    )
    result["timings"]["native_emit"] = emit_time

    if emit_code != 0:
        result["status"] = "FAIL"
        result["modes"]["native_debug"] = "EMIT_FAIL"
        result["failures"].append(f"Native emission failed: {emit_stderr.strip()}")
    else:
        # Hidden clone budget audit
        total_clones = count_clones_in_rust(out_rs)
        auth_clones = fixture.get("authorized_explicit_copies", 0)
        hidden_clones = max(0, total_clones - auth_clones)
        result["hidden_clones"] = hidden_clones

        if hidden_clones > fixture.get("max_hidden_clones", 0):
            result["status"] = "FAIL"
            result["failures"].append(
                f"Hidden clone budget exceeded: found {hidden_clones} unauthorized .clone() calls (total {total_clones}, authorized {auth_clones})"
            )

        # rustc compilation
        rustc_code, rustc_stdout, rustc_stderr, rustc_time = run_process(
            ["rustc", str(out_rs), "-o", str(out_bin)]
        )
        result["timings"]["rustc_debug"] = rustc_time
        if rustc_code != 0:
            result["status"] = "FAIL"
            result["modes"]["native_debug"] = "RUSTC_FAIL"
            result["failures"].append(f"rustc compilation failed: {rustc_stderr.strip()[:300]}")
        else:
            # Native execution
            nat_code, nat_stdout, nat_stderr, nat_time = run_process([str(out_bin)])
            result["timings"]["native_exec"] = nat_time
            actual_nat = nat_stdout.strip()
            exp_nat = fixture["expected_native"]
            if nat_code != 0:
                result["status"] = "FAIL"
                result["modes"]["native_debug"] = "CRASH"
                result["failures"].append(f"Native binary crashed (exit {nat_code}): {nat_stderr.strip()}")
            elif actual_nat != exp_nat:
                result["status"] = "FAIL"
                result["modes"]["native_debug"] = "VALUE_MISMATCH"
                result["failures"].append(f"Native value mismatch: expected {exp_nat!r}, got {actual_nat!r}")
            else:
                result["modes"]["native_debug"] = "PASS"

    # 4. Native Optimized (-O) Lowering & Rustc Compilation
    out_opt_rs = tmp_dir / f"{fid}_opt.rs"
    out_opt_bin = tmp_dir / f"{fid}_opt_bin"
    emit_opt_code, emit_opt_stdout, emit_opt_stderr, emit_opt_time = run_process(
        [str(compiler), "-O", str(fpath), "-o", str(out_opt_rs)]
    )
    result["timings"]["native_opt_emit"] = emit_opt_time

    if emit_opt_code != 0:
        result["status"] = "FAIL"
        result["modes"]["native_opt"] = "EMIT_FAIL"
        result["failures"].append(f"Native -O emission failed: {emit_opt_stderr.strip()}")
    else:
        rustc_opt_code, rustc_opt_stdout, rustc_opt_stderr, rustc_opt_time = run_process(
            ["rustc", "-O", str(out_opt_rs), "-o", str(out_opt_bin)]
        )
        result["timings"]["rustc_opt"] = rustc_opt_time
        if rustc_opt_code != 0:
            result["status"] = "FAIL"
            result["modes"]["native_opt"] = "RUSTC_FAIL"
            result["failures"].append(f"rustc -O compilation failed: {rustc_opt_stderr.strip()[:300]}")
        else:
            nat_opt_code, nat_opt_stdout, nat_opt_stderr, nat_opt_time = run_process([str(out_opt_bin)])
            result["timings"]["native_opt_exec"] = nat_opt_time
            actual_nat_opt = nat_opt_stdout.strip()
            exp_nat_opt = fixture["expected_native"]
            if nat_opt_code != 0:
                result["status"] = "FAIL"
                result["modes"]["native_opt"] = "CRASH"
                result["failures"].append(f"Native -O binary crashed (exit {nat_opt_code}): {nat_opt_stderr.strip()}")
            elif actual_nat_opt != exp_nat_opt:
                result["status"] = "FAIL"
                result["modes"]["native_opt"] = "VALUE_MISMATCH"
                result["failures"].append(f"Native -O value mismatch: expected {exp_nat_opt!r}, got {actual_nat_opt!r}")
            else:
                result["modes"]["native_opt"] = "PASS"

    return result


def generate_failure_ledger(results: list[dict], compiler_desc: str) -> str:
    lines = []
    lines.append(f"# Phase 28 Independent Semantic Oracle — Failure Ledger")
    lines.append(f"\nEvaluated against: `{compiler_desc}`")
    lines.append(f"Specification Baseline: `docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md` (§10 & §12)\n")

    failed_cases = [r for r in results if r["status"] == "FAIL"]
    passed_cases = [r for r in results if r["status"] == "PASS"]

    lines.append(f"## Summary Statistics")
    lines.append(f"- **Total Fixtures:** {len(results)}")
    lines.append(f"- **Passing (Compliant with Spec):** {len(passed_cases)}")
    lines.append(f"- **Failing (Compiler Defect / Non-Compliant):** {len(failed_cases)}")
    total_hidden_clones = sum(r.get("hidden_clones", 0) for r in results)
    lines.append(f"- **Total Unauthorized Hidden Clones Detected:** {total_hidden_clones}")
    lines.append("")

    lines.append("## Detailed Defect Entries\n")
    lines.append("| Failure ID | Fixture ID | Category | Mode | Description | Specification Citation |")
    lines.append("| --- | --- | --- | --- | --- | --- |")

    fail_counter = 1
    for r in failed_cases:
        fail_id = f"DEFECT-{fail_counter:03d}"
        fail_counter += 1
        modes_str = ", ".join(f"{k}:{v}" for k, v in r["modes"].items() if v != "PASS") or "CLONES"
        desc = "; ".join(r["failures"]).replace("\n", " ")[:120]
        spec = r["spec_citation"]
        lines.append(f"| `{fail_id}` | `{r['id']}` | {r['category']} | `{modes_str}` | {desc} | {spec} |")

    lines.append("\n## Per-Fixture Detail\n")
    for r in results:
        status_symbol = "✅ PASS" if r["status"] == "PASS" else "❌ FAIL"
        lines.append(f"### {r['id']}: {r['name']} ({status_symbol})")
        lines.append(f"- **Category:** {r['category']}")
        lines.append(f"- **Spec Rule:** {r['spec_citation']}")
        lines.append(f"- **Expected Outcome:** {r['kind']}")
        modes_detail = " | ".join(f"{k}: {v}" for k, v in r["modes"].items())
        lines.append(f"- **Engine Modes:** {modes_detail}")
        lines.append(f"- **Hidden Clones:** {r['hidden_clones']} (authorized: {r['explicit_copies']})")
        if r["failures"]:
            lines.append(f"- **Observed Failures:**")
            for f in r["failures"]:
                lines.append(f"  - `{f}`")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 28 Independent Semantic Oracle Suite")
    parser.add_argument("--compiler", type=Path, default=ROOT / "moss", help="Path to moss compiler binary")
    parser.add_argument("--tmp", type=Path, default=DEFAULT_TMP, help="Scratch directory for artifacts")
    parser.add_argument("--json-out", type=Path, default=None, help="Output JSON results path")
    parser.add_argument("--ledger-out", type=Path, default=None, help="Output failure ledger Markdown path")
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    if not compiler.exists():
        print(f"Error: compiler executable not found at {compiler}", file=sys.stderr)
        return 2

    tmp_dir = args.tmp.resolve()
    tmp_dir.mkdir(parents=True, exist_ok=True)

    print(f"================================================================================")
    print(f"Phase 28 Independent Semantic Oracle Suite")
    print(f"Compiler: {compiler}")
    print(f"Fixtures: {len(ORACLE_FIXTURES)}")
    print(f"Specification: docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md §10 & §12")
    print(f"================================================================================\n")

    results = []
    passed = 0
    failed = 0

    for fixture in ORACLE_FIXTURES:
        res = evaluate_fixture(fixture, compiler, tmp_dir)
        results.append(res)
        status_tag = "[PASS]" if res["status"] == "PASS" else "[FAIL]"
        modes_str = " ".join(f"{k}={v}" for k, v in res["modes"].items())
        clone_str = f"clones={res['hidden_clones']}"
        print(f"{status_tag} {res['id']:<35} | {res['category']:<25} | {modes_str} {clone_str}")
        if res["failures"]:
            for fail in res["failures"]:
                print(f"       -> {fail}")
        if res["status"] == "PASS":
            passed += 1
        else:
            failed += 1

    print("\n--------------------------------------------------------------------------------")
    print(f"Total: {len(results)} | Passed: {passed} | Defective / Failed: {failed}")
    total_hidden = sum(r.get("hidden_clones", 0) for r in results)
    print(f"Total Hidden Clones Detected: {total_hidden}")
    print("--------------------------------------------------------------------------------\n")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(results, indent=2))
        print(f"Results written to {args.json_out}")

    if args.ledger_out:
        args.ledger_out.parent.mkdir(parents=True, exist_ok=True)
        ledger_text = generate_failure_ledger(results, str(compiler))
        args.ledger_out.write_text(ledger_text)
        print(f"Failure ledger written to {args.ledger_out}")

    # Note: Oracle harness returns 0 when execution completes successfully,
    # regardless of whether compiler has defects, but prints clear status.
    return 0


if __name__ == "__main__":
    sys.exit(main())
