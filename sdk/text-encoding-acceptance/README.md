# Team T cross-package acceptance

This external consumer imports `moss.text`, `moss.encoding`, and all four
Foundation packages through real Margo path dependencies. It verifies text and
strict UTF-8 composition without inventing a String/byte conversion API.

Run from this directory:

```sh
../../margo run
../../margo --release run
../../margo debug
../../margo test
../../margo test --interp
```

All application modes print `text encoding acceptance ok`; the package tests
cover valid composition and invalid encoding boundaries.
