#!/usr/bin/env python3
import json
import subprocess
import os
import sys

MOSS_BIN = "/home/kuttybanerjee/daji/moss/moss"
SUITE_DIR = "/home/kuttybanerjee/daji/moss/tmp/swarm/domain_boundaries"

PROBES = [
    # Probe 1: Route Cycles
    {
        "file": "probe1a_2cycle.moss",
        "category": "Probe 1: Route Cycle",
        "description": "2-domain route cycle (A -> B -> A)",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "concrete domain route cycle detected",
    },
    {
        "file": "probe1b_3cycle.moss",
        "category": "Probe 1: Route Cycle",
        "description": "3-domain route cycle (A -> B -> C -> A)",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "concrete domain route cycle detected",
    },
    {
        "file": "torture_self_route.moss",
        "category": "Probe 1: Route Cycle",
        "description": "1-domain self route cycle (Node -> Node)",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "concrete domain route cycle detected",
    },
    # Probe 2: Illegal Self-Send and Same-Domain Chaining
    {
        "file": "probe2a_self_send.moss",
        "category": "Probe 2: Self-Send & Chaining",
        "description": "Domain attempting message self.X()",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "self-send is not allowed; move shared logic to an ordinary helper function",
    },
    {
        "file": "probe2b_same_domain_chaining.moss",
        "category": "Probe 2: Self-Send & Chaining",
        "description": "Same-domain handler chaining via route to another instance of same domain",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "same-domain handler chaining is not allowed; move shared logic to an ordinary helper function",
    },
    {
        "file": "probe2c_handler_direct_call.moss",
        "category": "Probe 2: Self-Send & Chaining",
        "description": "Handler calling another handler via self.Helper()",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "naked cross-domain call 'self.Helper' requires 'message'",
    },
    {
        "file": "probe2d_handler_unqualified_call.moss",
        "category": "Probe 2: Self-Send & Chaining",
        "description": "Handler calling another handler via unqualified Helper()",
        "expected_ok": False,
        "expected_code": "UNKNOWN_SYMBOL_OR_TYPE",
        "expected_msg_snippet": "unknown local function 'Helper'",
    },
    # Probe 3: Domain handle escaping
    {
        "file": "probe3a_pass_fn_param.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle passed as typed function parameter",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be passed as ordinary function parameters",
    },
    {
        "file": "probe3a_untyped_fn_param.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle passed as untyped function parameter",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be passed as ordinary function parameters",
    },
    {
        "file": "probe3b_pass_handler_param.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle passed as handler payload parameter",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be passed as handler payloads",
    },
    {
        "file": "probe3c_return_reply.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle returned from reply",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be returned through reply",
    },
    {
        "file": "probe3d_store_vector.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle stored in Vector literal",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be used as ordinary values or payloads",
    },
    {
        "file": "probe3d2_vector_push.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle passed to Vector.push",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be used as ordinary values or payloads",
    },
    {
        "file": "probe3e_store_map.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle stored in Map literal",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "is not an ordinary value; use domainroutes and message targets only",
    },
    {
        "file": "probe3e2_map_assign.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle assigned into Map index",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be used as ordinary values or payloads",
    },
    {
        "file": "torture_pipeline_domain.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle captured inside functional pipeline",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be used as ordinary values or payloads",
    },
    {
        "file": "torture_domain_in_type.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle declared as field in type aggregate",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be stored in aggregates",
    },
    {
        "file": "torture_domain_in_state.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle declared as ordinary domain state field",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handles cannot be stored as ordinary state",
    },
    {
        "file": "torture_handle_rebind.moss",
        "category": "Probe 3: Handle Escaping",
        "description": "Domain handle local rebind/mutation",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain handle binding 'w' is immutable",
    },
    # Probe 4: Dynamic domain construction
    {
        "file": "probe4a_construction_while.moss",
        "category": "Probe 4: Dynamic Construction",
        "description": "Domain construction inside while loop",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain construction is only allowed as a top-level binding in main's composition prefix",
    },
    {
        "file": "probe4b_construction_for.moss",
        "category": "Probe 4: Dynamic Construction",
        "description": "Domain construction inside for loop",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain construction is only allowed as a top-level binding in main's composition prefix",
    },
    {
        "file": "probe4c_construction_if.moss",
        "category": "Probe 4: Dynamic Construction",
        "description": "Domain construction inside if conditional",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain construction is only allowed as a top-level binding in main's composition prefix",
    },
    {
        "file": "probe4d_construction_helper.moss",
        "category": "Probe 4: Dynamic Construction",
        "description": "Domain construction inside helper function",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain construction is allowed only in the main composition prefix",
    },
    {
        "file": "probe4e_construction_post_prefix.moss",
        "category": "Probe 4: Dynamic Construction",
        "description": "Domain construction after composition prefix has closed",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain construction must occur before ordinary execution begins in main",
    },
    {
        "file": "torture_domain_in_test.moss",
        "category": "Probe 4: Dynamic Construction",
        "description": "Domain construction inside test block",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain construction is allowed only in the main composition prefix",
    },
    # Probe 5: Impure domain state initializers
    {
        "file": "probe5a_message_in_initializer.moss",
        "category": "Probe 5: Impure Initializer",
        "description": "Message call inside domain state initializer",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain state initializers must be side-effect-free",
    },
    {
        "file": "probe5b_impure_helper_io.moss",
        "category": "Probe 5: Impure Initializer",
        "description": "Impure helper with I/O (echo) in domain state initializer",
        "expected_ok": False,
        "expected_code": "MOSS_COMPILE_ERROR",
        "expected_msg_snippet": "domain state initializers must be side-effect-free",
    },
    {
        "file": "probe5c_pure_helper.moss",
        "category": "Probe 5: State Initializer (Valid)",
        "description": "Pure helper inside domain state initializer",
        "expected_ok": True,
        "expected_code": None,
        "expected_msg_snippet": None,
    },
    # Positive Control
    {
        "file": "positive_control.moss",
        "category": "Positive Control",
        "description": "Idiomatic multi-domain DAG with helper reuse, routes, pure init, static composition",
        "expected_ok": True,
        "expected_code": None,
        "expected_msg_snippet": None,
    },
]

def run_suite():
    passed = 0
    failed = 0
    results = []

    print("=" * 80)
    print("MOSS DOMAIN INVARIANTS & NEGATIVE BOUNDARIES TORTURE TEST SUITE")
    print("=" * 80)

    for item in PROBES:
        path = os.path.join(SUITE_DIR, item["file"])
        cmd = [MOSS_BIN, "check", path, "--json"]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            output = json.loads(proc.stdout)
        except Exception as e:
            print(f"FAILED TO PARSE JSON: {path}, stdout={proc.stdout}, stderr={proc.stderr}")
            failed += 1
            continue

        ok = output.get("ok", False)
        err = output.get("error", {})
        code = err.get("code")
        msg = err.get("message", "")
        line = err.get("line")

        test_passed = True
        reasons = []

        if ok != item["expected_ok"]:
            test_passed = False
            reasons.append(f"Expected ok={item['expected_ok']}, got {ok}")

        if not item["expected_ok"]:
            if item["expected_code"] and code != item["expected_code"]:
                test_passed = False
                reasons.append(f"Expected code={item['expected_code']}, got {code}")
            if item["expected_msg_snippet"] and item["expected_msg_snippet"] not in msg:
                test_passed = False
                reasons.append(f"Expected message snippet '{item['expected_msg_snippet']}', got '{msg}'")

        status_str = "PASS" if test_passed else "FAIL"
        if test_passed:
            passed += 1
        else:
            failed += 1

        print(f"[{status_str}] {item['category']}: {item['description']}")
        print(f"       File: {item['file']} (line {line}) -> ok={ok}, code={code}")
        if not ok:
            print(f"       Diagnostic: {msg.splitlines()[0]}")
        if not test_passed:
            print(f"       REASONS: {reasons}")
        print()

    print("=" * 80)
    print(f"PROBE SUMMARY: {passed} passed, {failed} failed out of {len(PROBES)} tests.")
    print("=" * 80)

    # Now verify Positive Control Fast Debug and Native
    print("\nVerifying Positive Control Execution:")
    pos_file = os.path.join(SUITE_DIR, "positive_control.moss")

    # 1. Fast Debug
    print("1. Running Fast Debug (moss run --interp)...")
    res_interp = subprocess.run([MOSS_BIN, "run", "--interp", pos_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    print(f"   Exit code: {res_interp.returncode}")
    print(f"   Output:\n{res_interp.stdout.strip()}")
    assert res_interp.returncode == 0, "Fast Debug failed!"
    assert res_interp.stdout.strip() == "1\n2\n770\n4", "Fast Debug output mismatch!"

    # 2. Native Compilation
    print("\n2. Compiling and running natively...")
    rs_file = os.path.join(SUITE_DIR, "positive_control.rs")
    bin_file = os.path.join(SUITE_DIR, "positive_control")
    res_gen = subprocess.run([MOSS_BIN, pos_file, "-o", rs_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res_gen.returncode == 0, f"Native codegen failed: {res_gen.stderr}"
    res_rustc = subprocess.run(["rustc", "-D", "warnings", rs_file, "-o", bin_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res_rustc.returncode == 0, f"rustc compilation failed: {res_rustc.stderr}"
    res_native = subprocess.run([bin_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    print(f"   Exit code: {res_native.returncode}")
    print(f"   Output:\n{res_native.stdout.strip()}")
    assert res_native.returncode == 0, "Native run failed!"
    assert res_native.stdout.strip() == "1\n2\n770\n4", "Native run output mismatch!"

    print("\nALL VERIFICATIONS PASSED: 100% PARITY BETWEEN FAST DEBUG AND NATIVE EXECUTION!")

if __name__ == "__main__":
    run_suite()
