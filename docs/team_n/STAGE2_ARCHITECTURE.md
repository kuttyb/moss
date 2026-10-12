# Moss Phase 24 Team N — Stage 2: Architecture Specification

**Team:** Team N (Numerical Computing)  
**Role:** L6 Coordinator / Architect  
**Branch:** `phase24-numerics`  

---

## 1. Package Overview & Dependencies

```
        +-------------------+
        |     moss.math     |  (Scalar math, IEEE-754, transcendentals)
        +---------+---------+
                  |
         +--------+--------+
         |                 |
         v                 v
+-----------------+ +--------------------+
|   moss.linalg   | |  moss.statistics   |
+--------+--------+ +--------------------+
         |
         v
+-----------------+
|   moss.sparse   |
+-----------------+
```

- **`moss.linalg` (`packages/linalg`)**:
  - Dependent on: `moss.math`
  - Core role: Dense row-major float matrices, vector operations, basic linear system solvers via LU decomposition with partial pivoting.
- **`moss.sparse` (`packages/sparse`)**:
  - Dependent on: `moss.linalg`
  - Core role: Compressed Sparse Row (CSR) and Compressed Sparse Column (CSC) representations, triplet conversion, SpMV, SpMM, and sparse addition.
- **`moss.statistics` (`packages/statistics`)**:
  - Dependent on: `moss.math`
  - Core role: Descriptive statistics (mean, variance, std dev, min/max), stable Kahan/two-pass summation, Welford streaming accumulators, covariance/correlation, quantiles and percentiles.
- **`moss.numerics_integration` (`packages/numerics_integration`)**:
  - Dependent on: `moss.linalg`, `moss.sparse`, `moss.statistics`, `moss.math`
  - Core role: End-to-end integration application, cross-package workflows (e.g. sparse-to-dense regression, statistical analysis of matrix spectra/residuals), and acceptance test gates.

---

## 2. Package A: `moss.linalg` Specification

### 2.1 Types & Error Contracts
```moss
export enum LinalgError:
  DimensionMismatch
  SingularMatrix
  InvalidShape
  NonSquareMatrix

export type Matrix:
  rows: Int
  cols: Int
  data: Vector[Float]
```

### 2.2 Invariants & Bounds Rules
- `rows > 0` and `cols > 0`. Any construction with non-positive dimensions raises `LinalgError.InvalidShape`.
- `data.length() == rows * cols`.
- Flat row-major indexing formula: `idx = r * cols + c`.
- In-place mutation rule (avoiding Rust E0502 borrow conflict):
  ```moss
  idx = r * m.cols + c
  m.data[idx] = val
  ```

### 2.3 Dense Linear Algebra API
1. **Constructors & Initializers:**
   - `zeros(rows: Int, cols: Int) -> Matrix`
   - `ones(rows: Int, cols: Int) -> Matrix`
   - `identity(n: Int) -> Matrix` (square identity matrix of size $n \times n$)
   - `from_vector(rows: Int, cols: Int, data: Vector[Float]) -> Matrix` (validates length)
   - `get(m: Matrix, r: Int, c: Int) -> Float` (validates bounds $[0, \text{rows}-1] \times [0, \text{cols}-1]$)
   - `set(m: Matrix, r: Int, c: Int, val: Float)` (in-place mutation)
2. **Vector Space Operations (`Vector[Float]`):**
   - `vec_dot(a: Vector[Float], b: Vector[Float]) -> Float` (raises `DimensionMismatch` if lengths differ)
   - `vec_norm(v: Vector[Float]) -> Float` ($L_2$ Euclidean norm)
   - `vec_norm_l1(v: Vector[Float]) -> Float` ($L_1$ Manhattan norm)
   - `vec_norm_inf(v: Vector[Float]) -> Float` ($L_\infty$ Chebyshev norm)
   - `vec_add(a: Vector[Float], b: Vector[Float]) -> Vector[Float]`
   - `vec_sub(a: Vector[Float], b: Vector[Float]) -> Vector[Float]`
   - `vec_scale(v: Vector[Float], s: Float) -> Vector[Float]`
   - `vec_outer(a: Vector[Float], b: Vector[Float]) -> Matrix` (outer product $a b^T$)
3. **Matrix Transformations & Arithmetic:**
   - `mat_add(a: Matrix, b: Matrix) -> Matrix` (raises `DimensionMismatch` if shapes differ)
   - `mat_sub(a: Matrix, b: Matrix) -> Matrix`
   - `mat_scale(a: Matrix, s: Float) -> Matrix`
   - `transpose(a: Matrix) -> Matrix` (returns $a^T$ of shape $(\text{cols}, \text{rows})$)
   - `mat_vec_mul(a: Matrix, v: Vector[Float]) -> Vector[Float]` ($A v$)
   - `mat_mul(a: Matrix, b: Matrix) -> Matrix` ($A B$, requires $A.\text{cols} == B.\text{rows}$)
   - `trace(a: Matrix) -> Float` (sum of diagonal entries, requires square matrix)
   - `mat_norm_frobenius(a: Matrix) -> Float` ($\sqrt{\sum A_{i,j}^2}$)
4. **Factorization & Solvers:**
   - `export type LUDecomposition: lu: Matrix, pivots: Vector[Int], det_sign: Float`
   - `lu_factorize(a: Matrix) -> LUDecomposition` (Doolittle algorithm with row pivoting; raises `NonSquareMatrix` if not square, `SingularMatrix` if pivot $< 10^{-14}$)
   - `lu_solve(decomp: LUDecomposition, b: Vector[Float]) -> Vector[Float]` (forward and back substitution)
   - `solve(a: Matrix, b: Vector[Float]) -> Vector[Float]` (direct linear system solver $A x = b$)
   - `determinant(a: Matrix) -> Float` (product of diagonal elements $\times \text{det\_sign}$)
   - `invert(a: Matrix) -> Matrix` (solves $A X = I$)

---

## 3. Package B: `moss.sparse` Specification

### 3.1 Types & Error Contracts
```moss
export enum SparseError:
  DimensionMismatch
  InvalidStructure
  IndexOutOfBounds
  InvalidShape

export type Triplet:
  row: Int
  col: Int
  value: Float

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

### 3.2 Invariants & Construction Rules
- `row_ptrs.length() == rows + 1`.
- `row_ptrs[0] == 0` and `row_ptrs[i] <= row_ptrs[i+1]`.
- Total non-zeros $\text{nnz} = \text{row\_ptrs}[\text{rows}] == \text{col\_indices.length()} == \text{values.length()}$.
- Within each row slice `row_ptrs[i] .. row_ptrs[i+1]`, column indices must be strictly ascending: `col_indices[k] < col_indices[k+1]`.
- Coordinate construction (`from_triplets`) aggregates duplicate coordinates by summing their values.

### 3.3 Sparse Operations API
1. **Construction & Interop:**
   - `from_triplets(rows: Int, cols: Int, triplets: Vector[Triplet]) -> CsrMatrix`
   - `from_dense(m: linalg.Matrix) -> CsrMatrix`
   - `to_dense(m: CsrMatrix) -> linalg.Matrix`
   - `nnz(m: CsrMatrix) -> Int`
   - `density(m: CsrMatrix) -> Float`
   - `csr_get(m: CsrMatrix, r: Int, c: Int) -> Float` (binary search in row slice)
   - `validate_csr(m: CsrMatrix) -> Bool`
2. **Conversions & Structural Transforms:**
   - `csr_to_csc(m: CsrMatrix) -> CscMatrix`
   - `csc_to_csr(m: CscMatrix) -> CsrMatrix`
   - `csr_transpose(m: CsrMatrix) -> CsrMatrix`
   - `csr_scale(m: CsrMatrix, s: Float) -> CsrMatrix`
3. **Linear Algebra on Sparse Matrices:**
   - `csr_mat_vec_mul(m: CsrMatrix, v: Vector[Float]) -> Vector[Float]` ($y = A x$ in $O(\text{nnz})$ time)
   - `csr_mat_add(a: CsrMatrix, b: CsrMatrix) -> CsrMatrix` (two-pointer merge of sorted row slices)
   - `csr_mat_mul(a: CsrMatrix, b: CsrMatrix) -> CsrMatrix` (sparse matrix-matrix multiplication)

---

## 4. Package C: `moss.statistics` Specification

### 4.1 Types & Error Contracts
```moss
export enum StatsError:
  EmptyInput
  InsufficientData
  DimensionMismatch
  InvalidWeight
  InvalidQuantile

export type StatsSummary:
  count: Int
  min_val: Float
  max_val: Float
  mean: Float
  variance: Float
  std_dev: Float

export type RunningStats:
  count: Int
  mean: Float
  m2: Float
  min_val: Float
  max_val: Float
```

### 4.2 Numerical Stability Rules
- **Summation:** Kahan compensated summation or two-pass summation to protect against catastrophic cancellation.
- **Variance:** Welford's algorithm or two-pass mean-centered formula ($S = \sum (x_i - \bar{x})^2$). Naïve single-pass $\sum x^2 - (\sum x)^2 / N$ is strictly prohibited.
- **Sample vs Population:** Flag `sample: Bool` determines denominator ($N-1$ when `sample=true`, $N$ when `sample=false`).
- **Quantiles:** Linear interpolation between nearest ranks (NIST / NumPy method 7).
- **Zero Variance:** Correlation between constant variables returns $0.0$.

### 4.3 Statistics API
1. **Descriptive Aggregations:**
   - `sum(v: Vector[Float]) -> Float`
   - `mean(v: Vector[Float]) -> Float` (raises `StatsError.EmptyInput`)
   - `weighted_mean(v: Vector[Float], w: Vector[Float]) -> Float` (raises `DimensionMismatch` or `InvalidWeight`)
   - `variance(v: Vector[Float], sample: Bool) -> Float` (raises `InsufficientData` if sample=true and $N < 2$)
   - `std_dev(v: Vector[Float], sample: Bool) -> Float`
   - `min_value(v: Vector[Float]) -> Float`
   - `max_value(v: Vector[Float]) -> Float`
   - `data_range(v: Vector[Float]) -> Float`
   - `summary(v: Vector[Float]) -> StatsSummary`
2. **Bivariate Analysis:**
   - `covariance(x: Vector[Float], y: Vector[Float], sample: Bool) -> Float`
   - `correlation(x: Vector[Float], y: Vector[Float]) -> Float`
3. **Order Statistics & Quantiles:**
   - `sort_floats(v: Vector[Float]) -> Vector[Float]` (stable in-place or copied sort)
   - `median(v: Vector[Float]) -> Float`
   - `quantile(v: Vector[Float], q: Float) -> Float` (with $q \in [0.0, 1.0]$)
   - `percentile(v: Vector[Float], p: Float) -> Float`
   - `iqr(v: Vector[Float]) -> Float`
4. **Streaming / Online Statistics (Welford):**
   - `new_running_stats() -> RunningStats`
   - `running_update(s: RunningStats, x: Float)` (in-place Welford update)
   - `running_count(s: RunningStats) -> Int`
   - `running_mean(s: RunningStats) -> Float`
   - `running_variance(s: RunningStats, sample: Bool) -> Float`
   - `running_std_dev(s: RunningStats, sample: Bool) -> Float`
   - `running_combine(a: RunningStats, b: RunningStats) -> RunningStats` (Chan-Golub-LeVeque parallel merge)
