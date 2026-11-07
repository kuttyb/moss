"""Team 4 generator: the B6 replay step specs (scenarios only, no fixtures).

The specs are derived from git history (immutable) plus the recorded reference
expectations in replay_expectations.json (written by `run.py replay-record`)."""
from __future__ import annotations

import json
from pathlib import Path
import random

import ect_common as C
import replay as R
from edit_generators import scenario_row


class B6ReplayGenerator(C.Generator):
    name = "b6_replay"
    version = "1.0.0"
    category = "b6_replay"

    def __init__(self):
        self.scenarios: list[dict] = []

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        exp = json.loads(R.EXPECTATIONS_PATH.read_text()) if R.EXPECTATIONS_PATH.exists() else {}
        rows, _ = R.build_specs(exp)
        for r in rows:
            for e in r["entries"]:
                missing = (e["in_pre"] and e["expect_pre"] is None) or (e["in_post"] and e["expect_post"] is None)
                if missing and exp:
                    raise C.ToolError(f"{r['id']}: no recorded expectation for {e['id']}; run replay-record")
        self.scenarios = [scenario_row(r) for r in rows]
        return []


REPLAY_GENERATORS = [B6ReplayGenerator()]
