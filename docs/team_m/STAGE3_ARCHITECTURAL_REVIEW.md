# Moss Phase 24 — Stage 3: Independent Architectural & Correctness Review

**Target Packages:** `moss.math`, `moss.random`, `moss.time`  
**Reviewer:** Independent L6 Architect, Moss Team M  
**Status:** **CONDITIONAL APPROVAL WITH MANDATORY DESIGN ADJUSTMENTS (INCORPORATED)**

---

## 1. Executive Summary

This independent architectural review assesses the Phase 24 standard library packages (`moss.math`, `moss.random`, and `moss.time`) against Moss v0.1 language semantics, compiler capabilities, numerical precision constraints, and Fast Debug / Native parity.

The high-level scope is sound and necessary for standard library self-sufficiency. The review identified several critical Moss v0.1 compiler rules and mathematical edge cases:

1. **Lack of `export let`:** Module-level constant exports (`export let`) are not supported in Moss v0.1. Constants must be exposed via nullary functions (`export fn pi() -> Float: return 3.14159...`).
2. **Absence of Bitwise Operators:** Moss v0.1 lacks `&`, `|`, `^`, `<<`, `>>`, and `~`. Classical algorithms relying on bit extraction, floating-point mantissa manipulation, or bit-shift PRNGs fail compilation.
3. **Absence of General Recursion:** Moss v0.1 strictly rejects direct and mutual recursion (`RECURSIVE_CALL`). All polynomial, range reduction, and series calculations must be strictly iterative (`while` loops).
4. **Defect of 64-bit LCG Without Shifts:** A raw 64-bit Knuth LCG suffers from severe low-order bit periodicity. Without bitwise shifts (`>>`) to discard low bits, operations like `randbool()` or `randint()` exhibit catastrophic non-uniformity (e.g., strictly alternating parity on LSB).
5. **Adoption of MRG32k3a:** Mandatory switch to Pierre L'Ecuyer's MRG32k3a combined multiple recursive generator ($2^{191}$ period, zero bitwise operations, bounded 64-bit products).
6. **Missing Native Clock Hook:** Moss v0.1 currently lacks runtime clock syscall builtins. Time value types are modeled in pure Moss, and a runtime clock hook RFC is submitted for L7 review.

---

## 2. Package-Specific Review & Disposition

### 2.1 `moss.math`
- **Constants:** Exported as nullary inline functions (`pi()`, `e()`, `tau()`, `infinity()`, `neg_infinity()`, `nan()`).
- **IEEE-754 Semantics:**
  - `abs(-0.0)` returning `+0.0`: Handled via explicit `if x == 0.0: return 0.0`.
  - `abs_int(INT64_MIN)`: Documented as wrapping to `INT64_MIN` under 64-bit two's complement.
  - `min`/`max`: NaN commutativity and signed zero distinction via reciprocal comparison (`1.0 / a < 1.0 / b`).
  - `signum(-0.0)` and `signum(NaN)`: Preserves signed zero `-0.0` and `NaN`.
  - `hypot(x, y)`: Scaling by $\max(|x|, |y|)$ prevents intermediate overflow for large magnitudes ($> 10^{154}$) and underflow for small magnitudes ($< 10^{-154}$).
  - `floor`/`ceil`/`trunc`/`round`: Implemented via the exact $2^{52} = 4503599627370496.0$ constant-shift float identity without bitwise operations or integer conversions.
  - `pow(x, y)`: Full IEEE-754 branch cascade, handling `pow(x, 0.0) == 1.0` (even for NaN) and integer binary exponentiation for negative bases.

### 2.2 `moss.random`
- **Algorithm Switch:** Replaced Knuth 64-bit LCG with **MRG32k3a**:
  - Two 3rd-order linear recurrences mod $m_1 = 4294967087$ and $m_2 = 4294944443$.
  - All intermediate multiplications ($< 6.03 \times 10^{15}$) strictly fit in 64-bit signed `Int`.
  - Rejection sampling in `randint(min, max)` eliminates modulo bias.
  - Passes TestU01 BigCrush; no low-bit periodicity.
- **Security Boundary:** Prominently documented non-cryptographic disclaimer.

### 2.3 `moss.time`
- **Clock Segregation:**
  - `Duration`: 64-bit signed nanoseconds ($\pm 292$ years).
  - `Instant`: Monotonic time point (steady clock), strictly for interval calculation and benchmarking.
  - `Timestamp`: Wall-clock Unix epoch time (milliseconds). Documented safe date range beyond year 200,000 AD.
- **Calendar Calculations:** Implemented Howard Hinnant's Euclidean proleptic Gregorian calendar algorithm in pure integer arithmetic (zero loops, zero leap-table dependencies).
- **ISO 8601 Formatting:** Exact zero-padded formatting `YYYY-MM-DDTHH:MM:SS.mmmZ` via pure Moss string operations.
- **Runtime Hook RFC:** Documented proposed `monotonic_now()` and `realtime_now()` compiler hooks for Fast Debug and Native Rust backends.

---

## 3. Review Disposition

All mandatory adjustments and architectural directives have been incorporated into the package source trees. Team M is authorized to proceed to validation and benchmark reporting.
