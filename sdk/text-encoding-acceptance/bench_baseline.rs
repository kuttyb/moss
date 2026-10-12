use std::hint::black_box;
use std::time::Instant;

fn text_find(value: &str, pattern: &str) -> i64 {
    if pattern.is_empty() {
        return 0;
    }
    let parts: Vec<String> = value.split(pattern).map(str::to_owned).collect();
    if parts.len() == 1 {
        return -1;
    }
    parts[0].chars().count() as i64
}

fn text_slice(value: &str, start: usize, length: usize) -> String {
    let chars: Vec<String> = value
        .chars()
        .map(|character| character.to_string())
        .collect();
    chars[start..start + length].concat()
}

fn ascii_lower(value: &str) -> String {
    let chars: Vec<String> = value
        .chars()
        .map(|character| character.to_string())
        .collect();
    let mut parts = Vec::<String>::new();
    for character in chars {
        let index = text_find("ABCDEFGHIJKLMNOPQRSTUVWXYZ", &character);
        if index >= 0 {
            parts.push(
                "abcdefghijklmnopqrstuvwxyz"
                    .chars()
                    .nth(index as usize)
                    .unwrap()
                    .to_string(),
            );
        } else {
            parts.push(character);
        }
    }
    parts.join("")
}

fn scalar_text_find(value: &str, pattern: &str) -> i64 {
    if pattern.is_empty() {
        return 0;
    }
    match value.find(pattern) {
        Some(byte_index) => value[..byte_index].chars().count() as i64,
        None => -1,
    }
}

fn scalar_text_slice(value: &str, start: usize, length: usize) -> String {
    value.chars().skip(start).take(length).collect()
}

fn scalar_ascii_lower(value: &str) -> String {
    value
        .chars()
        .map(|character| character.to_ascii_lowercase())
        .collect()
}

fn validate_utf8_ints(values: &[i64]) -> bool {
    let mut i = 0;
    while i < values.len() {
        let first = values[i];
        if !(0..=255).contains(&first) {
            return false;
        }
        if first <= 127 {
            i += 1;
            continue;
        }
        if (128..=191).contains(&first) {
            return false;
        }
        let (width, minimum) = if (192..=223).contains(&first) {
            (2, 128)
        } else if (224..=239).contains(&first) {
            (3, 2_048)
        } else if (240..=247).contains(&first) {
            (4, 65_536)
        } else {
            return false;
        };
        if values.len() - i < width {
            return false;
        }
        let second = values[i + 1];
        if !(128..=191).contains(&second) {
            return false;
        }
        let scalar = if width == 2 {
            (first - 192) * 64 + (second - 128)
        } else {
            let third = values[i + 2];
            if !(128..=191).contains(&third) {
                return false;
            }
            if width == 3 {
                (first - 224) * 4_096 + (second - 128) * 64 + (third - 128)
            } else {
                let fourth = values[i + 3];
                if !(128..=191).contains(&fourth) {
                    return false;
                }
                (first - 240) * 262_144
                    + (second - 128) * 4_096
                    + (third - 128) * 64
                    + (fourth - 128)
            }
        };
        if scalar < minimum || (55_296..=57_343).contains(&scalar) || scalar > 1_114_111 {
            return false;
        }
        i += width;
    }
    true
}

fn make_find_input() -> String {
    let mut value = String::new();
    for _ in 0..255 {
        value = value + "a";
    }
    value + "b"
}

fn make_slice_input() -> String {
    let mut value = String::new();
    for _ in 0..128 {
        value = value + "é🙂";
    }
    value
}

fn make_case_input() -> (String, String) {
    let mut value = String::new();
    let mut expected = String::new();
    for _ in 0..32 {
        value = value + "AbCdEfGh";
        expected = expected + "abcdefgh";
    }
    (value, expected)
}

fn make_ascii_bytes() -> Vec<i64> {
    let mut values = Vec::new();
    for i in 0..1_024 {
        black_box(values.push(black_box(65 + i % 26)));
    }
    values
}

fn repeated_concat(count: usize) -> String {
    let mut result = String::new();
    for _ in 0..count {
        result = result + "abcdefgh";
    }
    result
}

fn builder_append(parts: &mut Vec<String>, segment: &String) {
    parts.push(segment.clone());
}

fn segment_builder(count: usize) -> (Vec<String>, String) {
    let mut parts = Vec::<String>::new();
    for _ in 0..count {
        let segment = "abcdefgh".to_owned();
        black_box(builder_append(black_box(&mut parts), black_box(&segment)));
    }
    let built = parts.join("");
    (parts, built)
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
    println!(
        "{label}: median={} ns p25={} ns p75={} ns",
        samples[15], samples[7], samples[23]
    );
}

fn main() {
    measure("Rust text find 256 code points", || {
        let value = make_find_input();
        assert_eq!(value.chars().count(), 256);
        let pattern = "aaaaab";
        assert_eq!(pattern.chars().count(), 6);
        assert_eq!(
            black_box(text_find(black_box(&value), black_box(pattern))),
            250
        );
    });
    measure("Rust text slice 256 code points", || {
        let value = make_slice_input();
        assert_eq!(value.chars().count(), 256);
        let sliced = black_box(text_slice(black_box(&value), 64, 128));
        assert_eq!(sliced.chars().count(), 128);
        assert_eq!(sliced.chars().next(), Some('é'));
        assert_eq!(sliced.chars().last(), Some('🙂'));
    });
    measure("Rust ASCII lower 256 code points", || {
        let (value, expected) = make_case_input();
        assert_eq!(value.chars().count(), 256);
        assert_eq!(expected.chars().count(), 256);
        assert_eq!(black_box(ascii_lower(black_box(&value))), expected);
    });
    measure("Rust scalar text find 256 code points", || {
        let value = make_find_input();
        assert_eq!(value.chars().count(), 256);
        assert_eq!(
            black_box(scalar_text_find(black_box(&value), "aaaaab")),
            250
        );
    });
    measure("Rust scalar text slice 256 code points", || {
        let value = make_slice_input();
        assert_eq!(value.chars().count(), 256);
        let sliced = black_box(scalar_text_slice(black_box(&value), 64, 128));
        assert_eq!(sliced.chars().count(), 128);
    });
    measure("Rust scalar ASCII lower 256 code points", || {
        let (value, expected) = make_case_input();
        assert_eq!(value.chars().count(), 256);
        assert_eq!(black_box(scalar_ascii_lower(black_box(&value))), expected);
    });
    measure("Rust repeated concat 128 segments", || {
        let result = repeated_concat(128);
        let result_length = result.chars().count();
        assert_eq!(result_length, 1_024);
        black_box(&result);
    });
    measure("Rust segment builder 128 segments", || {
        let (parts, built) = segment_builder(128);
        assert_eq!(parts.len(), 128);
        let built_length = built.chars().count();
        assert_eq!(built_length, 1_024);
        black_box(&parts);
        black_box(&built);
    });
    measure("Rust same UTF-8 validation 1024 ASCII bytes", || {
        let values = make_ascii_bytes();
        assert_eq!(values.len(), 1_024);
        assert_eq!(values[0], 65);
        assert_eq!(values[1_023], 74);
        assert!(black_box(validate_utf8_ints(black_box(&values))));
    });
    measure("Rust std UTF-8 validation 1024 ASCII bytes", || {
        let values: Vec<u8> = make_ascii_bytes()
            .into_iter()
            .map(|value| value as u8)
            .collect();
        assert_eq!(values.len(), 1_024);
        assert_eq!(values[0], 65);
        assert_eq!(values[1_023], 74);
        assert!(black_box(std::str::from_utf8(black_box(&values)).is_ok()));
    });

    measure("Rust profile text find prebuilt 256 code points x8", || {
        let value = make_find_input();
        let mut checksum = 0_i64;
        for _ in 0..8 {
            checksum += black_box(text_find(black_box(&value), black_box("aaaaab")));
        }
        assert_eq!(value.chars().count(), 256);
        assert_eq!(checksum, 2_000);
    });
    measure(
        "Rust profile text slice prebuilt 256 code points x16",
        || {
            let value = make_slice_input();
            let mut total = 0;
            for _ in 0..16 {
                total += black_box(text_slice(black_box(&value), 64, 128))
                    .chars()
                    .count();
            }
            assert_eq!(value.chars().count(), 256);
            assert_eq!(total, 2_048);
        },
    );
    measure(
        "Rust profile ASCII lower prebuilt 256 code points x4",
        || {
            let (value, expected) = make_case_input();
            let mut lowered = String::new();
            for _ in 0..4 {
                lowered = black_box(ascii_lower(black_box(&value)));
            }
            assert_eq!(value.chars().count(), 256);
            assert_eq!(lowered, expected);
        },
    );
    measure(
        "Rust profile same UTF-8 validate prebuilt 1024 ASCII bytes x64",
        || {
            let values = make_ascii_bytes();
            let mut valid_count = 0;
            for _ in 0..64 {
                if black_box(validate_utf8_ints(black_box(&values))) {
                    valid_count += 1;
                }
            }
            assert_eq!(values.len(), 1_024);
            assert_eq!(valid_count, 64);
        },
    );
}
