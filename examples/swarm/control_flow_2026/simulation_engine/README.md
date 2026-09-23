# Emergency Supply Simulation Engine

This standalone Moss package simulates a short emergency-supply plan. Each day
combines incoming demand with carried backlog, assigns an urgency level from
shortage and weather, applies weather-sensitive delivery capacity, and records
the largest unresolved backlog. The final resilience score is:

```text
fulfilled need - remaining backlog - peak backlog
```

The program is intentionally small but application-shaped: it has mutable local
state, indexed value flow through three input vectors, nested decisions, a
bounded loop, two helper calls per day, and distinct recovery/shortage result
paths. A domain would add no value here because all state is local to one
deterministic plan evaluation.

Run it from this directory:

```sh
../../../../margo run
../../../../margo test --json
../../../../margo debug --trace
```

The demo prints:

```text
resilience score 28
```

Tests cover nested urgency classification, weather and critical-reserve delivery,
full recovery after a peak backlog, and a plan that ends with unresolved need.
`failed_attempts/` retains the initial native-broken `for` formulation, its
minimal reproducer, and the later unparenthesized-pipeline condition reproducer.
