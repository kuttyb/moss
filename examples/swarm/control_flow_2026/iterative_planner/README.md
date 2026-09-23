# Iterative Planner

This standalone Moss package schedules a small dependency-constrained task set
within an effort budget. Each task is represented by aligned `Vector[Int]`
columns for value, effort, dependency index, and urgency. `choose_next` scans
every task, ignores completed or unaffordable work, requires its dependency to
be completed, and selects the highest priority:

```text
priority = value * 3 + urgency * 2 - effort
```

The demo runs iterative planning rounds with nested eligibility branches. Its
five-task data set chooses survey, foundation, then launch and prints:

```text
3 45 1
```

That is `completed_tasks delivered_value remaining_effort`.

From this directory:

```sh
../../../../margo run
../../../../margo test --json
../../../../margo debug --trace
```

`tests/planner_test.moss` checks scoring, the dependency-unlocking sequence,
and the no-eligible-task path. The development history and preserved natural
failed formulations are in [REPORT.md](REPORT.md) and `failed_attempts/`.
