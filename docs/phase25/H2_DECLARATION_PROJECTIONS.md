# H2 checked declaration projection repair

The H/T intake at `5cfdc1630d67506146e4adcc52628e508ffdc7c5` accepted erased projections after binding a TypeRef on Param, Method, TraitMethod, Handler and Function. The minimal five-kind probe rejected **0/5** stale declarations. A valid callable Function also crossed typed-semantic projection with its bound return projection removed.

The repair requires an exact canonical projection whenever a TypeRef is bound, including presence for optional return/reply projections. Unbound source declarations remain valid. Typed-semantic projection and ABI interface emission validate the complete Program before consuming checked declarations; the existing Checker validation remains in place.

`tests/tooling/check_phase25_declaration_boundaries.cpp` injects nine empty/reset cases into the real compiler entry points. It requires the specific projection-contradiction failure through Program recursion, Checker construction, typed semantic export and ABI interface emission (36 boundary checks), and accepts valid bound and default unbound records. The existing G2 authority runner builds and executes this test, so required PR semantics cover it automatically.

Local optimized compiler rebuild, live moss-0.1 bootstrap, direct boundary regression, existing H G1/G2/G3 suites, feedback validation and whitespace checks passed. Automated PR evidence is not yet available for this branch. The H intake PR is unchanged; this branch must absorb the reviewed integration target before opening its draft PR. H1, H3 and release acceptance are outside this bounded repair.
