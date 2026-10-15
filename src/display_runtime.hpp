#pragma once

// Moss echo display runtime (Phase 25 S-B). Native `echo` of a Vector formats
// through these helpers so the text is Moss's display contract, identical to
// Fast Debug, and never Rust's `Debug` representation:
//   Int     decimal, with a leading '-' when negative
//   Float   canonical Moss Float text (__moss_ryu::canonical_float_text)
//   Bool    true | false
//   Char    the character itself (UTF-8), unquoted
//   String  the text itself, unquoted and unescaped
//   Vector  "[" + element texts joined by ", " + "]"; empty is "[]"
// The generator selects the element formatter from the checked Moss type, so
// there is no runtime trait or dynamic dispatch: a nested Vector is a nested
// call with a statically known element function. The items are private, so a
// consumer crate's glob import of a provider never sees a second copy.
inline const char* display_runtime_rust() {
  return R"RUST(
// ─── Begin Moss Echo Display Runtime ─────────────────────────────────────
#[allow(dead_code)]
fn __moss_display_int(out: &mut String, value: &i64) {
    out.push_str(&value.to_string());
}

#[allow(dead_code)]
fn __moss_display_float(out: &mut String, value: &f64) {
    out.push_str(&__moss_ryu::canonical_float_text(*value));
}

#[allow(dead_code)]
fn __moss_display_bool(out: &mut String, value: &bool) {
    out.push_str(if *value { "true" } else { "false" });
}

#[allow(dead_code)]
fn __moss_display_char(out: &mut String, value: &char) {
    out.push(*value);
}

#[allow(dead_code)]
fn __moss_display_string(out: &mut String, value: &MossStr) {
    use std::fmt::Write as _;
    let _ = write!(out, "{}", value);
}

#[allow(dead_code)]
fn __moss_display_vector_into<T, F: Fn(&mut String, &T)>(out: &mut String, items: &[T], each: &F) {
    out.push('[');
    for (index, element) in items.iter().enumerate() {
        if index > 0 {
            out.push_str(", ");
        }
        each(out, element);
    }
    out.push(']');
}

#[allow(dead_code)]
fn __moss_display_vector<T, F: Fn(&mut String, &T)>(items: &[T], each: &F) -> String {
    let mut out = String::new();
    __moss_display_vector_into(&mut out, items, each);
    out
}
// ─── End Moss Echo Display Runtime ───────────────────────────────────────
)RUST";
}
