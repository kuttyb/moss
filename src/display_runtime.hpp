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
const char* display_runtime_rust();
