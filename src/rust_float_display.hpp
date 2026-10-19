#pragma once

// Rust f64 Display shortest-digit selection for Fast Debug.
// The Grisu3 arithmetic and cached powers follow rust-lang/rust's
// library/core/src/num/imp/flt2dec/strategy/grisu.rs and diy_float.rs
// (MIT OR Apache-2.0). This uses no Rust runtime or foreign-call ABI.

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace moss_float_display_detail {

struct CachedPower { uint64_t f; int e; int k; };
inline constexpr CachedPower cached_powers[] = {
    {0xe61acf033d1a45dfULL, -1087, -308},
    {0xab70fe17c79ac6caULL, -1060, -300},
    {0xff77b1fcbebcdc4fULL, -1034, -292},
    {0xbe5691ef416bd60cULL, -1007, -284},
    {0x8dd01fad907ffc3cULL, -980, -276},
    {0xd3515c2831559a83ULL, -954, -268},
    {0x9d71ac8fada6c9b5ULL, -927, -260},
    {0xea9c227723ee8bcbULL, -901, -252},
    {0xaecc49914078536dULL, -874, -244},
    {0x823c12795db6ce57ULL, -847, -236},
    {0xc21094364dfb5637ULL, -821, -228},
    {0x9096ea6f3848984fULL, -794, -220},
    {0xd77485cb25823ac7ULL, -768, -212},
    {0xa086cfcd97bf97f4ULL, -741, -204},
    {0xef340a98172aace5ULL, -715, -196},
    {0xb23867fb2a35b28eULL, -688, -188},
    {0x84c8d4dfd2c63f3bULL, -661, -180},
    {0xc5dd44271ad3cdbaULL, -635, -172},
    {0x936b9fcebb25c996ULL, -608, -164},
    {0xdbac6c247d62a584ULL, -582, -156},
    {0xa3ab66580d5fdaf6ULL, -555, -148},
    {0xf3e2f893dec3f126ULL, -529, -140},
    {0xb5b5ada8aaff80b8ULL, -502, -132},
    {0x87625f056c7c4a8bULL, -475, -124},
    {0xc9bcff6034c13053ULL, -449, -116},
    {0x964e858c91ba2655ULL, -422, -108},
    {0xdff9772470297ebdULL, -396, -100},
    {0xa6dfbd9fb8e5b88fULL, -369, -92},
    {0xf8a95fcf88747d94ULL, -343, -84},
    {0xb94470938fa89bcfULL, -316, -76},
    {0x8a08f0f8bf0f156bULL, -289, -68},
    {0xcdb02555653131b6ULL, -263, -60},
    {0x993fe2c6d07b7facULL, -236, -52},
    {0xe45c10c42a2b3b06ULL, -210, -44},
    {0xaa242499697392d3ULL, -183, -36},
    {0xfd87b5f28300ca0eULL, -157, -28},
    {0xbce5086492111aebULL, -130, -20},
    {0x8cbccc096f5088ccULL, -103, -12},
    {0xd1b71758e219652cULL, -77, -4},
    {0x9c40000000000000ULL, -50, 4},
    {0xe8d4a51000000000ULL, -24, 12},
    {0xad78ebc5ac620000ULL, 3, 20},
    {0x813f3978f8940984ULL, 30, 28},
    {0xc097ce7bc90715b3ULL, 56, 36},
    {0x8f7e32ce7bea5c70ULL, 83, 44},
    {0xd5d238a4abe98068ULL, 109, 52},
    {0x9f4f2726179a2245ULL, 136, 60},
    {0xed63a231d4c4fb27ULL, 162, 68},
    {0xb0de65388cc8ada8ULL, 189, 76},
    {0x83c7088e1aab65dbULL, 216, 84},
    {0xc45d1df942711d9aULL, 242, 92},
    {0x924d692ca61be758ULL, 269, 100},
    {0xda01ee641a708deaULL, 295, 108},
    {0xa26da3999aef774aULL, 322, 116},
    {0xf209787bb47d6b85ULL, 348, 124},
    {0xb454e4a179dd1877ULL, 375, 132},
    {0x865b86925b9bc5c2ULL, 402, 140},
    {0xc83553c5c8965d3dULL, 428, 148},
    {0x952ab45cfa97a0b3ULL, 455, 156},
    {0xde469fbd99a05fe3ULL, 481, 164},
    {0xa59bc234db398c25ULL, 508, 172},
    {0xf6c69a72a3989f5cULL, 534, 180},
    {0xb7dcbf5354e9beceULL, 561, 188},
    {0x88fcf317f22241e2ULL, 588, 196},
    {0xcc20ce9bd35c78a5ULL, 614, 204},
    {0x98165af37b2153dfULL, 641, 212},
    {0xe2a0b5dc971f303aULL, 667, 220},
    {0xa8d9d1535ce3b396ULL, 694, 228},
    {0xfb9b7cd9a4a7443cULL, 720, 236},
    {0xbb764c4ca7a44410ULL, 747, 244},
    {0x8bab8eefb6409c1aULL, 774, 252},
    {0xd01fef10a657842cULL, 800, 260},
    {0x9b10a4e5e9913129ULL, 827, 268},
    {0xe7109bfba19c0c9dULL, 853, 276},
    {0xac2820d9623bf429ULL, 880, 284},
    {0x80444b5e7aa7cf85ULL, 907, 292},
    {0xbf21e44003acdd2dULL, 933, 300},
    {0x8e679c2f5e44ff8fULL, 960, 308},
    {0xd433179d9c8cb841ULL, 986, 316},
    {0x9e19db92b4e31ba9ULL, 1013, 324},
    {0xeb96bf6ebadf77d9ULL, 1039, 332},
};

struct Fp {
  uint64_t f;
  int e;

  Fp normalized() const {
    int shift = __builtin_clzll(f);
    return {f << shift, e - shift};
  }

  Fp normalized_to(int target) const {
    return {f << (e - target), target};
  }

  Fp multiply(Fp other) const {
    uint64_t a0 = static_cast<uint32_t>(f), a1 = f >> 32;
    uint64_t b0 = static_cast<uint32_t>(other.f), b1 = other.f >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1;
    uint64_t p10 = a1 * b0, p11 = a1 * b1;
    uint64_t middle = (p00 >> 32) + static_cast<uint32_t>(p01) +
                      static_cast<uint32_t>(p10);
    uint64_t high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
    uint64_t low = (middle << 32) | static_cast<uint32_t>(p00);
    return {high + (low >> 63), e + other.e + 64};
  }
};

struct Decoded {
  uint64_t mant;
  uint64_t minus;
  uint64_t plus;
  int exp;
  bool inclusive;
};

inline Decoded decode_positive_finite(double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  uint64_t fraction = bits & ((uint64_t{1} << 52) - 1);
  int exponent = static_cast<int>((bits >> 52) & 0x7ff);
  if (exponent == 0)
    return {fraction << 1, 1, 1, -1075, true};
  uint64_t mant = (uint64_t{1} << 52) | fraction;
  int exp = exponent - 1023 - 52;
  if (fraction == 0)
    return {mant << 2, 1, 2, exp - 2, true};
  return {mant << 1, 1, 1, exp - 1, (mant & 1) == 0};
}

inline std::pair<int, Fp> cached_power(int gamma) {
  constexpr int first_e = -1087;
  constexpr int last_e = 1039;
  constexpr int count = sizeof(cached_powers) / sizeof(cached_powers[0]);
  int index = (gamma - first_e) * (count - 1) / (last_e - first_e);
  const auto& power = cached_powers[index];
  return {power.k, {power.f, power.e}};
}

inline std::pair<int, uint32_t> max_pow10_no_more_than(uint32_t x) {
  int k = 0;
  uint32_t power = 1;
  while (power <= x / 10) {
    power *= 10;
    ++k;
  }
  return {k, power};
}

inline std::optional<std::pair<std::string, int>> round_and_weed(
    std::string digits, int exp, uint64_t remainder, uint64_t threshold,
    uint64_t plus1v, uint64_t ten_kappa, uint64_t ulp) {
  if (digits.empty() || plus1v < ulp) return std::nullopt;
  uint64_t plus1v_down = plus1v + ulp;
  uint64_t plus1v_up = plus1v - ulp;
  uint64_t plus1w = remainder;
  while (plus1w < plus1v_up && threshold >= plus1w &&
         threshold - plus1w >= ten_kappa &&
         (plus1w + ten_kappa < plus1v_up ||
          plus1v_up - plus1w >= plus1w + ten_kappa - plus1v_up)) {
    if (digits.back() <= '0') return std::nullopt;
    --digits.back();
    plus1w += ten_kappa;
  }
  if (plus1w < plus1v_down && threshold >= plus1w &&
      threshold - plus1w >= ten_kappa &&
      (plus1w + ten_kappa < plus1v_down ||
       plus1v_down - plus1w >= plus1w + ten_kappa - plus1v_down))
    return std::nullopt;
  if (ulp > UINT64_MAX / 4 || threshold < 4 * ulp) return std::nullopt;
  if (2 * ulp <= plus1w && plus1w <= threshold - 4 * ulp)
    return std::pair{std::move(digits), exp};
  return std::nullopt;
}

// Returns the same decimal digits and decimal-point position as Rust's
// Grisu3 fast path. A missing result means that Rust uses its Dragon fallback.
inline std::optional<std::pair<std::string, int>> grisu_shortest(double positive) {
  Decoded d = decode_positive_finite(positive);
  Fp plus = Fp{d.mant + d.plus, d.exp}.normalized();
  Fp minus = Fp{d.mant - d.minus, d.exp}.normalized_to(plus.e);
  Fp value = Fp{d.mant, d.exp}.normalized_to(plus.e);
  auto [minusk, cached] = cached_power(-32 - plus.e - 64);
  plus = plus.multiply(cached);
  minus = minus.multiply(cached);
  value = value.multiply(cached);
  uint64_t plus1 = plus.f + 1;
  uint64_t minus1 = minus.f - 1;
  int e = -plus.e;
  uint64_t mask = (uint64_t{1} << e) - 1;
  uint32_t plus1int = static_cast<uint32_t>(plus1 >> e);
  uint64_t plus1frac = plus1 & mask;
  auto [max_kappa, max_ten_kappa] = max_pow10_no_more_than(plus1int);
  int exp = max_kappa - minusk + 1;
  uint64_t delta1 = plus1 - minus1;
  uint64_t delta1frac = delta1 & mask;

  std::string digits;
  uint32_t ten_kappa = max_ten_kappa;
  uint32_t remainder = plus1int;
  while (true) {
    uint32_t q = remainder / ten_kappa;
    uint32_t r = remainder % ten_kappa;
    if (q >= 10 || digits.size() >= 17) return std::nullopt;
    digits.push_back(static_cast<char>('0' + q));
    uint64_t plus1rem = (static_cast<uint64_t>(r) << e) + plus1frac;
    if (plus1rem < delta1)
      return round_and_weed(std::move(digits), exp, plus1rem, delta1,
                            plus1 - value.f, static_cast<uint64_t>(ten_kappa) << e, 1);
    if (digits.size() > static_cast<size_t>(max_kappa)) break;
    ten_kappa /= 10;
    remainder = r;
  }

  uint64_t fractional_remainder = plus1frac;
  uint64_t threshold = delta1frac;
  uint64_t ulp = 1;
  while (digits.size() < 17) {
    fractional_remainder *= 10;
    threshold *= 10;
    ulp *= 10;
    uint64_t q = fractional_remainder >> e;
    uint64_t r = fractional_remainder & mask;
    if (q >= 10) return std::nullopt;
    digits.push_back(static_cast<char>('0' + q));
    if (r < threshold)
      return round_and_weed(std::move(digits), exp, r, threshold,
                            (plus1 - value.f) * ulp, uint64_t{1} << e, ulp);
    fractional_remainder = r;
  }
  return std::nullopt;
}

// The rare values rejected by Grisu use Rust's Dragon shortest path. The
// bounded base-2^32 integer below is sufficient for every binary64 exponent;
// simpler multiplication keeps this path independent of external libraries.
class Big {
 public:
  explicit Big(uint64_t value = 0) {
    limbs_.push_back(static_cast<uint32_t>(value));
    if (value >> 32) limbs_.push_back(static_cast<uint32_t>(value >> 32));
  }

  int compare(const Big& other) const {
    if (limbs_.size() != other.limbs_.size())
      return limbs_.size() < other.limbs_.size() ? -1 : 1;
    for (size_t i = limbs_.size(); i-- > 0;)
      if (limbs_[i] != other.limbs_[i])
        return limbs_[i] < other.limbs_[i] ? -1 : 1;
    return 0;
  }

  void multiply(uint32_t multiplier) {
    uint64_t carry = 0;
    for (uint32_t& limb : limbs_) {
      uint64_t product = static_cast<uint64_t>(limb) * multiplier + carry;
      limb = static_cast<uint32_t>(product);
      carry = product >> 32;
    }
    if (carry) limbs_.push_back(static_cast<uint32_t>(carry));
    normalize();
  }

  void multiply_pow2(int exponent) {
    for (int i = 0; i < exponent; ++i) multiply(2);
  }

  void multiply_pow10(int exponent) {
    for (int i = 0; i < exponent; ++i) multiply(10);
  }

  void add(const Big& other) {
    if (limbs_.size() < other.limbs_.size()) limbs_.resize(other.limbs_.size());
    uint64_t carry = 0;
    for (size_t i = 0; i < limbs_.size(); ++i) {
      uint64_t sum = static_cast<uint64_t>(limbs_[i]) + carry +
                     (i < other.limbs_.size() ? other.limbs_[i] : 0);
      limbs_[i] = static_cast<uint32_t>(sum);
      carry = sum >> 32;
    }
    if (carry) limbs_.push_back(static_cast<uint32_t>(carry));
  }

  void subtract(const Big& other) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < limbs_.size(); ++i) {
      uint64_t amount = borrow +
                        (i < other.limbs_.size() ? other.limbs_[i] : 0);
      uint64_t value = limbs_[i];
      limbs_[i] = static_cast<uint32_t>(value - amount);
      borrow = value < amount;
    }
    normalize();
  }

 private:
  void normalize() {
    while (limbs_.size() > 1 && limbs_.back() == 0) limbs_.pop_back();
  }
  std::vector<uint32_t> limbs_;
};

inline int estimate_scaling_factor(uint64_t mant, int exp) {
  int nbits = 64 - __builtin_clzll(mant - 1);
  int64_t scaled = static_cast<int64_t>(nbits + exp) * 1292913986LL;
  if (scaled >= 0) return static_cast<int>(scaled >> 32);
  return -static_cast<int>((-scaled + ((int64_t{1} << 32) - 1)) >> 32);
}

inline uint8_t div_rem_upto_16(Big& value, const Big& scale,
                               const Big& scale2, const Big& scale4,
                               const Big& scale8) {
  uint8_t digit = 0;
  if (value.compare(scale8) >= 0) { value.subtract(scale8); digit += 8; }
  if (value.compare(scale4) >= 0) { value.subtract(scale4); digit += 4; }
  if (value.compare(scale2) >= 0) { value.subtract(scale2); digit += 2; }
  if (value.compare(scale) >= 0) { value.subtract(scale); digit += 1; }
  return digit;
}

inline std::pair<std::string, int> dragon_shortest(double positive) {
  Decoded d = decode_positive_finite(positive);
  int k = estimate_scaling_factor(d.mant + d.plus, d.exp);
  Big mant(d.mant), minus(d.minus), plus(d.plus), scale(1);
  if (d.exp < 0) {
    scale.multiply_pow2(-d.exp);
  } else {
    mant.multiply_pow2(d.exp);
    minus.multiply_pow2(d.exp);
    plus.multiply_pow2(d.exp);
  }
  if (k >= 0) {
    scale.multiply_pow10(k);
  } else {
    mant.multiply_pow10(-k);
    minus.multiply_pow10(-k);
    plus.multiply_pow10(-k);
  }

  auto within_bound = [inclusive = d.inclusive](const Big& left,
                                                const Big& right) {
    int comparison = left.compare(right);
    return inclusive ? comparison <= 0 : comparison < 0;
  };
  Big high = mant;
  high.add(plus);
  if (within_bound(scale, high)) {
    ++k;
  } else {
    mant.multiply(10);
    minus.multiply(10);
    plus.multiply(10);
  }

  Big scale2 = scale, scale4 = scale, scale8 = scale;
  scale2.multiply(2);
  scale4.multiply(4);
  scale8.multiply(8);
  std::string digits;
  bool down = false, up = false;
  while (digits.size() < 18) {
    uint8_t digit = div_rem_upto_16(mant, scale, scale2, scale4, scale8);
    digits.push_back(static_cast<char>('0' + digit));
    down = within_bound(mant, minus);
    high = mant;
    high.add(plus);
    up = within_bound(scale, high);
    if (down || up) break;
    mant.multiply(10);
    minus.multiply(10);
    plus.multiply(10);
  }
  if (up) {
    Big twice = mant;
    twice.multiply(2);
    if (!down || twice.compare(scale) >= 0) {
      size_t i = digits.size();
      while (i > 0 && digits[i - 1] == '9') digits[--i] = '0';
      if (i > 0) {
        ++digits[i - 1];
      } else {
        digits.insert(digits.begin(), '1');
        ++k;
      }
    }
  }
  return {std::move(digits), k};
}

inline std::string fixed_decimal(const std::string& digits, int decimal_at) {
  if (decimal_at <= 0)
    return "0." + std::string(static_cast<size_t>(-decimal_at), '0') + digits;
  if (static_cast<size_t>(decimal_at) >= digits.size())
    return digits + std::string(static_cast<size_t>(decimal_at) - digits.size(), '0');
  return digits.substr(0, static_cast<size_t>(decimal_at)) + "." +
         digits.substr(static_cast<size_t>(decimal_at));
}

}  // namespace moss_float_display_detail
