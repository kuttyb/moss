use std::hint::black_box;
use std::time::Instant;

fn text_find(value: &str, pattern: &str) -> i64 {
    let value: Vec<char> = value.chars().collect();
    let pattern: Vec<char> = pattern.chars().collect();
    if pattern.is_empty() {
        return 0;
    }
    if pattern.len() > value.len() {
        return -1;
    }
    for start in 0..=value.len() - pattern.len() {
        if value[start..start + pattern.len()] == pattern[..] {
            return start as i64;
        }
    }
    -1
}

fn text_slice(value: &str, start: usize, length: usize) -> String {
    let chars: Vec<char> = value.chars().collect();
    chars[start..start + length].iter().collect()
}

fn repeated_concat(count: usize) -> usize {
    let mut result = String::new();
    for _ in 0..count {
        result = result + "abcdefgh";
    }
    result.chars().count()
}

fn segment_builder(count: usize) -> usize {
    let mut parts = Vec::<String>::new();
    for _ in 0..count {
        parts.push("abcdefgh".to_owned());
    }
    parts.join("").chars().count()
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
        samples.push(start.elapsed().as_nanos().div_ceil(1_000));
    }
    samples.sort_unstable();
    println!("{label}: median={} ns p25={} ns p75={} ns", samples[15], samples[7], samples[23]);
}

fn main() {
    let search = "a".repeat(255) + "b";
    let slice = "é🙂".repeat(128);
    measure("Rust equivalent text find 256 code points", || {
        black_box(text_find(black_box(&search), black_box("aaaaab")));
    });
    measure("Rust equivalent text slice 256 code points", || {
        black_box(text_slice(black_box(&slice), 64, 128));
    });
    measure("Rust repeated concat 128 segments", || {
        black_box(repeated_concat(128));
    });
    measure("Rust segment builder 128 segments", || {
        black_box(segment_builder(128));
    });
    measure("Rust std UTF-8 validate 1024 ASCII bytes", || {
        let mut values = Vec::<u8>::new();
        for i in 0..1_024 {
            values.push(65 + (i % 26) as u8);
        }
        black_box(std::str::from_utf8(black_box(&values)).is_ok());
    });
}
