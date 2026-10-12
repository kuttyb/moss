use std::hint::black_box;
use std::time::Instant;

fn sift_down(values: &mut [i64], start: usize, end: usize) {
    let mut root = start;
    while root < end / 2 {
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

fn sort_heapsort(values: &mut [i64]) {
    let n = values.len();
    let mut start = n / 2;
    while start > 0 {
        start -= 1;
        sift_down(values, start, n);
    }
    let mut end = n;
    while end > 1 {
        end -= 1;
        values.swap(0, end);
        sift_down(values, 0, end);
    }
}

fn measure(label: &str, mut body: impl FnMut()) {
    for _ in 0..5 {
        for _ in 0..1_000 {
            body();
        }
    }
    let mut samples = Vec::with_capacity(31);
    for _ in 0..31 {
        let start = Instant::now();
        for _ in 0..1_000 {
            body();
        }
        let elapsed = start.elapsed().as_nanos();
        samples.push(elapsed.div_ceil(1_000));
    }
    samples.sort_unstable();
    println!("{label}: median={} ns p25={} ns p75={} ns", samples[15], samples[7], samples[23]);
}

fn main() {
    measure("sort 256 descending Ints", || {
        let mut values = Vec::<i64>::new();
        let mut i = 256;
        while i > 0 {
            values.push(i);
            i -= 1;
        }
        let mut values = black_box(values);
        values.sort_unstable();
        black_box(values[0]);
    });
    measure("same heapsort 256 descending Ints", || {
        let mut values = Vec::<i64>::new();
        let mut i = 256;
        while i > 0 {
            values.push(i);
            i -= 1;
        }
        let mut values = black_box(values);
        sort_heapsort(&mut values);
        black_box(values[0]);
    });
    measure("sort 256 permuted Ints", || {
        let mut values = Vec::<i64>::new();
        let mut i = 0;
        while i < 256 {
            values.push((i * 73) % 257);
            i += 1;
        }
        let mut values = black_box(values);
        values.sort_unstable();
        black_box(values[0]);
    });
    measure("same heapsort 256 permuted Ints", || {
        let mut values = Vec::<i64>::new();
        let mut i = 0;
        while i < 256 {
            values.push((i * 73) % 257);
            i += 1;
        }
        let mut values = black_box(values);
        sort_heapsort(&mut values);
        black_box(values[0]);
    });
    measure("copy 512 owned bytes with validation", || {
        let mut values = Vec::<i64>::new();
        let mut i = 0;
        while i < 512 {
            values.push(i % 256);
            i += 1;
        }
        let values = black_box(values);
        assert!(values.iter().all(|&x| (0..=255).contains(&x)));
        let copied = values.clone();
        black_box(copied[511]);
    });
    measure("same byte validation and push-copy", || {
        let mut values = Vec::<i64>::new();
        let mut i = 0;
        while i < 512 {
            values.push(i % 256);
            i += 1;
        }
        let values = black_box(values);
        assert!(values.iter().all(|&x| (0..=255).contains(&x)));
        let mut copied = Vec::<i64>::new();
        for &value in &values {
            copied.push(value);
        }
        black_box(copied[511]);
    });
    measure("copy 512 owned bytes raw clone", || {
        let mut values = Vec::<i64>::new();
        let mut i = 0;
        while i < 512 {
            values.push(i % 256);
            i += 1;
        }
        let values = black_box(values);
        let copied = values.clone();
        black_box(copied[511]);
    });
}
