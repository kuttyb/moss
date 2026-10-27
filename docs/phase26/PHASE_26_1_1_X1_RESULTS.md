# Phase 26.1.1 X1 — `.mossi` execution sections

## Scope and design

X1 made `.mossi` the execution-body store for Fast Debug. Section 1 remains the
semantic interface and digest input: exported signatures and existing generic
bodies. Section 2 stores checked non-generic execution bodies and is excluded
from the Section 1 interface digest. The Section 2 payload has its own length
and hash and is bound to the package fingerprint and Section 1 digest. The
loader validates Section 1 first, then checks Section 2 metadata and bindings
before attaching execution bodies. Margo supplies and verifies the package
fingerprint.

Check-only provider fingerprints hash the actual Section 1 bytes, excluding
only the package source freshness record and Section 2, and include source
currentness. This lets a non-generic body-only edit refresh the provider
artifact without rechecking consumers, while Section 1 changes and stale
interfaces remain visible to consumers.

## Recorded X1 evidence

The X1 status entry records that `tests/tooling/check_phase26_1_1_x1.py` passed
the initial package graph, a non-generic E1 body edit, stable Section 1 hashes,
provider-only recheck, and generic-body propagation to the consumer. It also
records successful optimized compiler build, Python compilation, and whitespace
checks at that checkpoint. Those results predate the current full baseline and
were not rerun as part of the baseline at `fc6331d08146f7150378ee3728b228e915e078e5`.

The focused X1 gate builds an app and provider, checks Section 2 payload
length/hash metadata, verifies that the Section 1 digest remains stable for
non-generic edits and changes for generic edits, confirms dependency
check-count behavior, compares source-free and native Fast Debug output/traces,
and verifies that corrupt Section 2 fails closed without changing `moss check`'s
semantic verdict.

## Review history and current gaps

The Architect returned **NO-GO** for exact implementation SHA
`46f08087ce986ce267039e1766b09002c32164b9`: the direct-provider fingerprint
needed the actual Section 1 bytes. The status records that this was corrected
in the subsequent X1 checkpoint, including the Section 1 byte hash and
source-current status. There is no recorded Architect GO for the final X1+X2
implementation; that exact-SHA review remains required.

At `fc6331d08146f7150378ee3728b228e915e078e5`, X1 Section 2 writing and
loading remain guarded by `!v10_interface`. Bank's current decision requires
Section 2 support for ABI v10 too, without expanding v10's checked-body
coverage. The current broad inclusion of every non-generic private function
and method also fails the existing unrelated-private-helper regression; the
producer must retain only bodies needed by the execution closure while
including `sdk/text`'s reachable `text__matches_at` helper.
