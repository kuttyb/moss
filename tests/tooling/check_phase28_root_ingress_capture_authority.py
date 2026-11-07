#!/usr/bin/env python3
"""Exercise exact checker authority for compiler-generated Root captures."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = str(Path(sys.argv[1]).resolve())
FIXTURES = [
    ("tests/phase28/oracle/fixtures/oracle_43_handler_primitive_reply.moss", 1),
    ("tests/phase28/oracle/fixtures/oracle_45_handler_primitive_rebind_local.moss", 1),
    ("tests/tooling/fixtures/phase28_root_same_line_messages.moss", 2),
    ("tests/tooling/fixtures/phase21_root_nested_ingress.moss", 5),
    ("tests/tooling/fixtures/phase28_root_repeated_nested_messages.moss", 2),
    ("tests/tooling/fixtures/phase28_root_nested_message_arguments.moss", 2),
    ("tests/tooling/fixtures/phase28_root_depth2_message_arguments.moss", 3),
    ("tests/tooling/fixtures/phase28_root_nested_copy_payload.moss", 2),
]
ALIAS_REJECTED = "tests/tooling/fixtures/phase28_root_domain_handle_alias_rejected.moss"
UNKNOWN_HANDLER = "tests/tooling/fixtures/phase28_root_unknown_handler.moss"
FRESH_PAYLOAD = "tests/fresh_message_payload.moss"
TMP_ROOT = ROOT / "tmp"


def check(source, extra_env=None):
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("MOSS_TEST_"):
            env.pop(key)
    env.update(extra_env or {})
    return subprocess.run(
        [COMPILER, "check", source, "--json"], cwd=ROOT, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )


def nested_native_consumer_cases():
    sources = (
        ("tests/tooling/fixtures/phase28_root_repeated_nested_messages.moss",
         "14\n"),
        ("tests/tooling/fixtures/phase28_root_nested_message_arguments.moss",
         "7\n"),
        ("tests/tooling/fixtures/phase28_root_depth2_message_arguments.moss",
         "7\n"),
        ("tests/tooling/fixtures/phase28_root_nested_copy_payload.moss",
         "9\n"),
    )
    count = 0
    with tempfile.TemporaryDirectory(prefix="root-capture-consumer-",
                                     dir=TMP_ROOT) as temporary:
        output = Path(temporary) / "nested.rs"
        binary = Path(temporary) / "nested"
        for source, expected_output in sources:
            for mode in ("default", "shadow"):
                env = os.environ.copy()
                for key in list(env):
                    if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
                        env.pop(key)
                if mode == "shadow":
                    env["MOSS_EST"] = "shadow"
                generated = subprocess.run(
                    [COMPILER, source, "-O0", "-o", str(output)], cwd=ROOT,
                    env=env, text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False)
                assert generated.returncode == 0, (source, mode, generated.stdout,
                                                    generated.stderr)
                compiled = subprocess.run(
                    ["rustc", "--edition=2021", "-Dwarnings", "-C",
                     "opt-level=0", str(output), "-o", str(binary)], cwd=ROOT,
                    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    check=False)
                assert compiled.returncode == 0, (source, mode, compiled.stdout,
                                                   compiled.stderr)
                executed = subprocess.run(
                    [str(binary)], cwd=ROOT, text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False)
                assert executed.returncode == 0 and executed.stdout == expected_output and (
                    executed.stderr == ""), (source, mode, executed.stdout,
                                               executed.stderr)
                count += 1
                if source.endswith(("phase28_root_nested_message_arguments.moss",
                                    "phase28_root_depth2_message_arguments.moss",
                                    "phase28_root_nested_copy_payload.moss")):
                    debug = subprocess.run(
                        [COMPILER, "run", "--interp", source], cwd=ROOT,
                        env=env, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, check=False)
                    assert debug.returncode == 0 and (
                        debug.stdout == expected_output and debug.stderr == ""), (
                            source, mode, debug.stdout, debug.stderr)
                    count += 1
                for hook in (
                    "MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_CAPTURE_REQUIREMENT",
                    "MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_CAPTURE_REQUIREMENT",
                    "MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_CAPTURE_FACT",
                    "MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_CAPTURE_FACT",
                ):
                    env = os.environ.copy()
                    for key in list(env):
                        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
                            env.pop(key)
                    if mode == "shadow":
                        env["MOSS_EST"] = "shadow"
                    env[hook] = "1"
                    result = subprocess.run(
                        [COMPILER, source, "-O0", "-o", str(output)], cwd=ROOT,
                        env=env, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, check=False)
                    assert result.returncode != 0 and (
                        "Root-ingress capture requirement/fact" in
                        result.stdout + result.stderr), (mode, hook, result.stdout,
                                                         result.stderr)
                    count += 1
                env = os.environ.copy()
                for key in list(env):
                    if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
                        env.pop(key)
                if mode == "shadow":
                    env["MOSS_EST"] = "shadow"
                env["MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_CAPTURE_FACT"] = "expr-id"
                result = subprocess.run(
                    [COMPILER, source, "-O0", "-o", str(output)], cwd=ROOT,
                    env=env, text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False)
                assert result.returncode != 0 and (
                    "Root-ingress" in result.stdout + result.stderr), (
                        source, mode, "expr-id", result.stdout, result.stderr)
                count += 1
                if source.endswith(("phase28_root_nested_message_arguments.moss",
                                    "phase28_root_nested_copy_payload.moss")):
                    env = os.environ.copy()
                    for key in list(env):
                        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
                            env.pop(key)
                    if mode == "shadow":
                        env["MOSS_EST"] = "shadow"
                    env["MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_NESTED_MESSAGE_EXPR_ID"] = "1"
                    result = subprocess.run(
                        [COMPILER, source, "-O0", "-o", str(output)], cwd=ROOT,
                        env=env, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, check=False)
                    assert result.returncode != 0 and (
                        "Root-ingress" in result.stdout + result.stderr), (
                            source, mode, "nested-expr-id", result.stdout,
                            result.stderr)
                    count += 1
                if source.endswith(("phase28_root_depth2_message_arguments.moss",
                                    "phase28_root_nested_copy_payload.moss")):
                    for hook, command, expected_error in (
                        ("MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_OWNER",
                         [COMPILER, source, "-O0", "-o", str(output)],
                         "copy lacks its exact statement source handle"),
                        ("MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_NESTED_MESSAGE_EXPR_ID",
                         [COMPILER, source, "-O0", "-o", str(output)],
                         "Root-ingress"),
                        ("MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_OWNER",
                         [COMPILER, "run", "--interp", source],
                         "statement/message handle missing or ambiguous"),
                        ("MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_NESTED_EXPR_ID",
                         [COMPILER, "run", "--interp", source],
                         "Root-ingress"),
                    ):
                        env = os.environ.copy()
                        for key in list(env):
                            if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
                                env.pop(key)
                        if mode == "shadow":
                            env["MOSS_EST"] = "shadow"
                        env[hook] = "1"
                        result = subprocess.run(
                            command, cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            check=False)
                        assert result.returncode != 0 and (
                            expected_error in result.stdout + result.stderr), (
                                source, mode, hook, result.stdout, result.stderr)
                        count += 1
    return count


def parse_result(result):
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"checker returned malformed JSON: {result.stdout}\n{result.stderr}") from error


def payload_authority_cases(mode, source, rust_value, expected_output):
    env_mode = os.environ.copy()
    for key in list(env_mode):
        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
            env_mode.pop(key)
    if mode == "shadow":
        env_mode["MOSS_EST"] = "shadow"

    def run(args, extra_env=None):
        env = dict(env_mode)
        env.update(extra_env or {})
        return subprocess.run(
            args, cwd=ROOT, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    checked = run([COMPILER, "check", source, "--json"], {
        "MOSS_TEST_DUMP_ROOT_INGRESS_PAYLOAD_FACT": "1"})
    response = parse_result(checked)
    assert checked.returncode == 0 and response.get("ok"), (
        mode, checked.stdout, checked.stderr)
    facts = [line for line in checked.stderr.splitlines()
             if line.startswith("ROOT_INGRESS_PAYLOAD_FACT ")]
    assert len(facts) == 1, (mode, facts)
    for field in ("stmt=", "arena=", "message=", "arg=0", "arg_expr=",
                  "owner=handler:", "action=0", "context=main"):
        assert field in facts[0], (mode, field, facts[0])

    for env in (
        {"MOSS_TEST_DROP_ROOT_INGRESS_PAYLOAD_FACT": "1"},
        {"MOSS_TEST_DUPLICATE_ROOT_INGRESS_PAYLOAD_FACT": "1"},
    ):
        result = run([COMPILER, "check", source, "--json"], env)
        assert result.returncode != 0 and "Root-ingress payload checker fact/requirement set is incomplete" in (
            result.stdout + result.stderr), (mode, env, result.stdout, result.stderr)

    for field in ("owner", "argument", "type", "action", "context", "occurrence"):
        result = run([COMPILER, "check", source, "--json"], {
            "MOSS_TEST_PERTURB_ROOT_INGRESS_PAYLOAD_FACT": field})
        assert result.returncode != 0 and "Root-ingress payload" in (
            result.stdout + result.stderr), (mode, field, result.stdout, result.stderr)

    TMP_ROOT.mkdir(exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix="root-ingress-payload-", dir=TMP_ROOT))
    try:
        rust_path = temp_dir / "fresh_message_payload.rs"
        generated = run([COMPILER, source, "-o", str(rust_path)])
        assert generated.returncode == 0, (mode, generated.stdout, generated.stderr)
        rust = rust_path.read_text()
        payload_line = next((line for line in rust.splitlines()
                             if "__moss_root_arg_" in line), "")
        payload_value = payload_line.split("__moss_root_arg_", 1)[-1].split(" = ", 1)[-1]
        payload_value = payload_value.split("; runtime_invoke", 1)[0]
        assert rust_value in payload_value and ".clone()" not in payload_value, (
            mode, payload_line)

        for env in (
            {"MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_PAYLOAD_REQUIREMENT": "1"},
            {"MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_PAYLOAD_REQUIREMENT": "1"},
            {"MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_PAYLOAD_FACT": "1"},
            {"MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_PAYLOAD_FACT": "1"},
        ):
            result = run([COMPILER, source, "-o", str(rust_path)], env)
            assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                result.stdout + result.stderr), (mode, env, result.stdout, result.stderr)
        for field in ("owner", "argument", "type", "action", "context", "occurrence"):
            result = run([COMPILER, source, "-o", str(rust_path)], {
                "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_PAYLOAD_FACT": field})
            assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                result.stdout + result.stderr), (mode, field, result.stdout, result.stderr)
    finally:
        shutil.rmtree(temp_dir)

    fast_debug = run([COMPILER, "run", "--interp", source])
    assert fast_debug.returncode == 0 and fast_debug.stdout.strip() == expected_output, (
        mode, fast_debug.stdout, fast_debug.stderr)
    traced = run([COMPILER, "run", "--interp", source], {
        "MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS": "1"})
    trace_facts = [line for line in traced.stderr.splitlines()
                   if line.startswith("ROOT_INGRESS_INTERPRETER_COPY_COUNTS ")]
    assert traced.returncode == 0 and traced.stdout.strip() == expected_output and (
        len(trace_facts) == 1 and "authority=fresh" in trace_facts[0] and
        "authorized_independent_calls=0" in trace_facts[0] and
        "implicit_independent_calls=0" in trace_facts[0]), (
            mode, source, traced.stdout, traced.stderr)

    for hook in ("MOSS_TEST_INTERP_DROP_ROOT_INGRESS_FRESH_FACT",
                 "MOSS_TEST_INTERP_DUPLICATE_ROOT_INGRESS_FRESH_FACT",
                 "MOSS_TEST_INTERP_PERTURB_ROOT_INGRESS_FRESH_FACT"):
        result = run([COMPILER, "run", "--interp", source], {hook: "1"})
        assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
            result.stdout + result.stderr), (
                mode, source, hook, result.stdout, result.stderr)
    owned_local = "tests/phase28/r7_010/root_ingress_local_without_copy.moss"
    owned_local_dir = Path(tempfile.mkdtemp(
        prefix="root-ingress-owned-local-", dir=TMP_ROOT))
    try:
        checked = run([COMPILER, "check", owned_local, "--json"], {
            "MOSS_TEST_DUMP_ROOT_INGRESS_PAYLOAD_FACT": "1",
            "MOSS_TEST_DUMP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": "1"})
        response = parse_result(checked)
        assert checked.returncode == 0 and response.get("ok"), (
            mode, owned_local, checked.stdout, checked.stderr)
        assert "ROOT_INGRESS_PAYLOAD_FACT " not in checked.stderr and (
            "ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT " in checked.stderr), (
                mode, owned_local, checked.stderr)
        rust_path = owned_local_dir / "owned_local.rs"
        binary_path = owned_local_dir / "owned_local"
        generated = run([COMPILER, owned_local, "-O0", "-o", str(rust_path)])
        assert generated.returncode == 0, (
            mode, owned_local, generated.stdout, generated.stderr)
        compiled = run(["rustc", "--edition=2021", "-Dwarnings", str(rust_path),
                        "-o", str(binary_path)])
        assert compiled.returncode == 0, (
            mode, owned_local, compiled.stdout, compiled.stderr)
        native = run([str(binary_path)])
        debug = run([COMPILER, "run", "--interp", owned_local])
        assert native.returncode == 0 and native.stdout == "accepted\n" and (
            debug.returncode == 0 and debug.stdout == native.stdout), (
                mode, owned_local, native.stdout, native.stderr,
                debug.stdout, debug.stderr)
    finally:
        shutil.rmtree(owned_local_dir)
    return 1 + 2 + 6 + 1 + 4 + 6 + 1 + 1 + 1 + 1 + 3


def explicit_copy_root_cases(mode, source, expected_output):
    env_mode = os.environ.copy()
    for key in list(env_mode):
        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
            env_mode.pop(key)
    if mode == "shadow":
        env_mode["MOSS_EST"] = "shadow"

    def run(args, extra_env=None):
        env = dict(env_mode)
        env.update(extra_env or {})
        return subprocess.run(
            args, cwd=ROOT, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    checked = run([COMPILER, "check", source, "--json"], {
        "MOSS_TEST_REQUIRE_COPY_FACT": "1"})
    response = parse_result(checked)
    assert checked.returncode == 0 and response.get("ok"), (
        mode, source, checked.stdout, checked.stderr)

    TMP_ROOT.mkdir(exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix="root-ingress-explicit-copy-",
                                     dir=TMP_ROOT))
    try:
        rust_path = temp_dir / "root_copy.rs"
        binary_path = temp_dir / "root_copy"
        generated = run([COMPILER, source, "-o", str(rust_path)])
        assert generated.returncode == 0, (
            mode, source, generated.stdout, generated.stderr)
        rust = rust_path.read_text()
        assert "Moss copy fact call: typed-copy-fact-v1:" in rust, (
            mode, source, "explicit copy provenance missing")
        assert "runtime_invoke(move ||" in rust and "__moss_root_arg_" in rust, (
            mode, source, "explicit copy did not enter exact Root thunk")

        for hook in ("MOSS_TEST_DROP_COPY_FACT",
                     "MOSS_TEST_DUPLICATE_COPY_FACT",
                     "MOSS_TEST_PERTURB_COPY_FACT"):
            result = run([COMPILER, source, "-o", str(rust_path)], {hook: "1"})
            assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                result.stdout + result.stderr), (
                    mode, source, hook, result.stdout, result.stderr)

        for hook in ("MOSS_TEST_CONSUMER_DROP_COPY_FACT",
                     "MOSS_TEST_CONSUMER_DUPLICATE_COPY_FACT",
                     "MOSS_TEST_CONSUMER_PERTURB_COPY_FACT",
                     "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_OWNER",
                     "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID"):
            result = run([COMPILER, source, "-o", str(rust_path)], {hook: "1"})
            assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                result.stdout + result.stderr), (
                    mode, source, hook, result.stdout, result.stderr)

        compiled = run(["rustc", "-O", "-D", "warnings", str(rust_path),
                        "-o", str(binary_path)])
        assert compiled.returncode == 0, (
            mode, source, compiled.stdout, compiled.stderr)
        native = run([str(binary_path)])
        assert native.returncode == 0 and native.stdout.strip() == expected_output, (
            mode, source, native.stdout, native.stderr)
        fast_debug = run([COMPILER, "run", "--interp", source])
        assert fast_debug.returncode == 0 and fast_debug.stdout.strip() == expected_output, (
            mode, source, fast_debug.stdout, fast_debug.stderr)
        traced = run([COMPILER, "run", "--interp", source], {
            "MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS": "1"})
        trace_facts = [line for line in traced.stderr.splitlines()
                       if line.startswith("ROOT_INGRESS_INTERPRETER_COPY_COUNTS ")]
        if source.endswith("phase28_root_nested_copy_payload.moss"):
            assert traced.returncode == 0 and traced.stdout.strip() == expected_output and (
                len(trace_facts) == 2 and
                any("authority=copy" in fact and
                    "explicit_copy_evaluations=1" in fact and
                    "authorized_independent_calls=1" in fact and
                    "implicit_independent_calls=0" in fact
                    for fact in trace_facts) and
                any("authority=primitive" in fact and
                    "explicit_copy_evaluations=0" in fact and
                    "authorized_independent_calls=0" in fact and
                    "implicit_independent_calls=0" in fact
                    for fact in trace_facts)), (mode, source, traced.stdout,
                                                traced.stderr)
        else:
            assert traced.returncode == 0 and traced.stdout.strip() == expected_output and (
                len(trace_facts) == 1 and "authority=copy" in trace_facts[0] and
                "explicit_copy_evaluations=1" in trace_facts[0] and
                "authorized_independent_calls=1" in trace_facts[0] and
                "implicit_independent_calls=0" in trace_facts[0]), (
                    mode, source, traced.stdout, traced.stderr)

        for hook in ("MOSS_TEST_INTERP_DROP_ROOT_INGRESS_COPY_FACT",
                     "MOSS_TEST_INTERP_DUPLICATE_ROOT_INGRESS_COPY_FACT",
                     "MOSS_TEST_INTERP_PERTURB_ROOT_INGRESS_COPY_FACT",
                     "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_OWNER",
                     "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID"):
            result = run([COMPILER, "run", "--interp", source], {hook: "1"})
            assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                result.stdout + result.stderr), (
                    mode, source, hook, result.stdout, result.stderr)
    finally:
        shutil.rmtree(temp_dir)
    return 1 + 3 + 1 + 3 + 1 + 1


def primitive_root_cases(mode, source, expected_output):
    env_mode = os.environ.copy()
    for key in list(env_mode):
        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
            env_mode.pop(key)
    if mode == "shadow":
        env_mode["MOSS_EST"] = "shadow"

    def run(args, extra_env=None):
        env = dict(env_mode)
        env.update(extra_env or {})
        return subprocess.run(
            args, cwd=ROOT, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    checked = run([COMPILER, "check", source, "--json"])
    response = parse_result(checked)
    assert checked.returncode == 0 and response.get("ok"), (
        mode, source, checked.stdout, checked.stderr)
    TMP_ROOT.mkdir(exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix="root-ingress-primitive-",
                                     dir=TMP_ROOT))
    try:
        rust_path = temp_dir / "primitive_root.rs"
        binary_path = temp_dir / "primitive_root"
        generated = run([COMPILER, source, "-o", str(rust_path)])
        assert generated.returncode == 0, (
            mode, source, generated.stdout, generated.stderr)
        rust = rust_path.read_text()
        root_arg_line = next((line for line in rust.splitlines()
                              if "__moss_root_arg_" in line), "")
        assert "let __moss_root_arg_1 = 42_i64;" in root_arg_line, (
            mode, source, root_arg_line)
        compiled = run(["rustc", "-O", "-D", "warnings", str(rust_path),
                        "-o", str(binary_path)])
        assert compiled.returncode == 0, (
            mode, source, compiled.stdout, compiled.stderr)
        native = run([str(binary_path)])
        assert native.returncode == 0 and native.stdout.strip() == expected_output, (
            mode, source, native.stdout, native.stderr)
        traced = run([COMPILER, "run", "--interp", source], {
            "MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS": "1"})
        trace_facts = [line for line in traced.stderr.splitlines()
                       if line.startswith("ROOT_INGRESS_INTERPRETER_COPY_COUNTS ")]
        assert traced.returncode == 0 and traced.stdout.strip() == expected_output and (
            len(trace_facts) == 1 and "authority=primitive" in trace_facts[0] and
            "explicit_copy_evaluations=0" in trace_facts[0] and
            "authorized_independent_calls=0" in trace_facts[0] and
            "implicit_independent_calls=0" in trace_facts[0]), (
                mode, source, traced.stdout, traced.stderr)
    finally:
        shutil.rmtree(temp_dir)
    return 1 + 1 + 1 + 1


def root_argument_disposition_cases(mode):
    env_mode = os.environ.copy()
    for key in list(env_mode):
        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
            env_mode.pop(key)
    if mode == "shadow":
        env_mode["MOSS_EST"] = "shadow"

    def run(args, extra_env=None):
        env = dict(env_mode)
        env.update(extra_env or {})
        return subprocess.run(
            args, cwd=ROOT, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    TMP_ROOT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="root-argument-disposition-",
                                     dir=TMP_ROOT) as temporary:
        directory = Path(temporary)
        baseline_source = ROOT / "tests/phase4_materialized_message.moss"
        baseline_text = baseline_source.read_text()
        post_use_source = directory / "post_use.moss"
        post_use_source.write_text(
            baseline_text.replace("  echo total\n",
                                  "  echo total\n  echo materialized\n"))
        explicit_copy_source = directory / "explicit_copy_reuse.moss"
        explicit_copy_source.write_text(
            baseline_text.replace(
                "message sink.Total(materialized)",
                "message sink.Total(copy(materialized))").replace(
                    "  echo total\n",
                    "  echo total\n  echo materialized\n"))
        nested_source = ROOT / (
            "tests/tooling/fixtures/phase28_root_nested_local_argument_unproven.moss")
        branch_source = directory / "branch_join.moss"
        branch_source.write_text(baseline_text.replace(
            "  materialized = source |> map(double)\n",
            "  flag = true\n"
            "  if flag:\n"
            "    materialized = source |> map(double)\n"
            "  else:\n"
            "    materialized = source |> map(double)\n"))
        loop_source = directory / "loop_carried.moss"
        loop_source.write_text(baseline_text.replace(
            "  total = message sink.Total(materialized)\n  echo total\n",
            "  count = 0\n"
            "  while count < 1:\n"
            "    total = message sink.Total(materialized)\n"
            "    count = count + 1\n"
            "  echo count\n"))

        dumped = run([COMPILER, "check", str(baseline_source), "--json"], {
            "MOSS_TEST_DUMP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": "1"})
        response = parse_result(dumped)
        facts = [line for line in dumped.stderr.splitlines()
                 if line.startswith("ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT ")]
        assert dumped.returncode == 0 and response.get("ok") and len(facts) == 1, (
            mode, dumped.stdout, dumped.stderr)
        for field in ("stmt=", "arena=", "owner=", "message=", "arg=0",
                      "arg_expr=", "binding=main@", "definition=main@",
                      "type=vector[int]", "access=2", "context=main"):
            assert field in facts[0], (mode, field, facts[0])

        for extra_env in (
            {"MOSS_TEST_CHECKER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": "1"},
            {"MOSS_TEST_CHECKER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": "1"},
        ):
            rejected = run([COMPILER, "check", str(baseline_source), "--json"],
                           extra_env)
            assert rejected.returncode != 0 and (
                "Root-ingress argument disposition checker fact/requirement set is incomplete" in
                rejected.stdout + rejected.stderr), (
                    mode, extra_env, rejected.stdout, rejected.stderr)
        for field in ("owner", "message", "argument", "binding", "definition",
                      "type", "context"):
            rejected = run([COMPILER, "check", str(baseline_source), "--json"], {
                "MOSS_TEST_CHECKER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": field})
            assert rejected.returncode != 0 and (
                "Root-ingress argument disposition" in
                rejected.stdout + rejected.stderr), (
                    mode, field, rejected.stdout, rejected.stderr)

        for source in (branch_source, loop_source):
            rejected = run([COMPILER, "check", str(source), "--json"])
            assert rejected.returncode != 0 and (
                "OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN" in
                rejected.stdout + rejected.stderr), (
                    mode, source, rejected.stdout, rejected.stderr)
        nested_rejected = run([COMPILER, "check", str(nested_source), "--json"])
        assert nested_rejected.returncode != 0 and (
            "OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN" in
            nested_rejected.stdout + nested_rejected.stderr), (
                mode, nested_rejected.stdout, nested_rejected.stderr)

        rejected = run([COMPILER, "check", str(post_use_source), "--json"])
        assert rejected.returncode != 0 and (
            "OWNERSHIP_USE_AFTER_CONSUME" in rejected.stdout + rejected.stderr), (
                mode, rejected.stdout, rejected.stderr)

        for opt, suffix in (("-O0", "o0"), ("-O", "optimized")):
            rust_path = directory / f"transfer-{suffix}.rs"
            binary_path = directory / f"transfer-{suffix}"
            generated = run([COMPILER, str(baseline_source), opt,
                             "-o", str(rust_path)])
            assert generated.returncode == 0, (
                mode, opt, generated.stdout, generated.stderr)
            root_arg_line = next((line for line in rust_path.read_text().splitlines()
                                  if "__moss_root_arg_" in line and
                                  "materialized" in line), "")
            root_payload = (root_arg_line.split("let __moss_root_arg_", 1)[-1]
                            .split(" = ", 1)[-1].split(";", 1)[0])
            assert root_arg_line and root_payload == "materialized" and (
                ".clone()" not in root_payload), (mode, opt, root_arg_line)
            for hook in (
                "MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT",
                "MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT",
                "MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT",
                "MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT",
            ):
                failed = run([COMPILER, str(baseline_source), opt,
                              "-o", str(rust_path)], {hook: "1"})
                assert failed.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                    failed.stdout + failed.stderr), (
                        mode, opt, hook, failed.stdout, failed.stderr)
            for field in ("owner", "message", "argument", "binding", "definition",
                          "type", "context"):
                failed = run([COMPILER, str(baseline_source), opt,
                              "-o", str(rust_path)], {
                    "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": field})
                assert failed.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                    failed.stdout + failed.stderr), (
                        mode, opt, field, failed.stdout, failed.stderr)
            compiled = run(["rustc", "--edition=2021", "-Dwarnings",
                            "-C", "opt-level=" + ("0" if opt == "-O0" else "3"),
                            str(rust_path), "-o", str(binary_path)])
            assert compiled.returncode == 0, (
                mode, opt, compiled.stdout, compiled.stderr)
            native = run([str(binary_path)])
            assert native.returncode == 0 and native.stdout == "12\n", (
                mode, opt, native.stdout, native.stderr)
        fast_debug = run([COMPILER, "run", "--interp", str(baseline_source)])
        assert fast_debug.returncode == 0 and fast_debug.stdout == "12\n", (
            mode, fast_debug.stdout, fast_debug.stderr)
        for hook in (
            "MOSS_TEST_INTERPRETER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT",
            "MOSS_TEST_INTERPRETER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT",
            "MOSS_TEST_INTERPRETER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT",
            "MOSS_TEST_INTERPRETER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT",
        ):
            failed = run([COMPILER, "run", "--interp", str(baseline_source)],
                         {hook: "1"})
            assert failed.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                failed.stdout + failed.stderr), (
                    mode, hook, failed.stdout, failed.stderr)
        for field in ("owner", "message", "argument", "binding", "definition",
                      "type", "context"):
            failed = run([COMPILER, "run", "--interp", str(baseline_source)], {
                "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT": field})
            assert failed.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                failed.stdout + failed.stderr), (
                    mode, field, failed.stdout, failed.stderr)

        copy_generated = run([COMPILER, str(explicit_copy_source), "-O0",
                              "-o", str(directory / "copy-reuse.rs")])
        assert copy_generated.returncode == 0, (
            mode, copy_generated.stdout, copy_generated.stderr)
        copy_binary = directory / "copy-reuse"
        copy_compiled = run(["rustc", "--edition=2021", "-Dwarnings",
                             str(directory / "copy-reuse.rs"), "-o",
                             str(copy_binary)])
        assert copy_compiled.returncode == 0, (
            mode, copy_compiled.stdout, copy_compiled.stderr)
        copy_native = run([str(copy_binary)])
        copy_debug = run([COMPILER, "run", "--interp",
                          str(explicit_copy_source)])
        assert copy_native.returncode == 0 and copy_native.stdout == (
            "12\n[2, 4, 6]\n") and copy_debug.returncode == 0 and (
                copy_debug.stdout == copy_native.stdout), (
                    mode, copy_native.stdout, copy_native.stderr,
                    copy_debug.stdout, copy_debug.stderr)
    return 1 + 2 + 7 + 2 + 1 + 2 + 7 + 1


def main():
    payload_cases = (
        (FRESH_PAYLOAD, "Payload { value: 7_i64 }", "fresh: 7"),
        ("tests/string_payload_snapshot.moss", 'MossStr::from("fresh")', "fresh"),
    )
    scenario_count = nested_native_consumer_cases()
    scenario_count += sum(root_argument_disposition_cases(mode)
                          for mode in ("default", "shadow"))
    scenario_count += sum(payload_authority_cases(mode, *fixture)
                         for mode in ("default", "shadow")
                         for fixture in payload_cases)
    explicit_copy_cases = (
        ("tests/tooling/fixtures/builtin_generic_typing/underscore_handler_payload.moss",
         "9"),
        ("tests/phase26_direct_payload.moss", "7 8"),
        ("tests/phase27/conformance/message_argument_binding_use.moss",
         "777777\n777777\n777777\naccepted"),
        ("tests/tooling/fixtures/phase28_root_nested_copy_payload.moss", "9"),
    )
    scenario_count += sum(
        explicit_copy_root_cases(mode, source, expected)
        for mode in ("default", "shadow")
        for source, expected in explicit_copy_cases)
    scenario_count += sum(
        primitive_root_cases(mode,
            "tests/phase28/r7_010/root_ingress_primitive_computed_reply.moss",
            "43")
        for mode in ("default", "shadow"))
    for source, expected_fact_count in FIXTURES:
        baseline = check(source, {"MOSS_TEST_DUMP_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"})
        response = parse_result(baseline)
        assert baseline.returncode == 0 and response.get("ok"), baseline.stdout
        facts = [line for line in baseline.stderr.splitlines()
                 if line.startswith("ROOT_INGRESS_DOMAIN_CAPTURE_FACT ")]
        assert len(facts) == expected_fact_count, (source, facts)
        if source.endswith("phase21_root_nested_ingress.moss"):
            expr_ids = [fact.split("expr=", 1)[1].split()[0] for fact in facts]
            assert len(set(expr_ids)) == expected_fact_count, facts
        for fact in facts:
            for field in ("stmt=", "arena=", "expr=", "receiver=", "context=main",
                          "est_context=main", "instance=main::w", "handler=handler:Worker.",
                          "call_edge=main|handler:Worker."):
                assert field in fact, (source, field, fact)
        if source.endswith("phase28_root_same_line_messages.moss"):
            assert len(set(facts)) == 2, (source, facts)
            assert len({part.split("expr=", 1)[1].split()[0] for part in facts}) == 2, facts
            edge_occurrences = {
                fact.split("call_edge=", 1)[1].split("|receiver=", 1)[0].rsplit(":", 1)[1]
                for fact in facts
            }
            assert len(edge_occurrences) == 2, facts
        scenario_count += 1

        dropped = check(source, {"MOSS_TEST_DROP_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"})
        assert dropped.returncode != 0, (source, dropped.stdout)
        assert "Root-ingress domain capture checker fact/requirement set is incomplete" in (
            dropped.stdout + dropped.stderr)
        scenario_count += 1

        duplicate = check(source, {
            "MOSS_TEST_DUPLICATE_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"
        })
        assert duplicate.returncode != 0, (source, duplicate.stdout)
        assert "Root-ingress domain capture checker fact/requirement set is incomplete" in (
            duplicate.stdout + duplicate.stderr)
        scenario_count += 1

        missing_edge = check(source, {
            "MOSS_TEST_DROP_ROOT_INGRESS_MESSAGE_EDGE": "1"
        })
        assert missing_edge.returncode != 0, (source, missing_edge.stdout)
        assert "exact checked Root-ingress message call edge" in missing_edge.stdout
        scenario_count += 1
        duplicate_edge = check(source, {
            "MOSS_TEST_DUPLICATE_ROOT_INGRESS_MESSAGE_EDGE": "1"
        })
        assert duplicate_edge.returncode != 0, (source, duplicate_edge.stdout)
        assert "duplicate exact Root-ingress message call edges" in duplicate_edge.stdout
        scenario_count += 1
        perturbed_edge = check(source, {
            "MOSS_TEST_PERTURB_ROOT_INGRESS_MESSAGE_EDGE": "1"
        })
        assert perturbed_edge.returncode != 0, (source, perturbed_edge.stdout)
        assert "exact checked Root-ingress message call edge" in perturbed_edge.stdout
        scenario_count += 1

        for field in ("instance", "route", "handler", "call-edge",
                      "occurrence", "context"):
            corrupted = check(source, {
                "MOSS_TEST_PERTURB_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": field
            })
            assert corrupted.returncode != 0, (source, field, corrupted.stdout)
            assert "Root-ingress domain capture" in (
                corrupted.stdout + corrupted.stderr), corrupted.stdout + corrupted.stderr
            scenario_count += 1

    alias = check(ALIAS_REJECTED)
    alias_response = parse_result(alias)
    assert alias.returncode != 0 and not alias_response.get("ok"), alias.stdout
    assert "domain handles cannot be used as ordinary values or payloads" in (
        alias.stdout + alias.stderr)
    scenario_count += 1

    unknown = check(UNKNOWN_HANDLER)
    unknown_response = parse_result(unknown)
    assert unknown.returncode != 0 and not unknown_response.get("ok"), unknown.stdout
    assert unknown_response.get("error", {}).get("code") != "MOSS_INTERNAL_INVARIANT", unknown.stdout
    assert "no message handler" in unknown_response.get("error", {}).get("message", "").lower(), unknown.stdout
    scenario_count += 1

    print(f"Phase 28 Root-ingress domain capture authority: {scenario_count} scenarios PASS")


if __name__ == "__main__":
    main()
