#pragma once

#include <string>

// Moss String representation selected for one build.
//   plain       — MossStr = Rust String (native ABI v9 representation).
//   indexed-v1  — MossStr = MossString: UTF-8 bytes plus an eager code-point
//                 count and a byte offset every 64 code points, so length()
//                 is O(1) and char_at() decodes at most 63 scalars. This is
//                 the Team S String record staged for the joint ABI-v10 cut;
//                 artifacts carry it in their .mossi header and backend
//                 fingerprint, and the two representations never link.
// Generated code calls only the helpers below, so call sites are identical
// for both representations.

const char* char_ordering_runtime_rust();
const char* plain_string_runtime_rust();
const char* indexed_string_runtime_rust();
std::string char_string_ordering_runtime_rust(bool indexed_strings);
