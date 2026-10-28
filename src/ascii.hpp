#pragma once

namespace moss {

// Moss source syntax is ASCII-defined; do not let the process C locale alter
// token classification or whitespace trimming.
inline bool ascii_is_space(int value) {
  switch (value) {
    case ' ': case '\t': case '\n': case '\r': case '\f': case '\v':
      return true;
    default:
      return false;
  }
}

inline bool ascii_is_alpha(int value) {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

inline bool ascii_is_upper(int value) {
  return value >= 'A' && value <= 'Z';
}

inline bool ascii_is_digit(int value) {
  return value >= '0' && value <= '9';
}

inline bool ascii_is_alnum(int value) {
  return ascii_is_alpha(value) || ascii_is_digit(value);
}

inline bool ascii_is_hex_digit(int value) {
  return ascii_is_digit(value) || (value >= 'A' && value <= 'F') ||
      (value >= 'a' && value <= 'f');
}

inline int ascii_to_lower(int value) {
  return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

}  // namespace moss
