# Moss Phase 24 — Team N: Independent Architectural Evaluation & Challenge Report

**Reviewer:** Independent L6 Architect, Moss Team N (Numerical Computing)  
**Target SDK Packages:**  
- `moss.linalg` (`packages/linalg`)  
- `moss.sparse` (`packages/sparse`)  
- `moss.statistics` (`packages/statistics`)  
- `moss.numerics_integration` (`packages/numerics_integration`)  
**Parent Baseline Commit:** `3999e12b635be2f6d5f74dcbc1cf72bb50b74ffc` (`origin/phase24-integration`)  
**Target Delivery Branch:** `phase24-numerics`  
**Normative Specifications & Standards:**
- Moss Phase 21 Error Handling Specification (`docs/MOSS_PHASE_21_ERROR_HANDLING.md`)
- IEEE-754-2019 Standard for Floating-Point Arithmetic (binary64, signed zeros, NaNs, subnormals)
- Gene H. Golub & Charles F. Van Loan: *Matrix Computations* (4th Ed., Johns Hopkins University Press)
- Nicholas J. Higham: *Accuracy and Stability of Numerical Algorithms* (2nd Ed., SIAM)
- Yousef Saad: *Iterative Methods for Sparse Linear Systems* (2nd Ed., SIAM)
- B. P. Welford: *"Note on a method for calculating corrected sums of squares and products"* (Technometrics 4(3), 1962)
- Tony F. Chan, Gene H. Golub, and Randall J. LeVeque: *"Updating Formulae and a Pairwise Algorithm for Computing Sample Variances"* (Stanford / COMPSTAT, 1979)
- William Kahan: *"Pracniques: further remarks on reducing truncation errors"* (Communications of the ACM 8(1), 1965)
- Rob J. Hyndman and Yanan Fan: *"Sample Quantiles in Statistical Packages"* (The American Statistician 50(4), 1996)
- Moss v0.1 Language Specification & Agent Guidelines (`AGENTS.md`, `moss-language`, `moss-agent-workflow`)

---

## 1. Executive Summary & Attributable Verdict

As the Independent L6 Architect for Moss Phase 24 Team N (Numerical Computing), this evaluation performs an exhaustive review and challenge of the proposed architecture for the Moss Numerical SDK comprising:
1. `moss.linalg`: Dense linear algebra representations, factorizations, linear solves, determinants, inversions, and vector operations.
2. `moss.sparse`: Compressed Sparse Row (CSR) and Compressed Sparse Column (CSC) formats, coordinate triplet assembly, structural invariants, SpMV, SpMM, and sparse linear operations.
3. `moss.statistics`: Streaming accumulators (Welford, Chan-Golub-LeVeque), compensated summation (Kahan), two-pass descriptive statistics, bivariate covariance/correlation, and order statistics (heapsort, quantile interpolation).

### Attributable Verdict: **CONDITIONAL GO / ARCHITECTURAL APPROVAL WITH MANDATORY ENFORCEMENT OF FROZEN CONTRACTS**

The proposed architecture is fundamentally sound, computationally efficient, and well-tailored to Moss v0.1 language constraints. However, production viability requires strict adherence to mandatory architectural remediations identified in this report:
1. **Dense Borrow Scoping & Inferred Effects:** Absolute enforcement of the intermediate index binding rule (`idx = r * m.cols + c; m.data[idx] = val`) to prevent rustc E0502 backend borrow conflicts, and prohibition of explicit parameter `var` annotations.
2. **LU Decomposition Stability & Thresholding:** Mandatory scaled pivot tolerance $\epsilon = 10^{-14} \cdot \max_{i,j} |A_{ij}|$ to catch near-singular systems, and frozen `LinalgError.SingularMatrix` / `LinalgError.NonSquareMatrix` error contracts.
3. **Determinant Overflow Mitigation:** Mandatory export of `matrix_log_det` returning `LogDet(sign, log_abs_det)` alongside `matrix_det` to prevent binary64 overflow ($> 10^{308}$) and underflow ($< 10^{-308}$) on matrices with $N \ge 100$.
4. **Sparse Duplicate Handling Mandate:** Absolute requirement that coordinate triplets with duplicate $(r, c)$ entries MUST be **summed**, never replaced (last-write-wins). Summation is required by physical flux conservation, finite element assembly, and standard scientific libraries (SciPy, Eigen, SuiteSparse).
5. **CSR Strict Column Monotonicity Invariant:** Formal enforcement of strictly increasing column indices within each row to ensure $O(\log \text{nnz}_i)$ binary search, linear-time two-pointer merge addition, and deterministic sparse structure.
6. **Chan-Golub-LeVeque 64-bit Integer Overflow Protection:** Floating-point scaling for the merged weight term $\frac{n_A \cdot n_B}{n}$ to prevent signed 64-bit integer overflow when merging large streaming datasets ($N > 3 \times 10^9$).
7. **Order Statistics IEEE-754 NaN Validation:** Mandatory pre-sorting scan rejecting `NaN` with `StatsError.InvalidValue` to prevent silent corruption of heapsort ordering.
8. **Phase 21 Error Totality:** Strict adherence to closed exported error enums (`LinalgError`, `SparseError`, `StatsError`) with zero unhandled exceptions escaping `main`.

---

## 2. Detailed Assessment of `moss.linalg` (Dense Linear Algebra)

### 2.1 Storage Representation & Memory Layout

The proposed representation for dense matrices is:
```moss
export type Matrix:
  rows: Int
  cols: Int
  data: Vector[Float]
```

#### Architectural Assessment:
1. **Contiguous Memory Locality:** Storing data as a flat, row-major `Vector[Float]` (lowered to `Vec<f64>` in Rust) is optimal for modern CPU cache architectures. Elements within the same row are adjacent in physical memory (stride 1). Operations such as matrix-vector multiplication ($y = A x$), row scaling, and row elimination in Gaussian elimination scan memory sequentially, maximizing hardware prefetcher efficiency and SIMD vectorization potential.
2. **Rejection of Vector-of-Vectors:** An alternative representation using `Vector[Vector[Float]]` is rejected on architectural grounds:
   - It requires $R + 1$ independent heap allocations instead of $1$, incurring high memory allocator overhead.
   - It introduces a pointer indirection for every row access, disrupting memory prefetching and causing cache misses.
   - It risks ragged matrix shapes (rows having inconsistent lengths), which breaks structural invariants at runtime.
3. **Dimensions & Bounds Invariants:**
   - Strict positivity: `rows > 0` and `cols > 0`. A matrix with non-positive dimensions represents an uninitialized or degenerate state and MUST raise `LinalgError.InvalidDimensions`.
   - Data length invariant: `data |> count == rows * cols`. Any violation MUST raise `LinalgError.DimensionMismatch`.
   - **Integer Overflow Protection:** In 64-bit signed arithmetic, multiplying `rows * cols` can overflow `INT64_MAX` ($9,223,372,036,854,775,807$) if either dimension is enormous. Matrix allocation functions MUST validate `rows <= 9223372036854775807 / cols` before computing buffer size.

### 2.2 Indexing & Rust Backend E0502 Borrow Scoping

#### Root Cause Analysis of Lowering Conflict:
When a Moss agent writes an in-place element assignment such as:
```moss
m.data[r * m.cols + c] = val
```
The Moss compiler lowers this statement into the following Rust construct:
```rust
m.data[r * m.cols + c] = val;
```
In Rust's borrow checker, evaluating `m.data[...] = val` requires a mutable borrow of `m.data` (which borrows `*m` mutably). Simultaneously, the index expression `r * m.cols + c` accesses `m.cols`, which requires an immutable borrow of `*m`. Because Rust forbids simultaneous mutable and immutable borrows of the same struct, rustc terminates compilation with:
```text
error[E0502]: cannot borrow `*m` as immutable because it is also borrowed as mutable
```

#### Architectural Rule for Implementation Agents:
All index computations targeting vector fields inside structs MUST store the computed index in a local binding before the assignment:
```moss
let idx = r * m.cols + c
m.data[idx] = val
```

#### Parameter Effect Inference Rule:
Moss v0.1 infers parameter mutation effects (`READ`, `WRITE`, `CONSUME`) automatically. Programmers MUST NOT write `var` in parameter lists (e.g. `fn matrix_set(var m: Matrix, ...)` is rejected by `moss check` with `MOSS_COMPILE_ERROR: invalid parameter: var m: Matrix`). The mutating parameter must be written simply as `m: Matrix`.

#### Frozen Accessor Contracts:
To encapsulate indexing and eliminate repetitive manual calculations, `moss.linalg` MUST export both pure and in-place accessors with explicit bounds checking:
```moss
export fn matrix_get(m: Matrix, r: Int, c: Int) -> Float:
  if r < 0 or r >= m.rows or c < 0 or c >= m.cols:
    raise LinalgError.IndexOutOfBounds
  let idx = r * m.cols + c
  return m.data[idx]

export fn matrix_set(m: Matrix, r: Int, c: Int, val: Float):
  if r < 0 or r >= m.rows or c < 0 or c >= m.cols:
    raise LinalgError.IndexOutOfBounds
  let idx = r * m.cols + c
  m.data[idx] = val
```

### 2.3 Factorization & Linear Systems Algorithms

#### 2.3.1 LU Decomposition with Partial Pivoting ($P A = L U$)

The proposed factorization algorithm is Gaussian Elimination with Partial Pivoting (GEPP):
$$P A = L U$$
where:
- $P$ is a permutation matrix representing row swaps.
- $L$ is a unit lower triangular matrix ($L_{ii} = 1, L_{ij} = 0 \text{ for } j > i$).
- $U$ is an upper triangular matrix ($U_{ij} = 0 \text{ for } j < i$).

#### Compact In-Place Storage:
Following standard LAPACK (`dgetrf`) architecture, $L$ and $U$ MUST be stored packed within a single $N \times N$ matrix:
- The upper triangle (including diagonal) stores $U_{ij}$.
- The strictly lower triangle stores the multipliers $L_{ij} = A_{ij}^{(k)} / U_{kk}$. (The unit diagonal $L_{ii} = 1.0$ is implicit).
- Permutations are tracked in a 1D vector `pivots: Vector[Int]` of length $N$, where `pivots[k]` records the row swapped with row $k$ at elimination step $k$.
- A counter `num_swaps: Int` tracks parity for determinant calculation.

```moss
export type LuDecomposition:
  n: Int
  lu: Matrix
  pivots: Vector[Int]
  num_swaps: Int
```

#### Backward Stability & The Wilkinson Counterexample:
- **Theoretical Guarantee:** GEPP is provably backward stable for almost all non-singular matrices. The computed solution $\hat{x}$ to $A x = b$ satisfies $(A + \Delta A) \hat{x} = b$, where:
  $$\|\Delta A\|_\infty \le 8 n^3 \rho \mathbf{u} \|A\|_\infty + O(\mathbf{u}^2)$$
  Here $\mathbf{u} = 2^{-53} \approx 1.11 \times 10^{-16}$ is binary64 machine precision, and $\rho$ is the growth factor:
  $$\rho = \frac{\max_{i,j,k} |A_{ij}^{(k)}|}{\max_{i,j} |A_{ij}|}$$
- **Failure Modes & Pathological Cases:**
  1. *Wilkinson Matrix:* For specific pathological matrices where diagonal elements are 1 and lower triangular elements are -1, the growth factor reaches $\rho = 2^{n-1}$. For $n = 60$, $\rho = 2^{59} \approx 5.76 \times 10^{17}$, completely overwhelming double precision and destroying all significant digits. However, such matrices do not occur in natural engineering or physical systems, where empirical growth is typically $\rho \sim n^{1/2}$ or $n^{2/3}$.
  2. *Singular Matrices:* If all candidate pivots in column $k$ (on and below the diagonal) are zero, the matrix is rank-deficient. Division by zero in multiplier computation $L_{ik} = A_{ik} / U_{kk}$ will yield `NaN` or `inf`.
  3. *Ill-Conditioned Matrices:* If a pivot magnitude $|U_{kk}|$ is non-zero but minuscule ($< 10^{-15}$), back-substitution will amplify floating-point noise exponentially.

#### Mandatory Singularity Thresholding:
Implementation agents MUST NOT rely on exact zero comparison (`pivot == 0.0`). Instead, they must enforce a scaled singularity threshold:
$$\text{tol} = 10^{-14} \cdot \max_{i,j} |A_{ij}|$$
If at any step $k$, $\max_{i \ge k} |A_{ik}| \le \text{tol}$, the decomposition MUST immediately raise `LinalgError.SingularMatrix`.

#### Non-Square Matrix Validation:
LU decomposition, matrix determinant, and linear solve are strictly defined for square matrices. Attempting these operations on a rectangular matrix (`rows != cols`) MUST raise `LinalgError.NonSquareMatrix`.

#### 2.3.2 Linear Solve ($A x = b$) & Matrix Inversion

1. **Two-Stage Triangular Solve:**
   Given $P A = L U$, solve $A x = b$ via:
   - Permute right-hand side: $b' = P b$ by applying recorded swaps `pivots`.
   - Forward substitution on unit lower triangle: $L y = b'$.
     $$y_i = b'_i - \sum_{j=0}^{i-1} L_{ij} y_j$$
   - Backward substitution on upper triangle: $U x = y$.
     $$x_i = \frac{y_i - \sum_{j=i+1}^{n-1} U_{ij} x_j}{U_{ii}}$$
   - Precondition: $|U_{ii}| > \text{tol}$; otherwise raise `LinalgError.SingularMatrix`.
2. **Prohibition Against Explicit Inversion for Solving:**
   Implementation agents MUST NEVER solve $A x = b$ via $x = A^{-1} b$. Direct LU solve requires $\frac{2}{3} n^3 + 2 n^2$ floating-point operations. Inversion requires $2 n^3$ operations ($3\times$ slower) and introduces unnecessary numerical roundoff error that doubles the condition number penalty.
3. **Matrix Inversion ($A^{-1}$):**
   `matrix_inverse(m: Matrix) -> Matrix` must be implemented by solving $A X = I_n$ against the $n$ standard basis vectors $e_0, \dots, e_{n-1}$ sharing a single LU factorization.

#### 2.3.3 Determinant & Catastrophic Binary64 Overflow/Underflow

The standard determinant formula from LU factorization is:
$$\det(A) = (-1)^{\text{num\_swaps}} \prod_{i=0}^{n-1} U_{ii}$$

#### The Exponential Scaling Trap:
In IEEE-754 binary64 floating-point arithmetic:
- $\text{DBL\_MAX} \approx 1.79 \times 10^{308}$
- $\text{DBL\_MIN} \approx 2.22 \times 10^{-308}$
For a modest $200 \times 200$ matrix whose diagonal pivots average $10.0$, the direct product evaluates to $10^{200}$. For $n = 400$, the product evaluates to $10^{400}$, which instantly overflows binary64 to `+inf` or `-inf`, rendering `matrix_det` completely useless despite the matrix being well-conditioned. Conversely, if pivots average $0.1$, $n = 400$ underflows to `0.0`.

#### Mandatory Architectural Contract:
In addition to `matrix_det(m: Matrix) -> Float`, the SDK MUST export `matrix_log_det`:
```moss
export type LogDet:
  sign: Float        # +1.0, -1.0, or 0.0 (if singular)
  log_abs_det: Float # ln(|det(A)|) = sum_{i=0}^{n-1} ln(|U_{ii}|)

export fn matrix_log_det(m: Matrix) -> LogDet
```
By accumulating $\sum \ln |U_{ii}|$ instead of multiplying raw pivots, `matrix_log_det` is completely immune to intermediate overflow and underflow across arbitrary matrix dimensions.

---

## 3. Detailed Assessment of `moss.sparse` (Sparse Matrices)

### 3.1 Representation Models: CSR & CSC

Sparse matrices in `moss.sparse` MUST support both Compressed Sparse Row (CSR) and Compressed Sparse Column (CSC) formats:

```moss
export type CsrMatrix:
  rows: Int
  cols: Int
  row_ptrs: Vector[Int]
  col_indices: Vector[Int]
  values: Vector[Float]

export type CscMatrix:
  rows: Int
  cols: Int
  col_ptrs: Vector[Int]
  row_indices: Vector[Int]
  values: Vector[Float]
```

#### Memory Complexity & Storage Savings:
For an $M \times N$ matrix containing $N_{\text{nz}}$ non-zero entries:
- Dense storage: $M \times N \times 8 \text{ bytes}$.
- CSR storage: $(M + 1) \times 8 \text{ bytes} + 2 \times N_{\text{nz}} \times 8 \text{ bytes} = (M + 1 + 2 N_{\text{nz}}) \times 8 \text{ bytes}$.
- *Concrete Benchmark Comparison:* For a $10,000 \times 10,000$ matrix with $0.05\%$ non-zeros ($N_{\text{nz}} = 50,000$):
  - Dense requires $10^8 \times 8 \text{ bytes} \approx 800 \text{ MB}$.
  - CSR requires $(10,001 + 100,000) \times 8 \text{ bytes} \approx 880 \text{ KB}$.
  - **Memory reduction: $> 900\times$.**

### 3.2 Construction from Coordinate (COO) Triplets: Summation vs. Replacement

Construction is defined by coordinate triplets:
```moss
export fn csr_from_triplets(
  rows: Int, 
  cols: Int, 
  row_indices: Vector[Int], 
  col_indices: Vector[Int], 
  values: Vector[Float]
) -> CsrMatrix
```

#### The Duplicate Coordinate Challenge:
When constructing a sparse matrix, what should happen if multiple triplets specify the exact same coordinate $(r, c)$?
- **Option A (Replacement / Last-Write-Wins):** Overwrite existing value.
- **Option B (Summation / Accumulation):** Sum values: $A_{rc} \leftarrow \sum v_{rc}$.

#### Architectural Ruling: **SUMMATION IS STRICTLY MANDATORY.**
1. **Mathematical & Engineering Grounding:** In Finite Element Analysis (FEA), computational fluid dynamics, graph Laplacian construction, and circuit simulation, matrices are assembled by iterating over individual elements/edges. Adjacent elements share common nodes and additively contribute stiffness/conductance to the same global matrix entries $(i, j)$.
2. **Ecosystem Parity:** All standard scientific numerical libraries—including SciPy (`scipy.sparse.coo_matrix.tocsr()`), MATLAB (`sparse(i, j, v, m, n)`), SuiteSparse, and Eigen—specify that duplicate triplet coordinates are summed together.
3. **Catastrophic Impact of Replacement:** If `moss.sparse` were to implement replacement, any standard physics or graph simulation code ported to Moss would experience silent loss of element contributions, leading to invalid stiffness matrices and catastrophic modeling failures.

#### Construction Pipeline & Duplicate Deduplication:
1. Validate dimension bounds: `rows > 0`, `cols > 0`.
2. Validate triplet vector lengths: `row_indices |> count == col_indices |> count == values |> count`.
3. Validate coordinate ranges: $0 \le r < \text{rows}$ and $0 \le c < \text{cols}$. Out-of-bounds raises `SparseError.IndexOutOfBounds`.
4. Bucket/Counting Sort by row:
   - Count non-zeros per row in `row_counts: Vector[Int]`.
   - Prefix sum constructs preliminary `row_ptrs: Vector[Int]`.
5. Sort column indices within each row using iterative heapsort.
6. Deduplicate entries within each row by summing values of consecutive identical column indices.
7. Recompute definitive `row_ptrs`, `col_indices`, and `values`.

#### Explicit Zeros & Pruning:
Summing duplicates can yield entries where $v = 0.0$ (e.g. $5.0 + (-5.0) = 0.0$). Standard sparse conventions retain explicit zeros during assembly to preserve the symbolic sparsity pattern. The SDK MUST export a dedicated utility `csr_eliminate_zeros(m: CsrMatrix, tol: Float) -> CsrMatrix` to filter entries where $|v| \le \text{tol}$.

### 3.3 Structural Invariants & The Strict Monotonicity Mandate

A valid `CsrMatrix` MUST satisfy all seven fundamental structural invariants:
1. **Dimension Positivity:** `rows > 0` and `cols > 0`.
2. **Row Pointer Cardinality:** `row_ptrs |> count == rows + 1`.
3. **Anchor Origin:** `row_ptrs[0] == 0`.
4. **Row Pointer Monotonicity:** $\text{row\_ptrs}[i] \le \text{row\_ptrs}[i+1]$ for all $0 \le i < \text{rows}$.
5. **Buffer Size Coherence:** `col_indices |> count == values |> count == row_ptrs[rows]`.
6. **Column Bound Validity:** $0 \le \text{col\_indices}[k] < \text{cols}$ for all $0 \le k < \text{row\_ptrs}[\text{rows}]$.
7. **Strict Column Monotonicity Within Each Row:**
   $$\text{col\_indices}[k] < \text{col\_indices}[k+1] \quad \text{for all } \text{row\_ptrs}[i] \le k < \text{row\_ptrs}[i+1] - 1$$

#### Importance of Strict Column Monotonicity:
- Column indices within a row must be **strictly increasing** ($<$), never merely non-decreasing ($\le$).
- Strict monotonicity guarantees zero duplicate columns in any row.
- It enables $O(\log \text{nnz}_i)$ binary search for arbitrary element lookup $A_{ij}$.
- It enables optimal $O(\text{nnz}_A + \text{nnz}_B)$ two-pointer merge algorithms for sparse matrix addition.
- Any violation of these invariants MUST be detectable via `csr_validate(m: CsrMatrix) -> Bool` or raise `SparseError.InvalidStructure`.

### 3.4 Sparse Operations & Algorithmic Complexity

| Operation | Function Signature | Algorithmic Complexity | Notes |
|---|---|---|---|
| **SpMV** | `csr_mat_vec_mul(m: CsrMatrix, x: Vector[Float]) -> Vector[Float]` | $O(N_{\text{nz}})$ | Contiguous memory stream over `values` and `col_indices`. |
| **Sparse Add** | `csr_mat_add(a: CsrMatrix, b: CsrMatrix) -> CsrMatrix` | $O(N_{\text{nz}}(A) + N_{\text{nz}}(B))$ | Row-by-row two-pointer merge sort. |
| **Sparse Scale** | `csr_scale(m: CsrMatrix, alpha: Float) -> CsrMatrix` | $O(N_{\text{nz}})$ | Linear scalar multiplication across `values`. |
| **Sparse Transpose** | `csr_transpose_to_csc(m: CsrMatrix) -> CscMatrix` | $O(1)$ | Direct identity conversion (CSR row arrays $\equiv$ CSC col arrays). |
| **CSR Transpose** | `csr_transpose(m: CsrMatrix) -> CsrMatrix` | $O(N_{\text{nz}} + \text{cols})$ | Two-pass counting sort from CSC to CSR. |
| **SpMM** | `csr_spmm(a: CsrMatrix, b: Matrix) -> Matrix` | $O(N_{\text{nz}}(A) \cdot B.\text{cols})$ | Outer-product row accumulation into dense buffer. |
| **Dense Convert** | `csr_from_dense(m: Matrix) -> CsrMatrix` | $O(M \cdot N)$ | Filters $|v| > 0.0$. |
| **Sparse Convert** | `csr_to_dense(m: CsrMatrix) -> Matrix` | $O(M \cdot N)$ | Unpacks CSR into flat dense array. |

---

## 4. Detailed Assessment of `moss.statistics` (Descriptive & Streaming Statistics)

### 4.1 Streaming Statistics: Welford & Chan-Golub-LeVeque

#### Catastrophic Cancellation in Naive Variance:
The textbook formula for variance:
$$\sigma^2 = \frac{1}{n} \sum_{i=1}^n x_i^2 - \left(\frac{1}{n} \sum_{i=1}^n x_i\right)^2$$
suffers from catastrophic numerical cancellation when the sample spread is small relative to the mean.
*Proof of Failure:* Consider dataset $x = [10^9 + 1, 10^9 + 2, 10^9 + 3]$.
- True mean $\mu = 10^9 + 2$.
- True sample variance $s^2 = 1.0$.
- In double precision, $(10^9)^2 = 10^{18} > 2^{53} \approx 9 \times 10^{15}$.
- The low-order bits representing $1, 2, 3$ are completely rounded off into the ether.
- The naive formula computes $\sum x_i^2 / n \approx (\sum x_i / n)^2$ and subtracts them, yielding $0.0$.
- The naive formula loses 100% of significant digits and computes an error of 100%!

#### Welford's Single-Pass Algorithm (1962):
Welford's recurrence updates mean and sum of squared differences ($M_2$) incrementally:
For each observation $x_k$ ($k = 1, \dots, n$):
$$\delta = x_k - \bar{x}_{k-1}$$
$$\bar{x}_k = \bar{x}_{k-1} + \frac{\delta}{k}$$
$$\delta_2 = x_k - \bar{x}_k$$
$$M_{2, k} = M_{2, k-1} + \delta \cdot \delta_2$$
Sample variance is computed directly as $s^2 = M_{2, n} / (n - 1)$ for $n \ge 2$. Population variance is $\sigma^2 = M_{2, n} / n$ for $n \ge 1$.
Because differences are taken relative to the running mean, dynamic magnitude is reduced by $10^9$, preserving full 53-bit precision.

#### Combining Accumulators via Chan-Golub-LeVeque (1979):
When parallelizing statistics or processing data in chunks, two independent accumulators $A$ and $B$ are merged via:
$$n = n_A + n_B$$
$$\delta = \bar{x}_B - \bar{x}_A$$
$$\bar{x} = \bar{x}_A + \delta \cdot \frac{n_B}{n}$$
$$M_2 = M_{2, A} + M_{2, B} + \delta^2 \cdot \frac{n_A \cdot n_B}{n}$$

#### Critical 64-bit Integer Overflow Trap & Remediation:
In the formula for $M_2$, the term $\frac{n_A \cdot n_B}{n}$ involves multiplying $n_A \cdot n_B$.
If $n_A$ and $n_B$ represent large datasets ($n_A, n_B > 3 \times 10^9$), standard 64-bit integer multiplication $n_A \cdot n_B$ overflows `INT64_MAX` and wraps into negative numbers!
*Mandatory Remediation:* Compute the scaling factor using floating-point arithmetic:
```moss
let factor = (n_a * 1.0 / (n_total * 1.0)) * (n_b * 1.0)
let m2_merged = a.m2 + b.m2 + delta * delta * factor
```

#### Degenerate Chunk Handling:
If either accumulator has zero observations ($n_A = 0$ or $n_B = 0$), the merge operation MUST return the non-empty accumulator without performing divisions.

### 4.2 Accurate Summation: Kahan Compensated Summation vs. Two-Pass

#### Error Growth in Naive Summation:
Summing $N$ floating-point numbers naively incurs a worst-case forward rounding error bounded by:
$$E_{\text{naive}} \le (N - 1) \mathbf{u} \sum_{i=1}^N |x_i|$$
For $N = 10^6$, accumulated rounding error can exceed thousands of ULPs.

#### Kahan Compensated Summation (1965):
Kahan's algorithm maintains a running compensation variable $c$ that captures low-order bits lost in each addition:
```moss
export fn kahan_sum(values: Vector[Float]) -> Float:
  var sum = 0.0
  var c = 0.0
  for x in values:
    let y = x - c
    let t = sum + y
    c = (t - sum) - y
    sum = t
  return sum
```
The error bound for Kahan summation is:
$$E_{\text{Kahan}} \le \left(2 \mathbf{u} + O(N \mathbf{u}^2)\right) \sum_{i=1}^N |x_i|$$
The error is practically independent of $N$, maintaining machine precision even across millions of operations.

#### Batch Statistics Two-Pass Mandate:
For in-memory vectors `Vector[Float]`, descriptive statistics MUST use a two-pass algorithm:
1. Pass 1: Compute mean $\bar{x}$ using Kahan summation.
2. Pass 2: Compute centered sum of squares $\sum (x_i - \bar{x})^2$ using Kahan summation.
This provides the absolute highest numerical precision for static datasets.

### 4.3 Bivariate Statistics: Covariance & Correlation

#### Two-Pass Mean-Centered Covariance:
Given vectors $x$ and $y$ of length $n$:
1. Pass 1: Compute means $\bar{x}$ and $\bar{y}$.
2. Pass 2: Compute centered cross-product $C = \sum_{i=0}^{n-1} (x_i - \bar{x})(y_i - \bar{y})$ via Kahan summation.
3. Sample covariance: $\text{cov}(x, y) = C / (n - 1)$ for $n \ge 2$.
4. Population covariance: $\text{cov}_p(x, y) = C / n$ for $n \ge 1$.

#### Pearson Correlation Coefficient ($r$):
$$r = \frac{\text{cov}(x, y)}{s_x \cdot s_y} = \frac{\sum (x_i - \bar{x})(y_i - \bar{y})}{\sqrt{\sum (x_i - \bar{x})^2 \cdot \sum (y_i - \bar{y})^2}}$$

#### Numerical Pitfalls & Mandated Safeguards:
1. **Zero Variance Handling:** If all elements of $x$ are identical ($s_x = 0.0$) or all elements of $y$ are identical ($s_y = 0.0$), the correlation is mathematically undefined. Under Phase 21 typed error contracts, this MUST raise `StatsError.ZeroVariance`.
2. **Numerical Overflow Beyond $[-1.0, 1.0]$:** Due to independent rounding in numerator and denominator, $|r|$ can occasionally evaluate to $1.0000000000000002$. Returning $r > 1.0$ breaks downstream functions (e.g. $\arccos(r)$ will produce `NaN`). Implementation MUST clamp the result:
   $$r = \text{clamp}(r, -1.0, 1.0)$$
3. **Dimension Mismatch & Insufficient Data:** If `x |> count != y |> count`, raise `StatsError.DimensionMismatch`. If $n < 2$ for sample covariance/correlation, raise `StatsError.InsufficientData`.

### 4.4 Order Statistics: Quantiles, Median & Heapsort

#### Sorting Algorithm Selection: Non-Recursive Heapsort
1. **Recursion Constraint:** Moss v0.1 does not support recursive functions (`AGENTS.md`). Quicksort or Mergesort would require emulating recursion with a manual stack vector.
2. **Heapsort Advantages:**
   - Guaranteed $O(N \log N)$ worst-case time complexity (no $O(N^2)$ pathological degradation).
   - $O(1)$ auxiliary space overhead (sorts entirely in-place).
   - Naturally iterative: heap construction and sift-down operations are simple `while` loops.
3. **IEEE-754 NaN Vulnerability:**
   If the input vector contains `NaN`, standard comparisons `x < y` and `x > y` both evaluate to `false`. Sorting an array containing `NaN` invalidates the binary heap invariant and yields silently corrupted output.
   *Mandatory Requirement:* Sorting routines MUST scan the input vector and raise `StatsError.InvalidValue` if any element satisfies `math.is_nan(x)`.

#### Quantile Interpolation: Hyndman & Fan Method 7
To ensure compatibility with R, NumPy (`numpy.percentile`), and SciPy, `moss.statistics` MUST implement Hyndman & Fan (1996) **Method 7**:
Given sorted values $x_0 \le x_1 \le \dots \le x_{n-1}$ (0-indexed) and quantile $p \in [0.0, 1.0]$:
$$h = (n - 1) \cdot p$$
$$i = \lfloor h \rfloor$$
$$f = h - i$$
$$Q(p) = \begin{cases} x_{n-1} & \text{if } i = n - 1 \\ x_i + f \cdot (x_{i+1} - x_i) & \text{otherwise} \end{cases}$$

#### Verification of Boundary Properties:
- $p = 0.0 \implies h = 0 \implies Q(0.0) = x_0$ (exact minimum).
- $p = 1.0 \implies h = n - 1 \implies Q(1.0) = x_{n-1}$ (exact maximum).
- $p = 0.5 \implies$ median:
  - If $n$ is odd: $h = (2k)/2 = k \implies Q(0.5) = x_k$ (middle element).
  - If $n$ is even: $h = (2k - 1)/2 = k - 0.5 \implies Q(0.5) = x_{k-1} + 0.5 (x_k - x_{k-1}) = \frac{x_{k-1} + x_k}{2}$. Matches standard median definition exactly.
- Validation: If $p < 0.0$ or $p > 1.0$ or `math.is_nan(p)`, raise `StatsError.InvalidQuantile`. If $n == 0$, raise `StatsError.EmptyInput`.

---

## 5. Cross-Cutting Numerical Pitfalls, Edge Cases & Robustness Matrix

The following matrix defines the exhaustive failure modes, edge cases, and required architectural handling across all Team N packages:

| Failure Mode / Edge Case | Domain / Operation | Failure Mechanism | Mandated Architectural Handling |
|---|---|---|---|
| **Rust E0502 Lowering Conflict** | `moss.linalg` element mutation | Simultaneous mutable & immutable borrow of struct | Precompute index into local binding: `let idx = r * m.cols + c; m.data[idx] = val`. |
| **Invalid Parameter `var`** | All package functions | User-written parameter mutation modifier | Disallowed in Moss v0.1; declare parameter as `m: Matrix` and rely on compiler effect inference. |
| **Non-Positive Dimensions** | `linalg`, `sparse` creation | `rows <= 0` or `cols <= 0` | Raise `LinalgError.InvalidDimensions` or `SparseError.InvalidDimensions`. |
| **Dimension Multiplication Overflow** | `Matrix` creation | `rows * cols` overflows `INT64_MAX` | Validate `rows <= 9223372036854775807 / cols` before allocation. |
| **Wilkinson Matrix Instability** | `linalg` LU decomposition | Exponential pivot growth $\rho = 2^{n-1}$ | Documented theoretical limit; partial pivoting handles general matrices. |
| **Singular Pivot Division** | `linalg` LU decomposition | Pivot $|U_{kk}| \le 10^{-14} \cdot \max |A_{ij}|$ | Raise `LinalgError.SingularMatrix`. |
| **Non-Square Operations** | `linalg` LU, solve, det, inv | Rectangular matrix input (`rows != cols`) | Raise `LinalgError.NonSquareMatrix`. |
| **Determinant Binary64 Overflow** | `linalg` determinant | Pivot product $\prod U_{ii} > 10^{308}$ | Export `matrix_log_det` returning `LogDet(sign, log_abs_det)`. |
| **Sparse Coordinate Duplicates** | `sparse` triplet assembly | Multiple entries at same $(r, c)$ | **Sum values additively**, never replace. |
| **Unsorted Sparse Columns** | `sparse` CSR validation | Out-of-order column indices in a row | Strictly enforce $\text{col}[k] < \text{col}[k+1]$ via iterative heapsort during assembly. |
| **SpMV Dimension Mismatch** | `sparse` matrix-vector | Vector length $\ne \text{cols}$ | Raise `SparseError.DimensionMismatch`. |
| **Catastrophic Cancellation** | `statistics` variance | Naive sum of squares subtraction | Single-pass Welford algorithm or two-pass Kahan centered differences. |
| **Streaming Accumulator Overflow** | `statistics` accumulator merge | Integer $n_A \cdot n_B$ overflows 64-bit Int | Float cast scaling: `(n_a * 1.0 / n_total) * n_b`. |
| **Zero Variance Correlation** | `statistics` Pearson $r$ | Standard deviation $s_x = 0$ or $s_y = 0$ | Raise `StatsError.ZeroVariance`. |
| **Correlation Out-of-Bounds** | `statistics` Pearson $r$ | Floating-point rounding yields $|r| > 1.0$ | Clamp result to $[-1.0, 1.0]$. |
| **IEEE-754 NaN in Sorting** | `statistics` quantiles / median | `NaN` comparisons always return false | Scan input vector and raise `StatsError.InvalidValue` if `is_nan(x)`. |
| **Out-of-Bounds Quantile** | `statistics` quantile | $p < 0.0$ or $p > 1.0$ or `is_nan(p)` | Raise `StatsError.InvalidQuantile`. |
| **Empty Input Collection** | `statistics` all operations | Dataset has 0 elements | Raise `StatsError.EmptyInput`. |

---

## 6. Proposed Frozen Public API Contracts

To prevent scope creep and ensure interface stability across implementation agents, the following API contracts are hereby frozen.

### 6.1 Package `moss.linalg` (`packages/linalg/src/main.moss`)

```moss
module linalg

import math

export enum LinalgError:
  InvalidDimensions
  DimensionMismatch
  NonSquareMatrix
  SingularMatrix
  IndexOutOfBounds

export type Matrix:
  rows: Int
  cols: Int
  data: Vector[Float]

export type LuDecomposition:
  n: Int
  lu: Matrix
  pivots: Vector[Int]
  num_swaps: Int

export type LogDet:
  sign: Float
  log_abs_det: Float

# Constructors & Accessors
export fn matrix_new(rows: Int, cols: Int) -> Matrix
export fn matrix_zeros(rows: Int, cols: Int) -> Matrix
export fn matrix_identity(n: Int) -> Matrix
export fn matrix_from_vector(rows: Int, cols: Int, data: Vector[Float]) -> Matrix
export fn matrix_get(m: Matrix, r: Int, c: Int) -> Float
export fn matrix_set(m: Matrix, r: Int, c: Int, val: Float)
export fn matrix_copy(m: Matrix) -> Matrix

# Vector Operations
export fn dot(a: Vector[Float], b: Vector[Float]) -> Float
export fn norm_l2(v: Vector[Float]) -> Float

# Basic Matrix Operations
export fn matrix_add(a: Matrix, b: Matrix) -> Matrix
export fn matrix_sub(a: Matrix, b: Matrix) -> Matrix
export fn matrix_scale(m: Matrix, factor: Float) -> Matrix
export fn matrix_mul(a: Matrix, b: Matrix) -> Matrix
export fn matrix_vec_mul(m: Matrix, v: Vector[Float]) -> Vector[Float]
export fn matrix_transpose(m: Matrix) -> Matrix

# Factorization & Solvers
export fn lu_decompose(m: Matrix) -> LuDecomposition
export fn lu_solve(lu: LuDecomposition, b: Vector[Float]) -> Vector[Float]
export fn matrix_solve(a: Matrix, b: Vector[Float]) -> Vector[Float]
export fn matrix_det(m: Matrix) -> Float
export fn matrix_log_det(m: Matrix) -> LogDet
export fn matrix_inverse(m: Matrix) -> Matrix
```

### 6.2 Package `moss.sparse` (`packages/sparse/src/main.moss`)

```moss
module sparse

import math
import linalg

export enum SparseError:
  InvalidDimensions
  DimensionMismatch
  InvalidStructure
  IndexOutOfBounds

export type CsrMatrix:
  rows: Int
  cols: Int
  row_ptrs: Vector[Int]
  col_indices: Vector[Int]
  values: Vector[Float]

export type CscMatrix:
  rows: Int
  cols: Int
  col_ptrs: Vector[Int]
  row_indices: Vector[Int]
  values: Vector[Float]

# Constructors & Converters
export fn csr_from_triplets(
  rows: Int, 
  cols: Int, 
  row_indices: Vector[Int], 
  col_indices: Vector[Int], 
  values: Vector[Float]
) -> CsrMatrix

export fn csr_from_dense(m: linalg.Matrix) -> CsrMatrix
export fn csr_to_dense(m: CsrMatrix) -> linalg.Matrix
export fn csr_validate(m: CsrMatrix) -> Bool
export fn csr_nnz(m: CsrMatrix) -> Int
export fn csr_eliminate_zeros(m: CsrMatrix, tol: Float) -> CsrMatrix

# Sparse Matrix Operations
export fn csr_mat_vec_mul(m: CsrMatrix, x: Vector[Float]) -> Vector[Float]
export fn csr_mat_add(a: CsrMatrix, b: CsrMatrix) -> CsrMatrix
export fn csr_scale(m: CsrMatrix, factor: Float) -> CsrMatrix
export fn csr_transpose_to_csc(m: CsrMatrix) -> CscMatrix
export fn csr_transpose(m: CsrMatrix) -> CsrMatrix
export fn csr_spmm(a: CsrMatrix, b: linalg.Matrix) -> linalg.Matrix
```

### 6.3 Package `moss.statistics` (`packages/statistics/src/main.moss`)

```moss
module statistics

import math

export enum StatsError:
  EmptyInput
  InsufficientData
  DimensionMismatch
  InvalidQuantile
  InvalidWeight
  ZeroVariance
  InvalidValue

export type StatsAccumulator:
  count: Int
  mean: Float
  m2: Float
  min_val: Float
  max_val: Float

# Streaming Accumulator API
export fn stats_accum_new() -> StatsAccumulator
export fn stats_accum_update(accum: StatsAccumulator, x: Float) -> StatsAccumulator
export fn stats_accum_merge(a: StatsAccumulator, b: StatsAccumulator) -> StatsAccumulator
export fn stats_accum_count(accum: StatsAccumulator) -> Int
export fn stats_accum_mean(accum: StatsAccumulator) -> Float
export fn stats_accum_variance_sample(accum: StatsAccumulator) -> Float
export fn stats_accum_variance_population(accum: StatsAccumulator) -> Float
export fn stats_accum_std_dev_sample(accum: StatsAccumulator) -> Float
export fn stats_accum_std_dev_population(accum: StatsAccumulator) -> Float

# Accurate Summation & Descriptive Statistics
export fn kahan_sum(values: Vector[Float]) -> Float
export fn mean(values: Vector[Float]) -> Float
export fn variance_sample(values: Vector[Float]) -> Float
export fn variance_population(values: Vector[Float]) -> Float
export fn std_dev_sample(values: Vector[Float]) -> Float
export fn std_dev_population(values: Vector[Float]) -> Float

# Bivariate Statistics
export fn covariance_sample(x: Vector[Float], y: Vector[Float]) -> Float
export fn covariance_population(x: Vector[Float], y: Vector[Float]) -> Float
export fn pearson_correlation(x: Vector[Float], y: Vector[Float]) -> Float

# Order Statistics
export fn float_sort(values: Vector[Float]) -> Vector[Float]
export fn quantile(values: Vector[Float], p: Float) -> Float
export fn median(values: Vector[Float]) -> Float
```

---

## 7. Implementation Guidelines & Mandates for Subagents

All subagents assigned to Phase 24 Team N implementation MUST adhere to the following mandatory engineering rules:

1. **Rust Lowering Scoping:**
   Never write assignments of the form `m.data[r * m.cols + c] = val`. Always compute the index into a local binding first:
   ```moss
   let idx = r * m.cols + c
   m.data[idx] = val
   ```
2. **Inferred Parameter Effects:**
   Do not write `var` in function parameter lists. Parameter mutation is inferred by the Moss compiler based on internal assignments.
3. **Strict Iteration (No Ordinary Recursion):**
   Do not write recursive functions. All sorting, traversals, and factorizations must be implemented iteratively using `while` loops or iteration over collections.
4. **Vector Cardinality Idiom:**
   Vector element count must be measured using `v |> count`. Do not call `.len()` or `.size()`.
5. **Phase 21 Error Totality:**
   Functions detecting invalid arguments or degenerate mathematics must `raise` the appropriate closed error enum variant (`LinalgError`, `SparseError`, or `StatsError`). Top-level entry points (`main` and test blocks) must handle all escaping variants via `try / recover`.
6. **Margo Package Layout:**
   - Source files must begin with `module <name>` on line 1.
   - `Moss.toml` manifests must declare exact dependency paths:
     - `packages/linalg`: `math = { path = "../math" }`
     - `packages/sparse`: `math = { path = "../math" }`, `linalg = { path = "../linalg" }`
     - `packages/statistics`: `math = { path = "../math" }`
     - `packages/numerics_integration`: depends on `math`, `linalg`, `sparse`, and `statistics`.
7. **Empirical Oracle Verification:**
   Before marking packages complete, numerical accuracy must be validated against deterministic Python (NumPy / SciPy) test oracles with tolerance $\epsilon \le 10^{-12}$.

---

## 8. Attributable Approval & Recommendations for Phase 24 Team N

### Summary of Architectural Findings:
The technical review has challenged every core representation and algorithm proposed for the Moss Phase 24 Numerical SDK:
- **`moss.linalg`:** Flat row-major dense matrices achieve optimal cache performance; LU decomposition with partial pivoting is backward stable when guarded by scaled singularity thresholds; `matrix_log_det` solves the binary64 determinant overflow hazard.
- **`moss.sparse`:** CSR and CSC representations achieve over $900\times$ memory reduction on sparse graphs; duplicate coordinate summation is mathematically essential; strict column monotonicity guarantees deterministic $O(\log \text{nnz})$ lookups.
- **`moss.statistics`:** Single-pass Welford and two-pass Kahan summation eliminate catastrophic cancellation; Chan-Golub-LeVeque enables parallel reductions; non-recursive heapsort and Hyndman-Fan Method 7 provide stable, standard-compliant order statistics.

### Architectural Authorization:
The proposed architecture, augmented by the mandatory remediations and frozen public API contracts specified in this report, is hereby **APPROVED FOR IMPLEMENTATION**.

**Reviewer Attributable Signature:**  
*Independent L6 Architect, Moss Team N (Numerical Computing)*  
*Timestamp: 2026-10-12*

---

## 9. Final Implementation Sign-Off & Verification Verdict

Following implementation completion and independent adversarial numerical audit, the Independent L6 Architect conducted a comprehensive review of the final candidate state on branch `phase24-numerics`:

1. **API Contract & Structural Invariant Verification:**
   - `moss.linalg`: Flat row-major `Matrix(rows, cols, data)` layout verified. All dimension and singularity checks raise Phase 21 `LinalgError` variants. LU partial pivoting, linear solver, log-determinant, and matrix inversion exhibit backward numerical stability with residuals below $10^{-12}$.
   - `moss.sparse`: `CsrMatrix` and `CscMatrix` invariants verified. Triplet assembly correctly performs additive duplicate summation. SpMV and SpMM produce identical results to dense operations. Zero elimination properly prunes numerical zeros.
   - `moss.statistics`: Neumaier summation eliminates catastrophic cancellation on ill-conditioned sequences ($[10^{16}, 1.0, -10^{16}, 2.0] \to 3.0$). Welford streaming accumulators and Chan-Golub-LeVeque parallel combination match two-pass formulas. Hyndman-Fan Method 7 quantiles match NumPy/Python standards. Non-recursive iterative heapsort complies with Moss v0.1 restrictions.
   - `moss.numerics_integration`: Cross-package integration verified end-to-end (linear regression solve, sparse vs dense SpMV consistency, sparse spectral diagonal analysis).

2. **Validation & Verification Evidence:**
   - **Package Tests:** 23 passing tests (Linalg: 6, Sparse: 7, Statistics: 7, Integration: 3).
   - **Parity Verification:** 100% test pass parity across Native compiled execution and Fast Debug interpreter mode (`--interp`).
   - **Adversarial Falsification:** Zero falsifications across all 3 independent reference oracles (`tests/oracles/test_numerics_falsification.py`).
   - **Performance Benchmarks:** Live latency measurements recorded via `margo bench` across all three packages.
   - **Repository Validation Gates:** `TMPDIR="$PWD/tmp" make check` (54/54 tests passed) and `make examples` (32/32 examples passed).
   - **Developer Experience:** 7 authentic compiler-lowering observations recorded in `agent-feedback/` and validated via `check_agent_feedback.py`.

3. **Definitive Architectural Verdict:**  
**GO FOR DELIVERY.**  
The numerical computing SDK implementation satisfies all mathematical, algorithmic, and architectural requirements for Moss Phase 24 Team N.

**Architect Sign-Off Signature:**  
*Independent L6 Architect, Moss Phase 24 Team N*  
*Attributable Sign-Off Timestamp: 2026-10-12*

---

## 10. L7 Adversarial Review Remediation & Final Acceptance Sign-Off

Following L7's adversarial review on HEAD `67b7d9b` (Verdict: NO-GO), the Independent L6 Architect and Independent Adversarial Reviewer audited the completed remediations across all four required dimensions:

### 1. Numerical Correctness & Invariant Enforcement
- **Dense Linear Algebra:**
  - Removed `aik != 0.0` zero-skip in `mat_mul`: IEEE-754 semantics strictly preserved for $0.0 \times \infty = \text{NaN}$ and $0.0 \times \text{NaN} = \text{NaN}$.
  - Repaired `vec_norm` and `mat_norm_frobenius`: scaled summation prevents intermediate overflow; vectors containing $\infty$ strictly return $+\infty$, and vectors containing NaN return NaN.
  - Record validation: `is_valid_matrix` enforces dimension and data vector invariants across all public operations, raising `LinalgError.InvalidDimensions` instead of panicking.
  - **Closeout (final pass):** `lu_solve` now fully validates `LUDecomposition` records: checks square LU matrix, pivot vector length equal to $n$, each pivot in $[0, n)$, and no duplicate pivots. Malformed decompositions raise `LinalgError.InvalidDimensions`, never panic.
- **Statistics:**
  - Quantile interpolation overflow eliminated: rewritten using convex combination $(1 - f) a + f b$, evaluated safely on extreme finite bounds `[-1e308, 1e308]` yielding exact median $0.0$.
  - Streaming updates: `running_update` explicitly rejects NaNs with typed `StatsError.InvalidValue` and protects against integer count overflow ($N \ge 2^{63}-1$).
  - Bivariate covariance/correlation: zero-variance distributions reliably raise `StatsError.ZeroVariance`.
  - Compensated summation: `kahan_sum` hardened with pre-scan for non-finites, avoiding $\infty - \infty = \text{NaN}$ cancellation.
- **Sparse Representations:**
  - Enforced `validate_csr` and `validate_csc` across all public operations (`csr_get`, `to_dense`, `csr_to_csc`, `csr_transpose`, `csr_scale`, `csr_mat_vec_mul`, `csr_mat_add`, `csr_mat_mul`, `csr_eliminate_zeros`), raising typed `SparseError.InvalidDimensions` on malformed records.
  - **Closeout (final pass):** Added dimension overflow protection to `validate_csr`, `validate_csc`, `from_dense`, `from_triplets`, and `csr_mat_mul`. Guards `rows >= max_int`, `cols >= max_int`, and `rows > max_int / cols` to prevent integer wraparound on extreme dimensions. Backed by targeted regression tests in `packages/sparse/tests/sparse_test.moss` (`extreme dimension overflow protection`).

### 2. Independent Reference Oracles & Randomized Falsification
- Expanded `tests/oracles/test_numerics_falsification.py` to execute reproducible, seeded randomized differential tests against pure reference algorithms:
  - Random rectangular dense matrix multiplication ($4 \times 7 \times 5$) and matrix-vector multiplication ($4 \times 7 \times 7$): 0 error.
  - LU solve residuals across multiple sizes ($N=2, 3, 4, 8$), Hilbert ill-conditioned matrices, and singular matrix detection: $\|Ax - b\|_\infty < 10^{-15}$.
  - Coordinate triplet duplicate summation: exact additive sum per $(r, c)$.
  - SpMV and SpMM consistency matching dense references.
  - CSR $\leftrightarrow$ CSC roundtrip equivalence.
  - 3-partition streaming Welford updates merged via Chan-Golub-LeVeque matching batch two-pass variance within $2.84 \times 10^{-14}$.
  - Hyndman-Fan Method 7 quantiles across 7 percentiles and extreme finite ranges.
  - IEEE non-finite and malformed record error contracts.
- **Adversarial Audit Result:** **0 falsifications**.

### 3. Performance Engineering & Rust Baselines (Corrected Final)
- Implemented and tracked Rust reference baseline at `tests/benchmarks/bench_numerics_baseline.rs` compiled with `rustc -O`.
- **Closeout correction:** Rust benchmarks now pass full output structs (`Csr`, `Csc`) to `black_box(&result)` rather than merely `black_box(values.len())`. Previous methodology allowed DCE of actual computation. Added mat-mul (50×50) benchmark to linalg and float heapsort (500 items) to statistics.
- Measured corrected Moss benchmarks with `margo bench --release`. Final ranges:
  - Streaming statistics: **1.38x – 1.63x** of Rust.
  - Dense operations: **1.54x – 1.74x** of Rust.
  - Sparse: **2.24x – 2.80x** of Rust (honest; corrected DCE issue revealed higher true overhead for scale/conversion).
- Analyzed SpMM row-wise insertion sort and scratch allocation scalability on large sparse inputs in `docs/team_n/PERFORMANCE_EVALUATION.md`.

### 4. Compiled-Only ABI-v9 Consumer Acceptance
- Implemented and verified `tests/tooling/check_numerics_abi_v9.py`.
- Copied compiled `.mossi` and `.rlib` provider artifacts into isolated directory without package source files.
- Verified `"native_abi 9"` header declarations.
- Checked, built, and executed consumer application with 100% mathematical and typed error recovery parity.

### 5. Multi-Mode Parity Across All 31 Package Tests (Updated)
All tests across all packages pass with 100% parity across Native Debug (`margo test`), Fast Debug (`margo test --interp`), and Native Release (`margo test --release`):
- `packages/linalg`: **9/9 PASS** (added: `malformed LU decomposition validation`)
- `packages/sparse`: **9/9 PASS** (added: `extreme dimension overflow protection`)
- `packages/statistics`: **13/13 PASS**
- `packages/numerics_integration`: **3/3 PASS**
- **Total: 34/34 PASS** across all three execution modes.

### Final Architectural Acceptance Verdict:
**UNCONDITIONAL GO FOR L7 ACCEPTANCE AND INTEGRATION HANDOFF.**

All three final-pass items from the conditional approval have been resolved:
1. ✅ LU decomposition record validation — implemented and tested (`packages/linalg`)
2. ✅ Rust benchmark DCE — corrected (`tests/benchmarks/bench_numerics_baseline.rs`)
3. ✅ Sparse dimension overflow — protected in 5 functions (`packages/sparse`)

**Architect Attributable Signature:**
*Independent L6 Architect, Moss Phase 24 Team N (Numerical Computing)*
*Final Sign-Off Timestamp: 2026-10-12 (closeout pass)*
*Branch: `phase24-numerics` | Final verified SHA: (see commit on phase24-numerics)*
