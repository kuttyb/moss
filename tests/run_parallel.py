#!/usr/bin/env python3
"""Run the complete Moss v0.1 regression suite concurrently.

Standard-library Python implementation using ThreadPoolExecutor for DAG-scheduled
parallel testing, isolated workspaces, stdout/stderr capture, and timing metrics.
"""
from __future__ import annotations

import argparse
from concurrent.futures import Future, ThreadPoolExecutor, wait, FIRST_COMPLETED
from dataclasses import dataclass, field
import fnmatch
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import threading
import time


REPO = Path(__file__).resolve().parents[1]
GIT_LOCK = threading.Lock()
DEFAULT_CHECK_TIMEOUT_SECONDS = 600.0
CHECK_TIMEOUT_OVERRIDES = {
    # This checked release-mode semantics shard is the suite's longest known
    # check, so leave extra room beyond the generous default.
    "check_phase20_release_semantics": 900.0,
    # The tooling shard runs live LLDB and DAP sessions; allow several minutes
    # for them while still bounding a debugger that never resumes.
    "shard_phase5_tooling": 300.0,
}


@dataclass
class Job:
    name: str
    cmd_template: list[str]
    cwd: Path = field(default_factory=lambda: REPO)
    dependencies: list[str] = field(default_factory=list)
    use_opt_compiler: bool = False
    env_overrides: dict[str, str] = field(default_factory=dict)
    isolate_worktree: bool = False
    exclusive: bool = False
    timeout_seconds: float = 600.0


@dataclass
class JobResult:
    job: Job
    returncode: int
    duration: float
    stdout: str
    stderr: str
    output_dir: Path


def result_tail(result: JobResult, line_count: int = 20) -> list[str]:
    """Return the last output lines for a failed check, stdout then stderr."""
    combined = "\n".join(part for part in (result.stdout, result.stderr) if part)
    return combined.strip().splitlines()[-line_count:]


def _kill_process_tree(proc: subprocess.Popen[str]) -> None:
    """Kill a check and every descendant in its isolated process group."""
    if os.name == "posix":
        try:
            os.killpg(proc.pid, 9)
        except ProcessLookupError:
            pass
    else:
        proc.kill()
    proc.wait()


# All individual standalone Python regressions from tests/run.sh
TOOLING_TESTS: list[tuple] = [
    ("check_run_parallel_timeout", [sys.executable, "tests/tooling/check_run_parallel_timeout.py"]),
    ("check_make_incremental_dependencies", [sys.executable, "tests/tooling/check_make_incremental_dependencies.py"]),
    ("check_retired_syntax", [sys.executable, "tests/tooling/check_retired_syntax.py", "--self-test"]),
    ("check_agent_skills", [sys.executable, "tests/tooling/check_agent_skills.py", "{compiler}"]),
    ("check_agent_feedback", [sys.executable, "tests/tooling/check_agent_feedback.py", "--self-test"]),
    ("check_phase21_shared_abi", [sys.executable, "tests/tooling/check_phase21_shared_abi.py", "{out}/phase21-shared-abi"]),
    ("check_phase21_source_free_abi", [sys.executable, "tests/tooling/check_phase21_source_free_abi.py", "{compiler}", "{out}/phase21-source-free"]),
    ("check_phase21_abi_v9_migration", [sys.executable, "tests/tooling/check_phase21_abi_v9_migration.py", "{compiler}"]),
    ("check_phase21_source_free_cache_binding", [sys.executable, "tests/tooling/check_phase21_source_free_cache_binding.py", "{compiler}"]),
    ("check_phase21_source_free_proof_edges", [sys.executable, "tests/tooling/check_phase21_source_free_proof_edges.py", "{compiler}"]),
    ("check_phase21_source_free_standard_write_recovery", [sys.executable, "tests/tooling/check_phase21_source_free_standard_write_recovery.py", "{compiler}"]),
    ("check_phase21_standard_write_order", [sys.executable, "tests/tooling/check_phase21_standard_write_order.py", "{compiler}"]),
    ("check_phase21_addendum_a_c", [sys.executable, "tests/tooling/check_phase21_addendum_a_c.py", "{compiler}"]),
    ("check_phase21_original_af", [sys.executable, "tests/tooling/check_phase21_original_af.py", "{compiler}"]),
    ("check_phase21_imported_effects", [sys.executable, "tests/tooling/check_phase21_imported_effects.py", "{compiler}", "{out}/phase21-imported-effects"]),
    ("check_phase21_sourcefree_noop_warning", [sys.executable, "tests/tooling/check_phase21_sourcefree_noop_warning.py", "{compiler}"]),
    ("repro_phase21_mossi_pattern_forgery", [sys.executable, "tests/tooling/repro_phase21_mossi_pattern_forgery.py", "{compiler}"]),
    ("check_phase21_abi_v9_external_cache", [sys.executable, "tests/tooling/check_phase21_abi_v9_external_cache.py", "{compiler}"]),
    ("check_phase20_cross_crate_range_abi", [sys.executable, "tests/tooling/check_phase20_cross_crate_range_abi.py", "{compiler}"]),
    ("check_phase21_on_fail_capture", [sys.executable, "tests/tooling/check_phase21_on_fail_capture.py", "{compiler}"]),
    ("check_phase21_recovery_semantics", [sys.executable, "tests/tooling/check_phase21_recovery_semantics.py", "{compiler}"]),
    ("check_phase21_peer_recovery", [sys.executable, "tests/tooling/check_phase21_peer_recovery.py", "{compiler}"]),
    ("check_phase21_peer_warnings", [sys.executable, "tests/tooling/check_phase21_peer_warnings.py", "{compiler}"]),
    ("check_phase21_unary_not_effects", [sys.executable, "tests/tooling/check_phase21_unary_not_effects.py", "{compiler}"]),
    ("check_phase21_field_projection_effects", [sys.executable, "tests/tooling/check_phase21_field_projection_effects.py", "{compiler}"]),
    ("check_phase21_early_condition_raise", [sys.executable, "tests/tooling/check_phase21_early_condition_raise.py", "{compiler}"]),
    ("check_phase21_recover_reinit", [sys.executable, "tests/tooling/check_phase21_recover_reinit.py", "{compiler}"]),
    ("check_phase21_expression_ownership_edges", [sys.executable, "tests/tooling/check_phase21_expression_ownership_edges.py", "{compiler}"]),
    ("check_phase21_raising_iterators", [sys.executable, "tests/tooling/check_phase21_raising_iterators.py", "{compiler}"]),
    ("check_phase21_early_placement_proof", [sys.executable, "tests/tooling/check_phase21_early_placement_proof.py", "{compiler}"]),
    ("check_phase21_method_noop_warning", [sys.executable, "tests/tooling/check_phase21_method_noop_warning.py", "{compiler}"]),
    ("check_phase21_recovery_native", [sys.executable, "tests/tooling/check_phase21_recovery_native.py", "{compiler}"]),
    ("check_phase21_root_totality", [sys.executable, "tests/tooling/check_phase21_root_totality.py", "{compiler}"]),
    ("check_phase21_root_expression_ingress", [sys.executable, "tests/tooling/check_phase21_root_expression_ingress.py", "{compiler}"]),
    ("check_phase21_absolute_root_boundary", [sys.executable, "tests/tooling/check_phase21_absolute_root_boundary.py", "{compiler}"]),
    ("check_phase21_handler_failure_native", [sys.executable, "tests/tooling/check_phase21_handler_failure_native.py", "{compiler}"]),
    ("check_phase21_fileio_errors", [sys.executable, "tests/tooling/check_phase21_fileio_errors.py"]),
    ("check_phase21_fileio_close_full", [sys.executable, "tests/tooling/check_phase21_fileio_close_full.py", "{compiler}"]),
    ("check_phase21_fileio_root_cleanup", [sys.executable, "tests/tooling/check_phase21_fileio_root_cleanup.py", "{compiler}"]),
    ("check_phase21_lock_fileio_adversarial", [sys.executable, "tests/tooling/check_phase21_lock_fileio_adversarial.py", "{compiler}"]),
    ("check_phase21_float_ieee", [sys.executable, "tests/tooling/check_phase21_float_ieee.py", "{compiler}"]),
    ("check_phase21_numeric_parse", [sys.executable, "tests/tooling/check_phase21_numeric_parse.py", "{compiler}"]),
    ("check_phase21_builtin_error_contract", [sys.executable, "tests/tooling/check_phase21_builtin_error_contract.py", "{compiler}"]),
    ("check_phase21_standard_error_namespace", [sys.executable, "tests/tooling/check_phase21_standard_error_namespace.py", "{compiler}"]),
    ("check_phase21_borrowed_view_write", [sys.executable, "tests/tooling/check_phase21_borrowed_view_write.py", "{compiler}"]),
    ("check_generic_specialization", [sys.executable, "tests/tooling/check_generic_specialization.py", "{compiler}"]),
    ("check_phase24_imported_generic_expressions", [sys.executable, "tests/tooling/check_phase24_imported_generic_expressions.py", "{compiler}"]),
    ("check_phase24_same_module_type_resolution", [sys.executable, "tests/tooling/check_phase24_same_module_type_resolution.py", "{compiler}"]),
    ("check_phase24_bitwise_operations", [sys.executable, "tests/tooling/check_phase24_bitwise_operations.py", "{compiler}"]),
    ("check_phase25_recursive_layout", [sys.executable, "tests/tooling/check_phase25_recursive_layout.py", "{compiler}"]),
    ("check_phase26_w1_parse_cache", [sys.executable, "tests/tooling/check_phase26_w1_parse_cache.py", "{compiler}"]),
    ("check_phase26_w2_inference_worklist", [sys.executable, "tests/tooling/check_phase26_w2_inference_worklist.py", "{compiler}"]),
    ("check_phase26_inference_verify_modes", [sys.executable, "tests/tooling/check_phase26_inference_verify_modes.py", "{compiler}"]),
    ("check_phase26_1_f1_interface_rules", [sys.executable, "tests/tooling/check_phase26_1_f1_interface_rules.py", "{compiler}"]),
    ("check_phase26_1_1_x1_v9", [sys.executable, "tests/tooling/check_phase26_1_1_x1.py", "{compiler}"]),
    ("check_phase26_1_1_x1_v10", [sys.executable, "tests/tooling/check_phase26_1_1_x1_v10.py", "{compiler}"]),
    ("check_phase26_1_1_x2", [sys.executable, "tests/tooling/check_phase26_1_1_x2.py", "{compiler}"]),
    ("check_phase26_1_1_x4", [sys.executable, "tests/tooling/check_phase26_1_1_x4.py", "{compiler}"]),
    ("check_phase26_1_g1_fingerprints", [sys.executable, "tests/tooling/check_phase26_1_g1_fingerprints.py", "{compiler}"]),
    ("check_phase26_1_c1_scope", [sys.executable, "tests/tooling/check_phase26_1_c1_scope.py", "{compiler}"]),
    ("check_phase26_1_c1_corpus_cases", [sys.executable, "tests/tooling/check_phase26_1_c1_corpus_cases.py", "{compiler}"]),
    ("check_phase26_1_t1_generic_imports", [sys.executable, "tests/tooling/check_phase26_1_t1_generic_imports.py", "{compiler}"]),
    ("check_phase26_0_1_c3_parse_cache", [sys.executable, "tests/tooling/check_phase26_0_1_c3_parse_cache.py", "{compiler}"]),
    ("check_phase25_type_ir", [sys.executable, "tests/tooling/check_phase25_type_ir.py"]),
    ("check_phase25_a3_specialization", [sys.executable, "tests/tooling/check_phase25_a3_specialization.py", "{compiler}"]),
    ("check_phase25_conformance", [sys.executable, "tests/tooling/check_phase25_conformance.py", "{compiler}"]),
    ("check_phase25_nominal_identity", [sys.executable, "tests/tooling/check_phase25_nominal_identity.py", "{compiler}"]),
    ("check_phase25_field_visibility", [sys.executable, "tests/tooling/check_phase25_field_visibility.py", "{compiler}"]),
    ("check_phase25_team_a_semantics", [sys.executable, "tests/tooling/check_phase25_team_a_semantics.py", "{compiler}"]),
    ("check_phase25_typed_semantic_api", [sys.executable, "tests/tooling/check_phase25_typed_semantic_api.py", "{compiler}"]),
    ("check_phase25_checked_body_ir", [sys.executable, "tests/tooling/check_phase25_checked_body_ir.py", "{compiler}"]),
    ("check_phase25_r2_wp1_call_chain", [sys.executable, "tests/tooling/check_phase25_r2_wp1_call_chain.py", "{compiler}"]),
    ("check_phase25_r2_wp2_member", [sys.executable, "tests/tooling/check_phase25_r2_wp2_member.py", "{compiler}"]),
    ("check_phase25_r2_wp3_nominal", [sys.executable, "tests/tooling/check_phase25_r2_wp3_nominal.py", "{compiler}"]),
    ("check_phase25_r2_wp4_oracle_inventory", [sys.executable, "tests/tooling/check_phase25_r2_wp4_oracle_inventory.py"]),
    ("check_runtime", [sys.executable, "tests/phase25/runtime/check_runtime.py"]),
    ("check_shared_string_linkage", [sys.executable, "tests/phase25/runtime/check_shared_string_linkage.py"]),
    ("sdk_foundations", [sys.executable, "sdk/foundations/acceptance/run_all.py"]),
    ("sdk_text_encoding", [sys.executable, "sdk/text-encoding-acceptance/run_all.py"]),
    ("sdk_adv_data_cbor", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "cbor"]),
    ("sdk_adv_data_oracle_debug", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "oracle-debug"]),
    ("sdk_adv_data_oracle_release", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "oracle-release"]),
    ("sdk_adv_data_oracle_fast_debug", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "oracle-fast-debug"]),
    ("sdk_adv_data_margo_run", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "margo-run"]),
    ("sdk_adv_data_margo_release", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "margo-release"]),
    ("sdk_adv_data_margo_debug", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "margo-debug"]),
    ("sdk_adv_data_source_free", [sys.executable, "sdk/advanced-data-acceptance/run_all.py", "--step", "source-free"]),
    ("check_phase24_adversarial_architect", [sys.executable, "tests/tooling/check_phase24_adversarial_architect.py", "{compiler}"]),
    ("check_phase25_g1_regressions", [sys.executable, "tests/tooling/check_phase25_g1_regressions.py", "{compiler}"]),
    ("check_phase25_g4_unicode", [sys.executable, "tests/tooling/check_phase25_g4_unicode.py", "{compiler}"]),
    ("check_phase25_g3_ordering", [sys.executable, "tests/tooling/check_phase25_g3_ordering.py", "{compiler}"]),
    ("check_phase25_l7_repairs", [sys.executable, "tests/tooling/check_phase25_l7_repairs.py", "{compiler}"]),
    ("check_phase25_indexed_string", [sys.executable, "tests/tooling/check_phase25_indexed_string.py", "{compiler}"]),
    ("check_phase25_s2_char_string", [sys.executable, "tests/tooling/check_phase25_s2_char_string.py", "{compiler}"]),
    ("check_phase25_s1_checker_authority", [sys.executable, "tests/tooling/check_phase25_s1_checker_authority.py", "{compiler}"]),
    ("check_phase25_s3_key_protocol", [sys.executable, "tests/tooling/check_phase25_s3_key_protocol.py", "{compiler}"]),
    ("check_phase25_s4_fast_debug_adversarial", [sys.executable, "tests/tooling/check_phase25_s4_fast_debug_adversarial.py", "{compiler}"]),
    ("check_phase25_sc_safe_arithmetic", [sys.executable, "tests/tooling/check_phase25_sc_safe_arithmetic.py", "{compiler}"]),
    ("check_phase25_sa_numeric_literals", [sys.executable, "tests/tooling/check_phase25_sa_numeric_literals.py", "{compiler}"]),
    ("check_phase25_sb_check_display", [sys.executable, "tests/tooling/check_phase25_sb_check_display.py", "{compiler}"]),
    ("check_phase25_4_performance_baseline", [sys.executable, "tests/tooling/check_phase25_4_performance_baseline.py", "{compiler}"]),
    ("phase25_key_protocol_fixtures", [sys.executable, "tests/phase25/key_protocol/run_fixtures.py", "--moss", "{compiler}"]),
    ("check_builtin_generic_typing", [sys.executable, "tests/tooling/check_builtin_generic_typing.py", "{compiler}"]),
    ("check_swarm_feedback", [sys.executable, "tools/check_swarm_feedback.py", "--self-test"]),
    ("check_swarm_issues", [sys.executable, "tools/check_swarm_issues.py", "--self-test"]),
    ("check_agent_benchmark", [sys.executable, "tests/tooling/check_agent_benchmark.py", "{compiler}"]),
    ("check_agent_baseline", [sys.executable, "tests/tooling/check_agent_baseline.py", "{compiler}"]),
    ("check_agent_baseline_analysis", [sys.executable, "tests/tooling/check_agent_baseline_analysis.py"]),
    ("check_phase22_3_ab", [sys.executable, "tests/tooling/check_phase22_3_ab.py", "{compiler}"]),
    ("check_phase20_executor_fileio", [sys.executable, "tests/tooling/check_phase20_executor_fileio.py", "{compiler}", "{out}/phase20_executor_fileio"]),
    ("check_phase21c_branch_errors", [sys.executable, "tests/tooling/check_phase21c_branch_errors.py", "{compiler}", "{out}/phase21c_branch_errors"]),
    ("check_phase21_peer_effects", [sys.executable, "tests/tooling/check_phase21_peer_effects.py", "{compiler}"]),
    ("check_phase21_peer_open_pipelines", [sys.executable, "tests/tooling/check_phase21_peer_open_pipelines.py", "{compiler}"]),
    ("check_phase21_pipeline_try_placeholder", [sys.executable, "tests/tooling/check_phase21_pipeline_try_placeholder.py", "{compiler}"]),
    ("check_phase21_unary_not_speculation", [sys.executable, "tests/tooling/check_phase21_unary_not_speculation.py", "{compiler}"]),
    ("check_phase21_chunk_fileio_raise", [sys.executable, "tests/tooling/check_phase21_chunk_fileio_raise.py", "{compiler}"]),
    ("check_phase20_domain_field_physical", [sys.executable, "tests/tooling/check_phase20_domain_field_physical.py", "{compiler}", "{out}/phase20_domain_physical"]),
    ("check_phase20_r5_fair", [sys.executable, "tests/tooling/check_phase20_r5_fair.py", "{out}/phase20_r5_fair"]),
    ("check_teaching_diagnostics", [sys.executable, "tests/tooling/check_teaching_diagnostics.py", "{compiler}"]),
    ("check_no_internal_type_leak", [sys.executable, "tests/tooling/check_no_internal_type_leak.py", "{compiler}"]),
    ("check_semantic_queries", [sys.executable, "tests/tooling/check_semantic_queries.py", "{compiler}"]),
    ("check_phase225_semantic_ide", [sys.executable, "tests/tooling/check_phase225_semantic_ide.py", "{compiler}"]),
    ("check_phase15_15_string_api", [sys.executable, "tests/tooling/check_phase15_15_string_api.py"]),
    ("check_phase24_string_concat", [sys.executable, "tests/tooling/check_phase24_string_concat.py"]),
    ("check_phase25_char_string_sdk_packages", [sys.executable, "tests/tooling/check_phase25_char_string_sdk_packages.py", "{compiler}", "{out}/phase25-char-string-sdk"]),
    ("check_swarm_003_formatter", [sys.executable, "tests/tooling/check_swarm_003_formatter.py", "{compiler}"]),
    ("check_swarm_032_formatter_parentheses", [sys.executable, "tests/tooling/check_swarm_032_formatter_parentheses.py", "{compiler}"]),
    ("check_formatter_indexing", [sys.executable, "tests/tooling/check_formatter_indexing.py", "{compiler}"]),
    ("check_swarm_059_project_diagnostics", [sys.executable, "tests/tooling/check_swarm_059_project_diagnostics.py", "{compiler}"]),
    ("check_phase15_7_formatter_convergence", [sys.executable, "tests/tooling/check_phase15_7_formatter_convergence.py", "{compiler}"]),
    ("check_phase15_8_cross_package_fast_debug", [sys.executable, "tests/tooling/check_phase15_8_cross_package_fast_debug.py", "{compiler}"]),
    ("check_phase15_9_cross_package_specialization", [sys.executable, "tests/tooling/check_phase15_9_cross_package_specialization.py", "{compiler}"]),
    ("check_swarm_039_project_fast_debug_tests", [sys.executable, "tests/tooling/check_swarm_039_project_fast_debug_tests.py"]),
    ("check_swarm_059_diagnostic_provenance", [sys.executable, "tests/tooling/check_swarm_059_diagnostic_provenance.py"]),
    ("check_swarm_060_qualified_rename", [sys.executable, "tests/tooling/check_swarm_060_qualified_rename.py"]),
    ("check_phase20_fileio_semantics", [sys.executable, "tests/tooling/check_phase20_fileio_semantics.py", "{compiler}", "{out}/phase20_fileio_semantics"]),
    ("check_phase20_fileio_runtime", [sys.executable, "tests/tooling/check_phase20_fileio_runtime.py", "{compiler}", "{out}/phase20_fileio"]),
    ("phase20_review_probes", [sys.executable, "tests/tooling/phase20_review/run_review_probes.py", "{compiler}", "--strict"]),
    ("check_phase20_release_semantics", [sys.executable, "tests/tooling/check_phase20_release_semantics.py", "{compiler}"]),
    ("check_phase20_doc_examples", [sys.executable, "tests/tooling/check_phase20_doc_examples.py"]),
]

SHELL_SHARDS: list[tuple] = [
    ("shard_negative", ["tests/shards/shard_negative.sh", "{compiler}", "{out}"]),
    ("shard_abi_v10_units", ["tests/shards/shard_abi_v10_units.sh", "{compiler}", "{out}"]),
    ("shard_showcase", ["tests/shards/shard_showcase.sh", "{compiler}", "{out}"]),
    ("shard_swarm_cases", ["tests/shards/shard_swarm_cases.sh", "{compiler}", "{out}"]),
    ("shard_phase4_diff", ["tests/shards/shard_phase4_diff.sh", "{compiler}", "{out}"]),
    ("shard_phase46_opt", ["tests/shards/shard_phase46_opt.sh", "{compiler}", "{out}"]),
    ("shard_concurrency", ["tests/shards/shard_concurrency.sh", "{compiler}", "{out}"]),
    ("shard_phase25_int_payloads", ["tests/shards/shard_phase25_int_payloads.sh", "{compiler}", "{out}"]),
    ("shard_enum_modules", ["tests/shards/shard_enum_modules.sh", "{compiler}", "{out}"]),
    ("shard_fast_debug", ["tests/shards/shard_fast_debug.sh", "{compiler}", "{out}"]),
    ("shard_phase5_tooling", ["tests/shards/shard_phase5_tooling.sh", "{compiler}", "{out}"], ["shard_phase4_diff"]),
    ("shard_phase106_runtime", ["tests/shards/shard_phase106_runtime.sh", "{compiler}", "{out}"]),
]


def build_manifest() -> list[Job]:
    jobs: list[Job] = []
    lightweight = {
        "check_make_incremental_dependencies",
        "check_retired_syntax",
        "check_agent_skills",
        "check_agent_feedback",
        "check_phase21_unary_not_effects",
        "check_phase21_field_projection_effects",
        "check_phase21_raising_iterators",
        "check_phase21_method_noop_warning",
        "check_phase25_recursive_layout",
        "check_phase25_type_ir",
        "check_phase25_typed_semantic_api",
        "check_phase25_nominal_identity",
        "check_phase25_field_visibility",
        "check_phase25_s1_checker_authority",
        "check_teaching_diagnostics",
        "check_semantic_queries",
        "check_phase225_semantic_ide",
    }
    for item in TOOLING_TESTS:
        name, cmd = item[0], item[1]
        jobs.append(Job(name=name, cmd_template=cmd,
                        use_opt_compiler=name not in lightweight,
                        isolate_worktree=True,
                        exclusive=name == "check_phase25_4_performance_baseline",
                        timeout_seconds=CHECK_TIMEOUT_OVERRIDES.get(
                            name, DEFAULT_CHECK_TIMEOUT_SECONDS)))

    for item in SHELL_SHARDS:
        name, cmd = item[0], item[1]
        deps = item[2] if len(item) > 2 else []
        jobs.append(Job(name=name, cmd_template=cmd, dependencies=deps,
                        use_opt_compiler=True, isolate_worktree=True,
                        timeout_seconds=CHECK_TIMEOUT_OVERRIDES.get(
                            name, DEFAULT_CHECK_TIMEOUT_SECONDS)))

    return jobs


def run_single_job(
    job: Job,
    compiler_dev: Path,
    compiler_opt: Path,
    output_base: Path,
) -> JobResult:
    compiler = compiler_opt if job.use_opt_compiler else compiler_dev
    job_out = output_base / job.name
    job_out.mkdir(parents=True, exist_ok=True)
    margo_home = job_out / "margo_home"
    margo_home.mkdir(exist_ok=True)
    job_tmp = job_out / "tmp"
    job_tmp.mkdir(parents=True, exist_ok=True)

    worktree_dir: Path | None = None
    cwd = job.cwd
    if job.isolate_worktree:
        worktree_dir = job_out / "wt"
        with GIT_LOCK:
            if worktree_dir.exists():
                subprocess.run(
                    ["git", "worktree", "remove", "--force", str(worktree_dir)],
                    cwd=REPO,
                    check=False,
                    capture_output=True,
                )
            subprocess.run(
                ["git", "worktree", "add", "--detach", str(worktree_dir), "HEAD"],
                cwd=REPO,
                check=True,
                capture_output=True,
            )
            # Overlay modified tracked and untracked files into the worktree
            diff_proc = subprocess.run(
                ["git", "diff", "--name-only", "HEAD"],
                cwd=REPO, capture_output=True, text=True, check=True
            )
            untracked_proc = subprocess.run(
                ["git", "ls-files", "--others", "--exclude-standard"],
                cwd=REPO, capture_output=True, text=True, check=True
            )
            for rel in diff_proc.stdout.splitlines() + untracked_proc.stdout.splitlines():
                src_file = REPO / rel
                dst_file = worktree_dir / rel
                if src_file.is_file():
                    dst_file.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(src_file, dst_file)
            # Nested Margo and SDK runners often resolve ROOT/moss and expect
            # margo beside the compiler. A local launcher preserves both paths
            # while every job executes the same selected compiler read-only.
            (worktree_dir / "moss").write_text(
                f"#!/bin/sh\nexec {shlex.quote(str(compiler))} \"$@\"\n",
                encoding="utf-8",
            )
            (worktree_dir / "moss").chmod(0o755)
            (worktree_dir / "tmp").mkdir(exist_ok=True)
        if job.cwd == REPO:
            cwd = worktree_dir
        elif job.cwd.is_relative_to(REPO):
            cwd = worktree_dir / job.cwd.relative_to(REPO)

    job_compiler = (worktree_dir / "moss") if worktree_dir else compiler
    cmd = [
        arg.format(compiler=str(job_compiler), out=str(job_out))
        for arg in job.cmd_template
    ]

    env = os.environ.copy()
    env.update(
        MOSS=str(job_compiler),
        MARGO_HOME=str(margo_home),
        TMPDIR=str(job_tmp),
        PYTHONDONTWRITEBYTECODE="1",
        test_build=str(job_out),
    )
    env.update(job.env_overrides)

    start_time = time.monotonic()
    stdout_file = job_out / "stdout.log"
    stderr_file = job_out / "stderr.log"

    try:
        proc = subprocess.Popen(
            cmd,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=(os.name == "posix"),
        )
        timed_out = False
        try:
            stdout, stderr = proc.communicate(timeout=job.timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            _kill_process_tree(proc)
            stdout, stderr = proc.communicate()
        duration = time.monotonic() - start_time
        if timed_out:
            timeout_note = (
                f"check {job.name} exceeded its {job.timeout_seconds:g}s timeout; "
                "the process tree was killed."
            )
            stderr = "\n".join(part for part in (stderr, timeout_note) if part)
            returncode = 124
        else:
            returncode = proc.returncode
        stdout_file.write_text(stdout, encoding="utf-8")
        stderr_file.write_text(stderr, encoding="utf-8")
        return JobResult(
            job=job,
            returncode=returncode,
            duration=duration,
            stdout=stdout,
            stderr=stderr,
            output_dir=job_out,
        )
    except Exception as exc:
        duration = time.monotonic() - start_time
        err_msg = f"Execution exception: {exc}\n"
        stderr_file.write_text(err_msg, encoding="utf-8")
        return JobResult(
            job=job,
            returncode=1,
            duration=duration,
            stdout="",
            stderr=err_msg,
            output_dir=job_out,
        )
    finally:
        if worktree_dir and worktree_dir.exists():
            with GIT_LOCK:
                subprocess.run(
                    ["git", "worktree", "remove", "--force", str(worktree_dir)],
                    cwd=REPO,
                    check=False,
                    capture_output=True,
                )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Moss regression suite in parallel.")
    parser.add_argument(
        "--jobs", "-j",
        type=int,
        default=os.cpu_count() or 16,
        help="Number of concurrent test worker threads (default: CPU cores)",
    )
    parser.add_argument(
        "--compiler-dev",
        type=Path,
        default=REPO / "build" / "compiler" / "moss-dev",
        help="Path to development Moss compiler (-O0)",
    )
    parser.add_argument(
        "--compiler-opt",
        type=Path,
        default=REPO / "build" / "compiler" / "moss-opt",
        help="Path to optimized Moss compiler (-O2)",
    )
    parser.add_argument(
        "--filter", "-k",
        type=str,
        default=None,
        help="Only run jobs whose names match this pattern (e.g. 'shard_*')",
    )
    parser.add_argument(
        "--exclude-job",
        action="append",
        default=[],
        help="Explicitly omit a named job; every omission is reported in the summary",
    )
    parser.add_argument(
        "--output-base",
        type=Path,
        default=REPO / "build" / "tests" / "parallel",
        help="Base directory for isolated job build outputs",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List all jobs in the suite and exit",
    )
    args = parser.parse_args()
    args.output_base = args.output_base.resolve()
    if not args.output_base.is_relative_to(REPO):
        print("Error: --output-base must stay within this checkout.", file=sys.stderr)
        return 1

    manifest = build_manifest()
    jobs_by_name = {job.name: job for job in manifest}
    if args.jobs < 1 or len(jobs_by_name) != len(manifest):
        print("Error: --jobs must be positive and job names must be unique.", file=sys.stderr)
        return 1
    for job in manifest:
        if any(dep not in jobs_by_name for dep in job.dependencies):
            print(f"Error: {job.name} has a missing dependency.", file=sys.stderr)
            return 1

    if args.list:
        print(f"Total jobs in manifest: {len(manifest)}")
        for job in manifest:
            deps = f" (deps: {', '.join(job.dependencies)})" if job.dependencies else ""
            print(f"  {job.name}{deps}")
        return 0

    compiler_dev = args.compiler_dev.resolve()
    compiler_opt = args.compiler_opt.resolve()
    for variant, compiler in (("development", compiler_dev),
                              ("optimized", compiler_opt)):
        if not compiler.is_file() or not os.access(compiler, os.X_OK):
            print(f"Error: required {variant} compiler {compiler} is unavailable.",
                  file=sys.stderr)
            return 1

    # Filter jobs if requested
    selected_jobs = manifest
    if args.filter:
        selected_names = {j.name for j in manifest if fnmatch.fnmatch(j.name, args.filter)}
        if not selected_names:
            print(f"No jobs matched filter: {args.filter}", file=sys.stderr)
            return 1
        # A filtered DAG run still executes every prerequisite it needs.
        frontier = list(selected_names)
        while frontier:
            for dependency in jobs_by_name[frontier.pop()].dependencies:
                if dependency not in selected_names:
                    selected_names.add(dependency)
                    frontier.append(dependency)
        selected_jobs = [job for job in manifest if job.name in selected_names]

    unknown_exclusions = sorted(set(args.exclude_job) - set(jobs_by_name))
    if unknown_exclusions:
        print(f"Unknown excluded job(s): {', '.join(unknown_exclusions)}", file=sys.stderr)
        return 1
    if args.exclude_job:
        excluded_names = set(args.exclude_job)
        selected_jobs = [job for job in selected_jobs if job.name not in excluded_names]

    args.output_base.mkdir(parents=True, exist_ok=True)
    (REPO / "tmp").mkdir(parents=True, exist_ok=True)

    print("=" * 72)
    print("MOSS PARALLEL REGRESSION SUITE")
    print(f"  Concurrency:   {args.jobs} worker threads")
    print(f"  Selected jobs: {len(selected_jobs)} of {len(manifest)}")
    if args.exclude_job:
        print(f"  Explicitly excluded: {', '.join(sorted(set(args.exclude_job)))}")
    print(f"  Dev compiler:  {compiler_dev.relative_to(REPO)}")
    print(f"  Opt compiler:  {compiler_opt.relative_to(REPO)}")
    print(f"  Output base:   {args.output_base.relative_to(REPO)}")
    print("=" * 72)

    suite_start = time.monotonic()
    pending: dict[str, Job] = {j.name: j for j in selected_jobs}
    running: dict[Future[JobResult], str] = {}
    passed: set[str] = set()
    failed: dict[str, JobResult] = {}
    skipped: dict[str, str] = {}
    results: list[JobResult] = []

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        while pending or running:
            # Schedule jobs whose dependencies are satisfied
            ready: list[str] = []
            for name, job in list(pending.items()):
                # If any dependency failed, mark skipped
                failed_deps = [d for d in job.dependencies if d in failed or d in skipped]
                if failed_deps:
                    skipped[name] = f"dependency {failed_deps[0]} failed/skipped"
                    del pending[name]
                    print(f"  [SKIP] {name} ({skipped[name]})", flush=True)
                    continue

                # If all dependencies passed, ready to run
                if all(d in passed for d in job.dependencies):
                    ready.append(name)

            exclusive_running = any(jobs_by_name[name].exclusive for name in running.values())
            exclusive_ready = [name for name in ready if jobs_by_name[name].exclusive]
            if exclusive_running:
                ready = []
            elif exclusive_ready and (running or any(not jobs_by_name[name].exclusive for name in ready)):
                ready = [name for name in ready if not jobs_by_name[name].exclusive]
            elif not running and exclusive_ready:
                ready = exclusive_ready[:1]

            for name in ready:
                job = pending.pop(name)
                fut = executor.submit(
                    run_single_job,
                    job,
                    compiler_dev,
                    compiler_opt,
                    args.output_base,
                )
                running[fut] = name

            if not running:
                if pending:
                    # Unresolvable cycle or missing dependency
                    for name in list(pending):
                        skipped[name] = "unresolved dependencies"
                        print(f"  [SKIP] {name} (unresolved dependencies)", flush=True)
                        del pending[name]
                break

            # Wait for at least one running job to complete
            done, _ = wait(running.keys(), return_when=FIRST_COMPLETED)
            for fut in done:
                name = running.pop(fut)
                res = fut.result()
                results.append(res)
                if res.returncode == 0:
                    passed.add(name)
                    print(f"  [PASS] {name:<46} ({res.duration:6.2f}s)", flush=True)
                else:
                    failed[name] = res
                    print(f"  [FAIL] {name:<46} (exit {res.returncode}, {res.duration:6.2f}s)", flush=True)

    suite_duration = time.monotonic() - suite_start
    cumulative_duration = sum(r.duration for r in results)
    speedup = cumulative_duration / max(0.001, suite_duration)

    print("\n" + "=" * 72)
    print("REGRESSION SUITE SUMMARY")
    print(f"  Total jobs:      {len(selected_jobs)}")
    print(f"  Passed:          {len(passed)}")
    print(f"  Failed:          {len(failed)}")
    print(f"  Skipped:         {len(skipped)}")
    print(f"  Suite wall time: {suite_duration:.2f}s ({suite_duration / 60:.1f}m)")
    print(f"  Cumulative time: {cumulative_duration:.2f}s ({cumulative_duration / 60:.1f}m)")
    print(f"  Parallel speedup:{speedup:6.1f}x")
    print("=" * 72)

    # Top 10 slowest jobs
    print("\nTop 10 slowest test jobs:")
    sorted_by_duration = sorted(results, key=lambda r: r.duration, reverse=True)
    for i, res in enumerate(sorted_by_duration[:10], start=1):
        status = "PASS" if res.returncode == 0 else f"FAIL (exit {res.returncode})"
        print(f"  {i:2d}. {res.job.name:<44} {res.duration:6.2f}s  [{status}]")

    if failed or skipped:
        print("\n" + "=" * 72)
        print("FAILED JOBS DETAIL:")
        for name, res in failed.items():
            print(f"\n--- [FAIL] {name} (exit {res.returncode}, {res.duration:.2f}s) ---")
            print(f"Build output: {res.output_dir}")
            tail = result_tail(res, 20)
            if tail:
                print("Last log lines:")
                for line in tail:
                    print(f"  {line}")
        for name, reason in skipped.items():
            print(f"\n--- [SKIP] {name}: {reason} ---")
        print("=" * 72)
        return 1

    print("\nAll regression jobs passed successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
