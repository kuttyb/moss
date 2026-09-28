# Phase 22.3 semantic-query A/B benchmark

## Protocol

- Compiler and benchmark SHA: `6c8cd5b7ce249fb59d95231cfa52e4caf0470104`
- Compiler binary SHA-256: `8a1799f7f758820d66e8a933c46f2d4dfc249e21c7d8145a94a8ab18a5a79146`
- Model: `gpt-6-sol`; reasoning: `medium`; Codex: `codex-cli 0.158.0`
- Rust: `rustc 1.98.1 (48a229cea 2026-09-01)`
- Tasks: 20; trials/task/profile: 5; timeout: 900 seconds
- Profiles: legacy post-22.1 projection from `bb388d4aeb1df7f6dac682b427d666a44a748bf4`; full current Phase 22.3 treatment
- Legacy docs/skills hashes: `{".agents/skills/moss-agent-workflow/SKILL.md": "1dd00e7372da850a13b06ddff686a36f26ed6ae767c4fb5c661dc2644a7a5379", ".agents/skills/moss-language/SKILL.md": "6fe19ff1cc287e359b241d1f8eb914fb702eb9b78e324380c7983eb3429fd8d9", ".codex/CURRENT_STATUS.md": "faf5d5fd8163945870a89f4a5028c6d629aa91fcfa620b08ca28fbfdadc6ae29", "AGENTS.md": "4262a3e6a91b2acd8fba55cd416b05ac481a1ad31e3a23cfceab98644887370f", "docs/AGENT_API.md": "35fff03177ca0b20101ce56529c2f167a6348c6f141ff4824d13454323e7df7f"}`
- Phase 22.3 docs/skills hashes: `{".agents/skills/moss-agent-workflow/SKILL.md": "65d7338079856a6bda5eff64d4ce976048145dd8b44c68789368eccf213abb8a", ".agents/skills/moss-language/SKILL.md": "45cf0921d5ac37f38111e6ae378d0c4f854735dc84afd4402b1a7cc13e9cfea6", ".codex/CURRENT_STATUS.md": "e6bfb2d0d0dc6f0aea5f73b3735d4104e3322d10a6d2aee55ba67a4f486d2756", "AGENTS.md": "71b1395081226aac8f8f0e322562d94498d12fa27267489aed541d6625b97957", "docs/AGENT_API.md": "ba84ea5a8c1c172978871f96f316bf1eacc0ab170b579a191596cae8d877ae4d"}`
- Restrictions: Codex shell workspace sandbox network_access=false; workspace-write in a fresh mount namespace with Git metadata replaced by empty tmpfs

## Headline result

| Metric | Legacy | Phase 22.3 | Delta |
|---|---:|---:|---:|
| Semantic fact accuracy | 92.7% | 96.2% | 3.5% |
| Final task pass rate | 100.0% | 100.0% | 0.0% |
| Semantic-heavy pass rate | 100.0% | 100.0% | 0.0% |
| Control-task pass rate | 100.0% | 100.0% | 0.0% |
| Median semantic detour cost | 3.000 | 3.000 | 0.000 |
| Source inspection commands | 1.230 | 1.290 | 0.060 |
| Generated Rust inspections | 0.000 | 0.000 | 0.000 |
| Semantic queries/task | 2.590 | 2.490 | -0.100 |
| Queries/correct fact | 0.969 | 0.849 | -0.120 |
| First-validation success | 85.0% | 85.0% | 0.0% |
| Agent tool calls/task | 14.940 | 14.650 | -0.290 |
| Moss/Margo calls/task | 6.010 | 5.770 | -0.240 |
| Attempts-to-green | 1.150 | 1.150 | 0.000 |
| Wall time/task (ms) | 44524.380 | 43402.380 | -1122.000 |
| Input tokens/task | 258363.520 | 236485.690 | -21877.830 |

Bootstrap intervals are paired 95% intervals over task/trial observations and are descriptive, not a claim of statistical significance.

## Category breakdown

| Category | Accuracy delta | Pass-rate delta | SDC delta |
|---|---:|---:|---:|
| control | 0.0% | 0.0% | n/a |
| domain-synchronization | 0.0% | 0.0% | 0.200 |
| effects-calls | 9.2% | 0.0% | -1.400 |
| resolution | 0.0% | 0.0% | 0.550 |
| type-ownership | 5.5% | 0.0% | 0.050 |

## Adoption

Legacy semantic-query adoption: 87.5%. Phase 22.3 adoption: 87.5%.
Treatment `resolve` adoption: 68.8%.
Legacy query distribution: `{"calls": 39, "effects": 59, "inspect": 98, "ownership": 17, "resolve": 8, "type": 16, "why": 22}`.
Treatment query distribution: `{"calls": 31, "effects": 41, "inspect": 52, "ownership": 15, "resolve": 87, "type": 11, "why": 12}`.

## Failure analysis

- Legacy Wins: none
- Treatment Wins: P223008, P223009, P223012
- Both Fail: none
- Both Pass Treatment More Efficient: P223002, P223006, P223009, P223010, P223012, P223016
- Treatment Regressions: none

## Interpretation

Under the controlled query-surface intervention, Phase 22.3 changed pooled semantic fact accuracy by 3.5%, mean semantic detour cost by -0.150, and final completion by 0.0%. These deltas concern agent semantic observability and workflow efficiency; they do not imply any change to Moss language semantics.

Phase 22.3 improves semantic retrieval when agents use it, while discovery/adoption remains a separate agent-workflow problem.

## Remaining work

None. All planned trials completed with the required raw evidence.
