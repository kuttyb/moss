# CP-B revised `.mossi` invariant review

VERDICT: GO — 0 confirmed product defects; 1 test-coverage caveat closed by the independent probe.

## Scope and tested revisions

- Candidate source: `dacf2fab1ab252968dc974b180e7279e46cb22e9` (`phase27-work`).
- Baseline source: `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`.
- Candidate compiler: built in its detached worktree with `make --jobs="$(nproc)" moss-dev`; binary SHA-256 `d221490864330671fc2470c313896397fb2d38f57ba082781eaf888efc36c8cb`.
- Baseline compiler: built in a separate detached worktree with the same command; binary SHA-256 `00f34062c56343485cd8661dd4145fea3d5a7ef5d19b052ef2a7068224497147`.
- Baseline `margo` matches candidate `margo` byte for byte.

## Results

1. **Same compiler, off versus shadow:** Ran `tests/tooling/check_phase27_est_mossi.py` using the candidate compiler. The script built library and app packages at fixed absolute paths for both modes and compared the complete raw `.mossi` bytes. Both passed. Raw artifact SHA-256 values were library `5f6bc2d5e6234e155d1dea05f85c32837ba3060e2451608186d44ec1d592da2f` and app `0da7da202c5863832331a851d6a2c84942f866334089f86756bac899d30a4b8a`. The probe also confirmed generic ABI-v9 and imported Section 2 bodies are consumed by the shadow EST path.

2. **Different compiler binaries and baseline recomputation:** The compiler identity had the same absolute `moss_path` for both builds, `/home/owner/daji/code/moss/tmp/cp-b-revised/tmp/phase27-mossi-baseline/<run-id>/active-moss`, and distinct `moss_sha256` values above. Rust compiler identity was identical. No identity override or masked build input was used; each actual build used Margo's standard identity and fingerprint flow.

   The independent probe confirmed each actual artifact's recorded `package_fingerprint` equals Margo's result using that build's own compiler identity. It then recomputed each package fingerprint with the baseline identity, replaced only `package_fingerprint` in Section 2, recomputed `execution_body_hash` over that payload, and replaced only that dependent hash. The full reconstructed baseline and candidate files were byte-identical for library and app. Thus every other Section 1 and Section 2 byte matched. Raw full-file hashes remained distinct across compilers, as required by compiler-bound identity.

3. **Coverage check:** The candidate cross-compiler script compares the complete reconstructed files, so it cannot silently ignore additional Section 1/2 differences. Its regex replacements are limited to one `package_fingerprint` in the execution payload and one `execution_body_hash` in the file. The supplied script did not explicitly assert that the *candidate artifact's* recorded fingerprint recomputes from the candidate identity. The added independent probe asserts this for both library and app; both assertions pass. This was a test-proof gap, not an observed product defect.

4. **Prior finding:** The prior raw cross-binary comparison finding is resolved under the revised invariant. Raw cross-binary `.mossi` equality is intentionally not expected because package identity retains the compiler binary SHA. Under baseline-identity recomputation, the remaining artifact bytes match exactly.

## Reproduction

See [`tests/phase27/adversarial/cp-b-revised/README.md`](../../tests/phase27/adversarial/cp-b-revised/README.md) and `cross_compiler_probe.py`. The original same-binary gate and the detached baseline build command are included there.
