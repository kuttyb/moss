# Workflow state machine

This standalone Moss package models a small release workflow driven by integer
events. It begins in intake, advances through review and approval, can rewrite
its state after a retry, rejects immediately, and reports a partial score when
the input ends before completion.

Event codes are intentionally compact so the control flow is easy to trace:

- `1` admits an intake item to review.
- `2` advances review to approval.
- `3` marks approval ready to finish.
- `4` completes a ready workflow.
- `8` requests a retry from review or approval.
- `9` rejects immediately.

`run_workflow` uses mutable local stage, retry, score, index, and history
bindings. It has nested branch paths, while-loop traversal, early results for
rejection/completion, local vector mutation, and helpers that carry the
accumulated values into final scoring. `completed_steps` separately demonstrates
the supported `break` path.

From this directory:

```sh
/home/kuttybanerjee/daji/moss/margo run
/home/kuttybanerjee/daji/moss/margo test --json
/home/kuttybanerjee/daji/moss/margo debug --trace
```

The demo follows an approval retry and prints its score (`55`) and the number
of events processed before any rejection (`6`).
