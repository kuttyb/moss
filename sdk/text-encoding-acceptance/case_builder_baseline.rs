use std::hint::black_box;
use std::time::Instant;

fn measure(label: &str, mut body: impl FnMut()) {
    for _ in 0..5 {
        for _ in 0..1_000 {
            body();
        }
    }
    let mut samples = Vec::with_capacity(31);
    for _ in 0..31 {
        let started = Instant::now();
        for _ in 0..1_000 {
            body();
        }
        samples.push(started.elapsed().as_nanos().div_ceil(1_000));
    }
    samples.sort_unstable();
    println!(
        "{label}: median={} ns p25={} ns p75={} ns",
        samples[15], samples[7], samples[23]
    );
}

fn make_multibyte_case_input() -> (String, String) {
    let mut value = String::new();
    let mut expected = String::new();
    for _ in 0..32 {
        value = value + "aé🙂zBCdE";
        expected = expected + "Aé🙂ZBCDE";
    }
    (value, expected)
}

fn make_long_case_input() -> (String, String) {
    let mut value = String::new();
    let mut expected = String::new();
    for _ in 0..512 {
        value = value + "AbCdEfGh";
        expected = expected + "abcdefgh";
    }
    (value, expected)
}

fn equivalent_segment_builder(count: usize) -> (Vec<String>, String) {
    let mut parts = Vec::<String>::new();
    for _ in 0..count {
        let part = String::from("abcdefgh");
        black_box(parts.push(black_box(part.clone())));
    }
    let built = parts.join("");
    (parts, built)
}

fn reserved_string_builder(count: usize) -> String {
    let mut result = String::with_capacity(count * 8);
    for _ in 0..count {
        black_box(result.push_str(black_box("abcdefgh")));
    }
    result
}

fn reusable_reserved_builder_build(builder: &String) -> String {
    builder.clone()
}

fn main() {
    measure("Rust ASCII lower short 16 code points", || {
        let value = "AbCdEfGhIjKlMnOp";
        let expected = "abcdefghijklmnop";
        assert_eq!(value.chars().count(), 16);
        assert_eq!(expected.chars().count(), 16);
        assert_eq!(black_box(value.to_ascii_lowercase()), expected);
    });

    measure("Rust ASCII upper multibyte 256 code points", || {
        let (value, expected) = make_multibyte_case_input();
        assert_eq!(value.chars().count(), 256);
        assert_eq!(expected.chars().count(), 256);
        assert_eq!(black_box(value.to_ascii_uppercase()), expected);
    });

    measure(
        "Rust profile ASCII lower prebuilt 4096 code points x16",
        || {
            let (value, expected) = make_long_case_input();
            let mut lowered = String::new();
            for _ in 0..16 {
                lowered = black_box(black_box(&value).to_ascii_lowercase());
            }
            assert_eq!(value.chars().count(), 4_096);
            assert_eq!(expected.chars().count(), 4_096);
            assert_eq!(lowered, expected);
        },
    );

    measure(
        "Rust isolated ASCII lower literal 64 code points x16",
        || {
            let value =
                String::from("AbCdEfGhAbCdEfGhAbCdEfGhAbCdEfGhAbCdEfGhAbCdEfGhAbCdEfGhAbCdEfGh");
            let expected =
                String::from("abcdefghabcdefghabcdefghabcdefghabcdefghabcdefghabcdefghabcdefgh");
            let mut lowered = String::new();
            for _ in 0..16 {
                lowered = black_box(black_box(&value).to_ascii_lowercase());
            }
            assert_eq!(value.chars().count(), 64);
            assert_eq!(expected.chars().count(), 64);
            assert_eq!(lowered, expected);
        },
    );

    for count in [8_usize, 128, 1_024] {
        measure(
            &format!("Rust equivalent segment builder {count} segments"),
            || {
                let (parts, built) = equivalent_segment_builder(count);
                assert_eq!(parts.len(), count);
                assert_eq!(built.chars().count(), count * 8);
                assert_eq!(built.as_bytes().first(), Some(&b'a'));
                assert_eq!(built.as_bytes().last(), Some(&b'h'));
                black_box(parts);
                black_box(built);
            },
        );
        measure(
            &format!("Rust reserved String builder {count} segments"),
            || {
                let built = reserved_string_builder(count);
                assert_eq!(built.chars().count(), count * 8);
                assert_eq!(built.as_bytes().first(), Some(&b'a'));
                assert_eq!(built.as_bytes().last(), Some(&b'h'));
                black_box(built);
            },
        );
        measure(
            &format!("Rust capacity builder finish {count} segments"),
            || {
                let part = String::from("abcdefgh");
                let mut builder = String::with_capacity(count * 8);
                for _ in 0..count {
                    black_box(builder.push_str(black_box(&part)));
                }
                let built = black_box(builder);
                assert_eq!(built.chars().count(), count * 8);
                assert_eq!(built.chars().nth(0), Some('a'));
                assert_eq!(built.chars().nth(count * 8 - 1), Some('h'));
            },
        );
    }

    measure(
        "Rust capacity builder append preconstructed input 128 segments",
        || {
            let part = String::from("abcdefgh");
            let mut builder = String::with_capacity(1_024);
            for _ in 0..128 {
                black_box(builder.push_str(black_box(&part)));
            }
            assert_eq!(black_box(&builder).chars().count(), 1_024);
        },
    );
    measure(
        "Rust capacity builder end to end reusable build 128 segments x16",
        || {
            let part = String::from("abcdefgh");
            let mut reusable_builder = String::with_capacity(1_024);
            for _ in 0..128 {
                black_box(reusable_builder.push_str(black_box(&part)));
            }
            let mut checksum = 0;
            for _ in 0..16 {
                let built = black_box(reusable_reserved_builder_build(
                    black_box(&reusable_builder),
                ));
                checksum += built.chars().count();
            }
            assert_eq!(checksum, 16_384);
        },
    );

    measure(
        "Rust profile equivalent builder build preconstructed 128 segments x16",
        || {
            let mut parts = Vec::<String>::new();
            for _ in 0..128 {
                let part = String::from("abcdefgh");
                black_box(parts.push(black_box(part.clone())));
            }
            let mut checksum = 0;
            for _ in 0..16 {
                checksum += black_box(parts.join("")).chars().count();
            }
            assert_eq!(parts.len(), 128);
            assert_eq!(checksum, 16_384);
        },
    );
}
