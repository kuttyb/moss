# P28-R7-005.1 Oracle Successor Report

## Scope and certification

This is a separately versioned v2 successor to the certified R7-005 oracle. The certified v1 package at `tests/phase28/oracle/` is unchanged. Its expectations digest remains `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`; the runtime callsite allowlist digest remains `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`. The v2 package retains the first 56 parent expectations and adds four fixtures, for 60 total.

Independent normative review approved the corrected diagnostic mappings and four fixture expectations at `c808e9fab5187e25703cac42e4be15162516d39d`. Reviewer-controlled holdouts received an oracle-spec GO and integrity PASS. Their two frozen-compiler failures are preserved as compiler findings: primitive Vector loop element consumption is rejected, and Map.get write-through outputs `new\nold` instead of `new\nnew`.

## Mapping and fixtures

Six mapping corrections address ORACLE-03, ORACLE-28, ORACLE-30, ORACLE-34, ORACLE-35, and ORACLE-10B. ORACLE-10B remains byte-identical to the v1 fixture and requires the exact clone-alias diagnostic and executable source line. New fixtures cover primitive Vector elements, Map.get write-through, specialization identity collision, and shadowed binding identity. The v1 package was not altered to obtain any result.

## Frozen R7-008 comparison

The full 60-fixture v2 comparison was rerun against compiler SHA `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc` after the final ORACLE-10B mapping change. Results: 13 PASS, 47 FAIL; 18 semantic failures and 32 rows blocked by missing or invalid clone provenance. ORACLE-10B passes with the stricter mapping. The newly added primitive loop fixture fails at checking with `OWNERSHIP_VECTOR_ELEMENT_CONSUME`. Map.get write-through checks successfully but Fast Debug, native debug, and native `-O` produce `9\n1` instead of `9\n9`. Specialization and shadowing pass semantic/runtime checks but lack required provenance artifacts. The detailed JSON and ledger are in `tests/phase28/oracle_r7_005_1/reports/`.

These results report implementation conformance; they do not alter the oracle's normative expectations. Certification is an oracle-spec decision and does not imply compiler compliance. V2 recertification and final independent adversarial review are recorded in the package certification and review artifacts.
