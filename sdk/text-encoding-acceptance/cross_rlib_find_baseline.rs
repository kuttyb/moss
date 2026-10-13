//! Independent operation-only control for the release moss.text provider.
//!
//! This intentionally constructs all inputs before timing. It is not part of
//! the ordinary Margo benchmark target because it links the generated Rust
//! provider directly to isolate cross-rlib call/lowering cost.

use std::hint::black_box;
use std::time::Instant;

fn local_find(value: &String, pattern: &String) -> i64 {
    match value.find(pattern.as_str()) {
        Some(byte) => value[..byte].chars().count() as i64,
        None => -1,
    }
}

fn workload(
    call: fn(&String, &String) -> i64,
    value: &String,
    early: &String,
    late: &String,
    absent: &String,
) -> i64 {
    let mut checksum = 0_i64;
    for _ in 0..128 {
        checksum += black_box(call(black_box(value), black_box(early)));
        checksum += black_box(call(black_box(value), black_box(late)));
        checksum += black_box(call(black_box(value), black_box(absent)));
    }
    checksum
}

fn quartiles(mut samples: Vec<u128>) -> (u128, u128, u128) {
    samples.sort_unstable();
    (samples[7], samples[15], samples[23])
}

fn main() {
    let value = format!("{}界end", "é🙂".repeat(512));
    let early = String::from("é🙂");
    let late = String::from("界end");
    let absent = String::from("absent");
    assert_eq!(value.chars().count(), 1_028);
    assert_eq!(value.chars().nth(1_024), Some('界'));
    assert_eq!(
        workload(local_find, &value, &early, &late, &absent),
        130_944
    );
    assert_eq!(
        workload(moss_text::text__find, &value, &early, &late, &absent),
        130_944
    );

    for _ in 0..5 {
        black_box(workload(local_find, &value, &early, &late, &absent));
        black_box(workload(
            moss_text::text__find,
            &value,
            &early,
            &late,
            &absent,
        ));
    }

    let mut local = Vec::with_capacity(31);
    let mut provider = Vec::with_capacity(31);
    for sample in 0..31 {
        let provider_first = sample % 2 != 0;
        if provider_first {
            let started = Instant::now();
            black_box(workload(
                moss_text::text__find,
                &value,
                &early,
                &late,
                &absent,
            ));
            provider.push(started.elapsed().as_nanos());
        }
        let started = Instant::now();
        black_box(workload(local_find, &value, &early, &late, &absent));
        local.push(started.elapsed().as_nanos());
        if !provider_first {
            let started = Instant::now();
            black_box(workload(
                moss_text::text__find,
                &value,
                &early,
                &late,
                &absent,
            ));
            provider.push(started.elapsed().as_nanos());
        }
    }

    println!(
        "local_p25_median_p75={:?} provider_p25_median_p75={:?}",
        quartiles(local),
        quartiles(provider)
    );
}
