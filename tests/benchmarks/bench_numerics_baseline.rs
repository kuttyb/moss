use std::hint::black_box;
use std::time::Instant;

fn measure(label: &str, mut body: impl FnMut()) {
    // Warmup
    for _ in 0..5 {
        for _ in 0..100 {
            body();
        }
    }
    let mut samples = Vec::with_capacity(31);
    for _ in 0..31 {
        let start = Instant::now();
        for _ in 0..100 {
            body();
        }
        let elapsed = start.elapsed().as_nanos();
        samples.push(elapsed.div_ceil(100));
    }
    samples.sort_unstable();
    println!(
        "{label}\n\nmedian: {} ns\np25: {} ns\np75: {} ns\nsamples: 31\n",
        samples[15], samples[7], samples[23]
    );
}

// -----------------------------------------------------------------------------
// Dense Linear Algebra Benchmarks
// -----------------------------------------------------------------------------

fn bench_dense_vec_norm_1000d() {
    let mut v = Vec::<f64>::with_capacity(1000);
    let mut x = 0.0;
    for _ in 0..1000 {
        v.push(x * 0.05);
        x += 1.0;
    }
    let v = black_box(v);

    let mut max_val = 0.0f64;
    for &val in &v {
        let a = val.abs();
        if a > max_val {
            max_val = a;
        }
    }
    if max_val == 0.0 {
        black_box(0.0);
        return;
    }
    let mut sum_sq = 0.0;
    for &val in &v {
        let sc = val / max_val;
        sum_sq += sc * sc;
    }
    black_box(max_val * sum_sq.sqrt());
}

fn bench_dense_frobenius_50x50() {
    let rows = 50;
    let cols = 50;
    let mut data = Vec::<f64>::with_capacity(rows * cols);
    let mut x = 0.0;
    for _ in 0..(rows * cols) {
        data.push(x * 0.01);
        x += 1.0;
    }
    let data = black_box(data);

    let mut max_val = 0.0f64;
    for &val in &data {
        let a = val.abs();
        if a > max_val {
            max_val = a;
        }
    }
    if max_val == 0.0 {
        black_box(0.0);
        return;
    }
    let mut sum_sq = 0.0;
    for &val in &data {
        let sc = val / max_val;
        sum_sq += sc * sc;
    }
    black_box(max_val * sum_sq.sqrt());
}

fn bench_dense_mat_mul_50x50() {
    let n = 50;
    let mut a = vec![0.0f64; n * n];
    let mut b = vec![0.0f64; n * n];
    for i in 0..(n * n) {
        a[i] = (i as f64) * 0.01;
        b[i] = (i as f64) * 0.02;
    }
    let a = black_box(a);
    let b = black_box(b);
    let mut c = vec![0.0f64; n * n];

    for i in 0..n {
        for k in 0..n {
            let aik = a[i * n + k];
            for j in 0..n {
                c[i * n + j] += aik * b[k * n + j];
            }
        }
    }
    black_box(c[0]);
}

// -----------------------------------------------------------------------------
// Sparse Matrix Benchmarks
// -----------------------------------------------------------------------------

struct Csr {
    rows: usize,
    cols: usize,
    row_ptrs: Vec<usize>,
    col_indices: Vec<usize>,
    values: Vec<f64>,
}

fn make_test_csr(n: usize) -> Csr {
    let mut row_ptrs = Vec::with_capacity(n + 1);
    let mut col_indices = Vec::new();
    let mut values = Vec::new();
    row_ptrs.push(0);
    let mut offset = 0;
    let mut x = 1.0;
    for r in 0..n {
        col_indices.push(r);
        values.push(x * 0.5);
        offset += 1;
        if r + 1 < n {
            col_indices.push(r + 1);
            values.push(x * 0.25);
            offset += 1;
        }
        row_ptrs.push(offset);
        x += 1.0;
    }
    Csr {
        rows: n,
        cols: n,
        row_ptrs,
        col_indices,
        values,
    }
}

fn bench_sparse_scale_500x500() {
    let csr = make_test_csr(500);
    let csr = black_box(csr);
    let mut scaled_values = Vec::with_capacity(csr.values.len());
    for &v in &csr.values {
        scaled_values.push(v * 3.5);
    }
    black_box(scaled_values.len());
}

fn bench_sparse_transpose_200x200() {
    let csr = make_test_csr(200);
    let csr = black_box(csr);
    let n_rows = csr.rows;
    let n_cols = csr.cols;
    let nnz = csr.values.len();

    let mut col_counts = vec![0usize; n_cols];
    for &col in &csr.col_indices {
        col_counts[col] += 1;
    }

    let mut t_row_ptrs = vec![0usize; n_cols + 1];
    let mut current_offset = 0;
    for c in 0..n_cols {
        t_row_ptrs[c] = current_offset;
        current_offset += col_counts[c];
    }
    t_row_ptrs[n_cols] = current_offset;

    let mut next_pos = t_row_ptrs[..n_cols].to_vec();
    let mut t_col_indices = vec![0usize; nnz];
    let mut t_values = vec![0.0f64; nnz];

    for r in 0..n_rows {
        for idx in csr.row_ptrs[r]..csr.row_ptrs[r + 1] {
            let col = csr.col_indices[idx];
            let val = csr.values[idx];
            let dest = next_pos[col];
            t_col_indices[dest] = r;
            t_values[dest] = val;
            next_pos[col] += 1;
        }
    }
    black_box(t_values.len());
}

fn bench_sparse_to_csc_200x200() {
    let csr = make_test_csr(200);
    let csr = black_box(csr);
    let n_rows = csr.rows;
    let n_cols = csr.cols;
    let nnz = csr.values.len();

    let mut col_counts = vec![0usize; n_cols];
    for &col in &csr.col_indices {
        col_counts[col] += 1;
    }

    let mut col_ptrs = vec![0usize; n_cols + 1];
    let mut current_offset = 0;
    for c in 0..n_cols {
        col_ptrs[c] = current_offset;
        current_offset += col_counts[c];
    }
    col_ptrs[n_cols] = current_offset;

    let mut next_pos = col_ptrs[..n_cols].to_vec();
    let mut row_indices = vec![0usize; nnz];
    let mut values = vec![0.0f64; nnz];

    for r in 0..n_rows {
        for idx in csr.row_ptrs[r]..csr.row_ptrs[r + 1] {
            let col = csr.col_indices[idx];
            let val = csr.values[idx];
            let dest = next_pos[col];
            row_indices[dest] = r;
            values[dest] = val;
            next_pos[col] += 1;
        }
    }
    black_box(values.len());
}

// -----------------------------------------------------------------------------
// Statistics Benchmarks
// -----------------------------------------------------------------------------

struct RunningStats {
    count: usize,
    mean: f64,
    m2: f64,
    min: f64,
    max: f64,
}

impl RunningStats {
    fn new() -> Self {
        Self {
            count: 0,
            mean: 0.0,
            m2: 0.0,
            min: f64::INFINITY,
            max: f64::NEG_INFINITY,
        }
    }

    fn update(&mut self, x: f64) {
        self.count += 1;
        let delta = x - self.mean;
        self.mean += delta / (self.count as f64);
        let delta2 = x - self.mean;
        self.m2 += delta * delta2;
        if x < self.min {
            self.min = x;
        }
        if x > self.max {
            self.max = x;
        }
    }

    fn combine(&self, b: &Self) -> Self {
        if self.count == 0 {
            return Self { ..*b };
        }
        if b.count == 0 {
            return Self { ..*self };
        }
        let total_count = self.count + b.count;
        let delta = b.mean - self.mean;
        let combined_mean =
            self.mean + delta * (b.count as f64) / (total_count as f64);
        let combined_m2 = self.m2
            + b.m2
            + delta * delta * (self.count as f64) * (b.count as f64)
                / (total_count as f64);
        let combined_min = self.min.min(b.min);
        let combined_max = self.max.max(b.max);
        Self {
            count: total_count,
            mean: combined_mean,
            m2: combined_m2,
            min: combined_min,
            max: combined_max,
        }
    }
}

fn bench_streaming_welford_1000() {
    let mut rs = RunningStats::new();
    let mut x = 0.0;
    for _ in 0..1000 {
        rs.update(x * 0.1);
        x += 1.0;
    }
    black_box(rs.mean);
}

fn bench_streaming_welford_5000() {
    let mut rs = RunningStats::new();
    let mut x = 0.0;
    for _ in 0..5000 {
        rs.update((x * 0.05) - 25.0);
        x += 1.0;
    }
    black_box(rs.mean);
}

fn bench_cgl_combine_500() {
    let mut a = RunningStats::new();
    let mut b = RunningStats::new();
    let mut x = 0.0;
    for _ in 0..500 {
        a.update(x * 0.1);
        b.update((x + 500.0) * 0.1);
        x += 1.0;
    }
    let c = a.combine(&b);
    black_box(c.mean);
}

fn sift_down_f64(values: &mut [f64], start: usize, end: usize) {
    let mut root = start;
    while root * 2 + 1 < end {
        let mut child = root * 2 + 1;
        if child + 1 < end && values[child] < values[child + 1] {
            child += 1;
        }
        if values[root] < values[child] {
            values.swap(root, child);
            root = child;
        } else {
            return;
        }
    }
}

fn heapsort_f64(values: &mut [f64]) {
    let n = values.len();
    if n <= 1 {
        return;
    }
    let mut start = n / 2;
    while start > 0 {
        start -= 1;
        sift_down_f64(values, start, n);
    }
    let mut end = n;
    while end > 1 {
        end -= 1;
        values.swap(0, end);
        sift_down_f64(values, 0, end);
    }
}

fn bench_heapsort_500() {
    let mut data = Vec::with_capacity(500);
    for i in (0..500).rev() {
        data.push(i as f64);
    }
    let mut data = black_box(data);
    heapsort_f64(&mut data);
    black_box(data[0]);
}

fn main() {
    println!("=== RUST REFERENCE BENCHMARKS (RELEASE -O) ===\n");
    measure("linalg vector euclidean norm 1000d", bench_dense_vec_norm_1000d);
    measure("linalg matrix frobenius norm 50x50", bench_dense_frobenius_50x50);
    measure("linalg matrix multiplication 50x50", bench_dense_mat_mul_50x50);
    measure("sparse csr scale 500x500", bench_sparse_scale_500x500);
    measure("sparse csr transpose 200x200", bench_sparse_transpose_200x200);
    measure("sparse csr to csc conversion 200x200", bench_sparse_to_csc_200x200);
    measure("statistics streaming welford updates 1000 items", bench_streaming_welford_1000);
    measure("statistics streaming welford updates 5000 items", bench_streaming_welford_5000);
    measure("statistics streaming chan golub leveque parallel combine", bench_cgl_combine_500);
    measure("statistics float heapsort 500 items", bench_heapsort_500);
}
