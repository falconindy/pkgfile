/*
Copyright (c) 2018-2021 Felix Gündling

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once


#include <chrono>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <vector>


#include <memory>


namespace cista {

template <typename TemplateSizeType>
constexpr TemplateSizeType next_power_of_two(TemplateSizeType n) noexcept {
  --n;
  n |= n >> 1U;
  n |= n >> 2U;
  n |= n >> 4U;
  n |= n >> 8U;
  n |= n >> 16U;
  if constexpr (sizeof(TemplateSizeType) > 32U) {
    n |= n >> 32U;
  }
  ++n;
  return n;
}

}  // namespace cista

namespace cista {

template <typename T1, typename T2>
T1 to_next_multiple(T1 const n, T2 const multiple) noexcept {
  auto const r = n % multiple;
  return r == 0 ? n : n + multiple - r;
}

}  // namespace cista

#if CISTA_USE_MIMALLOC

// mimalloc.h: basic_ios::clear: iostream error
#define CISTA_ALIGNED_ALLOC(alignment, size)                                  \
  (mi_malloc_aligned(                                                         \
      cista::to_next_multiple((size), cista::next_power_of_two((alignment))), \
      cista::next_power_of_two((alignment))))
#define CISTA_ALIGNED_FREE(alignment, ptr) \
  (mi_free_aligned((ptr), cista::next_power_of_two((alignment))))

#elif defined(_MSC_VER)

#define CISTA_ALIGNED_ALLOC(alignment, size) \
  (_aligned_malloc((size), cista::next_power_of_two((alignment))))
#define CISTA_ALIGNED_FREE(alignment, ptr) (_aligned_free((ptr)))

#elif defined(_LIBCPP_HAS_C11_FEATURES) || defined(_GLIBCXX_HAVE_ALIGNED_ALLOC)

#include <memory>
#define CISTA_ALIGNED_ALLOC(alignment, size) \
  (std::aligned_alloc(                       \
      cista::next_power_of_two((alignment)), \
      cista::to_next_multiple((size), cista::next_power_of_two((alignment)))))
#define CISTA_ALIGNED_FREE(alignment, ptr) std::free((ptr))

#else

#include <cstdlib>
#define CISTA_ALIGNED_ALLOC(alignment, size) (std::malloc((size)))
#define CISTA_ALIGNED_FREE(alignment, ptr) (std::free((ptr)))

#endif


#include <array>

namespace cista {

template <typename T, std::size_t Size>
using array = std::array<T, Size>;

namespace raw {
using cista::array;
}  // namespace raw

namespace offset {
using cista::array;
}  // namespace offset

}  // namespace cista

#include <cassert>
#include <cinttypes>
#include <iosfwd>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>


#if defined(_MSC_VER)
#include <intrin.h>
#if defined(_M_X64)
#pragma intrinsic(_BitScanReverse64)
#pragma intrinsic(_BitScanForward64)
#endif
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)
#endif

#include <cinttypes>
#include <cstddef>

namespace cista {

template <typename T>
inline constexpr unsigned constexpr_trailing_zeros(T t) {
  auto const is_bit_set = [&](unsigned const i) {
    return ((t >> i) & T{1U}) == T{1U};
  };
  for (auto i = 0U; i != sizeof(T) * 8U; ++i) {
    if (is_bit_set(i)) {
      return i;
    }
  }
  return 0U;
}

template <typename T>
constexpr unsigned trailing_zeros(T t) noexcept {
  static_assert(sizeof(T) == 8U || sizeof(T) == 4U, "not supported");

  if (t == 0U) {
    return sizeof(T) == 8U ? 64U : 32U;
  }

  if constexpr (sizeof(T) == 8U) {  // 64bit
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned long index = 0U;
    _BitScanForward64(&index, t);
    return index;
#elif defined(_MSC_VER)
    unsigned long index = 0U;
    if (static_cast<std::uint32_t>(t) == 0) {
      _BitScanForward(&index, t >> 32U);
      return index + 32U;
    }
    _BitScanForward(&index, static_cast<std::uint32_t>(t));
    return index;
#else
    return static_cast<unsigned>(__builtin_ctzll(t));
#endif
  } else if constexpr (sizeof(T) == 4U) {  // 32bit
#if defined(_MSC_VER)
    unsigned long index = 0U;
    _BitScanForward(&index, t);
    return index;
#else
    return static_cast<unsigned>(__builtin_ctz(t));
#endif
  }
}

template <typename T>
constexpr unsigned leading_zeros(T t) noexcept {
  static_assert(sizeof(T) == 8U || sizeof(T) == 4U, "not supported");

  if (t == 0U) {
    return sizeof(T) == 8U ? 64U : 32U;
  }

  if constexpr (sizeof(T) == 8U) {  // 64bit
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned long index = 0U;
    if (_BitScanReverse64(&index, t)) {
      return 63U - index;
    }
    return 64U;
#elif defined(_MSC_VER)
    unsigned long index = 0U;
    if ((t >> 32U) && _BitScanReverse(&index, t >> 32U)) {
      return 31U - index;
    }
    if (_BitScanReverse(&index, static_cast<std::uint32_t>(t))) {
      return 63U - index;
    }
    return 64U;
#else
    return static_cast<unsigned>(__builtin_clzll(t));
#endif
  } else if constexpr (sizeof(T) == 4U) {  // 32bit
#if defined(_MSC_VER)
    unsigned long index = 0;
    if (_BitScanReverse(&index, t)) {
      return 31U - index;
    }
    return 32U;
#else
    return static_cast<unsigned>(__builtin_clz(t));
#endif
  }
}

inline std::size_t popcount(std::uint64_t const b) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
  return __popcnt64(b);
#elif defined(_MSC_VER)
  return static_cast<std::size_t>(
      __popcnt(static_cast<std::uint32_t>(b)) +
      __popcnt(static_cast<std::uint32_t>(b >> 32U)));
#elif defined(__INTEL_COMPILER)
  return static_cast<std::size_t>(_mm_popcnt_u64(b));
#else
  return static_cast<std::size_t>(__builtin_popcountll(b));
#endif
}

}  // namespace cista

namespace cista {

template <std::size_t Size>
struct bitset {
  using block_t = std::uint64_t;
  static constexpr auto const bits_per_block = sizeof(block_t) * 8U;
  static constexpr auto const num_blocks =
      Size / bits_per_block + (Size % bits_per_block == 0U ? 0U : 1U);

  constexpr bitset() noexcept = default;
  constexpr bitset(std::string_view s) noexcept { set(s); }
  static constexpr bitset max() {
    bitset ret;
    for (auto& b : ret.blocks_) {
      b = std::numeric_limits<block_t>::max();
    }
    return ret;
  }

  auto cista_members() noexcept { return std::tie(blocks_); }

  constexpr void set(std::string_view s) noexcept {
    for (std::size_t i = 0U; i != std::min(Size, s.size()); ++i) {
      set(i, s[s.size() - i - 1U] != '0');
    }
  }

  constexpr void set(std::size_t const i, bool const val = true) noexcept {
    assert((i / bits_per_block) < num_blocks);
    auto& block = blocks_[i / bits_per_block];
    auto const bit = i % bits_per_block;
    auto const mask = block_t{1U} << bit;
    if (val) {
      block |= mask;
    } else {
      block &= (~block_t{0U} ^ mask);
    }
  }

  void reset() noexcept { blocks_ = {}; }

  bool operator[](std::size_t const i) const noexcept { return test(i); }

  std::size_t count() const noexcept {
    std::size_t sum = 0U;
    for (std::size_t i = 0U; i != num_blocks - 1U; ++i) {
      sum += popcount(blocks_[i]);
    }
    return sum + popcount(sanitized_last_block());
  }

  constexpr bool test(std::size_t const i) const noexcept {
    if (i >= Size) {
      return false;
    }
    auto const block = blocks_[i / bits_per_block];
    auto const bit = (i % bits_per_block);
    return (block & (block_t{1U} << bit)) != 0U;
  }

  std::size_t size() const noexcept { return Size; }

  bool any() const noexcept {
    for (std::size_t i = 0U; i != num_blocks - 1U; ++i) {
      if (blocks_[i] != 0U) {
        return true;
      }
    }
    return sanitized_last_block() != 0U;
  }

  bool none() const noexcept { return !any(); }

  block_t sanitized_last_block() const noexcept {
    if constexpr ((Size % bits_per_block) != 0U) {
      return blocks_[num_blocks - 1U] &
             ~((~block_t{0U}) << (Size % bits_per_block));
    } else {
      return blocks_[num_blocks - 1U];
    }
  }

  std::string to_string() const {
    std::string s{};
    s.resize(Size);
    for (std::size_t i = 0U; i != Size; ++i) {
      s[i] = test(Size - i - 1U) ? '1' : '0';
    }
    return s;
  }

  friend bool operator==(bitset const& a, bitset const& b) noexcept {
    for (std::size_t i = 0U; i != num_blocks - 1U; ++i) {
      if (a.blocks_[i] != b.blocks_[i]) {
        return false;
      }
    }
    return a.sanitized_last_block() == b.sanitized_last_block();
  }

  friend bool operator<(bitset const& a, bitset const& b) noexcept {
    auto const a_last = a.sanitized_last_block();
    auto const b_last = b.sanitized_last_block();
    if (a_last < b_last) {
      return true;
    }
    if (b_last < a_last) {
      return false;
    }

    for (int i = num_blocks - 2; i != -1; --i) {
      auto const x = a.blocks_[i];
      auto const y = b.blocks_[i];
      if (x < y) {
        return true;
      }
      if (y < x) {
        return false;
      }
    }

    return false;
  }
  friend bool operator!=(bitset const& a, bitset const& b) noexcept {
    return !(a == b);
  }

  friend bool operator>(bitset const& a, bitset const& b) noexcept {
    return b < a;
  }

  friend bool operator<=(bitset const& a, bitset const& b) noexcept {
    return !(a > b);
  }

  friend bool operator>=(bitset const& a, bitset const& b) noexcept {
    return !(a < b);
  }

  bitset& operator&=(bitset const& o) noexcept {
    for (auto i = 0U; i < num_blocks; ++i) {
      blocks_[i] &= o.blocks_[i];
    }
    return *this;
  }

  bitset& operator|=(bitset const& o) noexcept {
    for (auto i = 0U; i < num_blocks; ++i) {
      blocks_[i] |= o.blocks_[i];
    }
    return *this;
  }

  bitset& operator^=(bitset const& o) noexcept {
    for (auto i = 0U; i < num_blocks; ++i) {
      blocks_[i] ^= o.blocks_[i];
    }
    return *this;
  }

  bitset operator~() const noexcept {
    auto copy = *this;
    for (auto& b : copy.blocks_) {
      b = ~b;
    }
    return copy;
  }

  friend bitset operator&(bitset const& lhs, bitset const& rhs) noexcept {
    auto copy = lhs;
    copy &= rhs;
    return copy;
  }

  friend bitset operator|(bitset const& lhs, bitset const& rhs) noexcept {
    auto copy = lhs;
    copy |= rhs;
    return copy;
  }

  friend bitset operator^(bitset const& lhs, bitset const& rhs) noexcept {
    auto copy = lhs;
    copy ^= rhs;
    return copy;
  }

  bitset& operator>>=(std::size_t const shift) noexcept {
    if (shift >= Size) {
      reset();
      return *this;
    }

    if constexpr ((Size % bits_per_block) != 0U) {
      blocks_[num_blocks - 1U] = sanitized_last_block();
    }

    if constexpr (num_blocks == 1U) {
      blocks_[0U] >>= shift;
      return *this;
    } else {
      if (shift == 0U) {
        return *this;
      }

      auto const shift_blocks = shift / bits_per_block;
      auto const shift_bits = shift % bits_per_block;
      auto const border = num_blocks - shift_blocks - 1U;

      if (shift_bits == 0U) {
        for (std::size_t i = 0U; i <= border; ++i) {
          blocks_[i] = blocks_[i + shift_blocks];
        }
      } else {
        for (std::size_t i = 0U; i < border; ++i) {
          blocks_[i] =
              (blocks_[i + shift_blocks] >> shift_bits) |
              (blocks_[i + shift_blocks + 1] << (bits_per_block - shift_bits));
        }
        blocks_[border] = (blocks_[num_blocks - 1] >> shift_bits);
      }

      for (auto i = border + 1U; i != num_blocks; ++i) {
        blocks_[i] = 0U;
      }

      return *this;
    }
  }

  bitset& operator<<=(std::size_t const shift) noexcept {
    if (shift >= Size) {
      reset();
      return *this;
    }

    if constexpr (num_blocks == 1U) {
      blocks_[0U] <<= shift;
      return *this;
    } else {
      if (shift == 0U) {
        return *this;
      }

      auto const shift_blocks = shift / bits_per_block;
      auto const shift_bits = shift % bits_per_block;

      if (shift_bits == 0U) {
        for (auto i = std::size_t{num_blocks - 1}; i >= shift_blocks; --i) {
          blocks_[i] = blocks_[i - shift_blocks];
        }
      } else {
        for (auto i = std::size_t{num_blocks - 1}; i != shift_blocks; --i) {
          blocks_[i] =
              (blocks_[i - shift_blocks] << shift_bits) |
              (blocks_[i - shift_blocks - 1U] >> (bits_per_block - shift_bits));
        }
        blocks_[shift_blocks] = blocks_[0U] << shift_bits;
      }

      for (auto i = 0U; i != shift_blocks; ++i) {
        blocks_[i] = 0U;
      }

      return *this;
    }
  }

  bitset operator>>(std::size_t const i) const noexcept {
    auto copy = *this;
    copy >>= i;
    return copy;
  }

  bitset operator<<(std::size_t const i) const noexcept {
    auto copy = *this;
    copy <<= i;
    return copy;
  }

  friend std::ostream& operator<<(std::ostream& out, bitset const& b) {
    return out << b.to_string();
  }

  cista::array<block_t, num_blocks> blocks_{};
};

}  // namespace cista

#if __has_include("fmt/ostream.h")

// fmt/ostream.h: basic_ios::clear: iostream error

template <std::size_t Size>
struct fmt::formatter<cista::bitset<Size>> : ostream_formatter {};

#endif

#include <cassert>
#include <cinttypes>
#include <iosfwd>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>


#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <ostream>
#include <type_traits>
#include <vector>


#include <memory>

namespace cista {

template <typename T, template <typename> typename Ptr>
class allocator : public std::allocator<T> {
public:
  using size_type = std::size_t;
  using pointer = Ptr<T>;
  using const_pointer = Ptr<T const>;

  template <typename T1>
  struct rebind {
    using other = allocator<T1, Ptr>;
  };

  using std::allocator<T>::allocator;
  using std::allocator<T>::allocate;
  using std::allocator<T>::deallocate;
};

}  // namespace cista


#if defined(__has_include) && (_MSVC_LANG >= 202002L || __cplusplus >= 202002L)
#if __has_include(<bit>)
#include <bit>
#endif
#endif
#include <cstring>
#include <type_traits>


#include <cinttypes>
#include <limits>

#define PRI_O PRIdPTR

namespace cista {

using offset_t = intptr_t;

constexpr auto const NULLPTR_OFFSET = std::numeric_limits<offset_t>::min();
constexpr auto const DANGLING = std::numeric_limits<offset_t>::min() + 1U;

}  // namespace cista

#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

namespace cista {

template <typename T, typename Tag>
struct strong {
  using value_t = T;

  constexpr strong() = default;

  explicit constexpr strong(T const& v) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
      : v_{v} {}
  explicit constexpr strong(T&& v) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : v_{std::move(v)} {}

  template <typename X>
#if _MSVC_LANG >= 202002L || __cplusplus >= 202002L
    requires std::is_integral_v<std::decay_t<X>> &&
             std::is_integral_v<std::decay_t<T>>
#endif
  explicit constexpr strong(X&& x) : v_{static_cast<T>(x)} {
  }

  constexpr strong(strong&& o) noexcept(
      std::is_nothrow_move_constructible_v<T>) = default;
  constexpr strong& operator=(strong&& o) noexcept(
      std::is_nothrow_move_constructible_v<T>) = default;

  constexpr strong(strong const& o) = default;
  constexpr strong& operator=(strong const& o) = default;

  static constexpr strong invalid() {
    return strong{std::numeric_limits<T>::max()};
  }

  constexpr strong& operator++() {
    ++v_;
    return *this;
  }

  constexpr strong operator++(int) {
    auto cpy = *this;
    ++v_;
    return cpy;
  }

  constexpr strong& operator--() {
    --v_;
    return *this;
  }

  constexpr const strong operator--(int) {
    auto cpy = *this;
    --v_;
    return cpy;
  }

  constexpr strong operator+(strong const& s) const {
    return strong{static_cast<value_t>(v_ + s.v_)};
  }
  constexpr strong operator-(strong const& s) const {
    return strong{static_cast<value_t>(v_ - s.v_)};
  }
  constexpr strong operator*(strong const& s) const {
    return strong{static_cast<value_t>(v_ * s.v_)};
  }
  constexpr strong operator/(strong const& s) const {
    return strong{static_cast<value_t>(v_ / s.v_)};
  }
  constexpr strong operator+(T const& i) const {
    return strong{static_cast<value_t>(v_ + i)};
  }
  constexpr strong operator-(T const& i) const {
    return strong{static_cast<value_t>(v_ - i)};
  }
  constexpr strong operator*(T const& i) const {
    return strong{static_cast<value_t>(v_ * i)};
  }
  constexpr strong operator/(T const& i) const {
    return strong{static_cast<value_t>(v_ / i)};
  }

  constexpr strong& operator+=(T const& i) {
    v_ += i;
    return *this;
  }
  constexpr strong& operator-=(T const& i) {
    v_ -= i;
    return *this;
  }

  constexpr strong operator>>(T const& i) const {
    return strong{static_cast<value_t>(v_ >> i)};
  }
  constexpr strong operator<<(T const& i) const {
    return strong{static_cast<value_t>(v_ << i)};
  }
  constexpr strong operator>>(strong const& o) const { return v_ >> o.v_; }
  constexpr strong operator<<(strong const& o) const { return v_ << o.v_; }

  constexpr strong& operator|=(strong const& o) {
    v_ |= o.v_;
    return *this;
  }
  constexpr strong& operator&=(strong const& o) {
    v_ &= o.v_;
    return *this;
  }

  constexpr bool operator==(strong const& o) const { return v_ == o.v_; }
  constexpr bool operator!=(strong const& o) const { return v_ != o.v_; }
  constexpr bool operator<=(strong const& o) const { return v_ <= o.v_; }
  constexpr bool operator>=(strong const& o) const { return v_ >= o.v_; }
  constexpr bool operator<(strong const& o) const { return v_ < o.v_; }
  constexpr bool operator>(strong const& o) const { return v_ > o.v_; }

  constexpr bool operator==(T const& o) const { return v_ == o; }
  constexpr bool operator!=(T const& o) const { return v_ != o; }
  constexpr bool operator<=(T const& o) const { return v_ <= o; }
  constexpr bool operator>=(T const& o) const { return v_ >= o; }
  constexpr bool operator<(T const& o) const { return v_ < o; }
  constexpr bool operator>(T const& o) const { return v_ > o; }

  explicit operator T const&() const& noexcept { return v_; }

  friend std::ostream& operator<<(std::ostream& o, strong const& t) {
    return o << t.v_;
  }

  T v_;
};

template <typename T>
struct is_strong : std::false_type {};

template <typename T, typename Tag>
struct is_strong<strong<T, Tag>> : std::true_type {};

template <typename T>
constexpr auto const is_strong_v = is_strong<T>::value;

template <typename T, typename Tag>
inline constexpr typename strong<T, Tag>::value_t to_idx(
    strong<T, Tag> const& s) {
  return s.v_;
}

template <typename T>
struct base_type {
  using type = T;
};

template <typename T, typename Tag>
struct base_type<strong<T, Tag>> {
  using type = T;
};

template <typename T>
using base_t = typename base_type<T>::type;

template <typename T>
T to_idx(T const& t) {
  return t;
}

}  // namespace cista

#include <limits>

namespace std {

template <typename T, typename Tag>
class numeric_limits<cista::strong<T, Tag>> {
public:
  static constexpr cista::strong<T, Tag> min() noexcept {
    return cista::strong<T, Tag>{std::numeric_limits<T>::min()};
  }
  static constexpr cista::strong<T, Tag> max() noexcept {
    return cista::strong<T, Tag>{std::numeric_limits<T>::max()};
  }
  static constexpr bool is_integer = std::is_integral_v<T>;
};

template <typename T, typename Tag>
struct hash<cista::strong<T, Tag>> {
  size_t operator()(cista::strong<T, Tag> const& t) const {
    return hash<T>{}(t.v_);
  }
};

}  // namespace std

#if __has_include("fmt/ostream.h")


template <typename T, typename Tag>
struct fmt::formatter<cista::strong<T, Tag>> : ostream_formatter {};

#endif

namespace cista {

#if __cpp_lib_bit_cast
inline offset_t to_offset(void const* ptr) noexcept {
  return std::bit_cast<offset_t>(ptr);
}
#else
inline offset_t to_offset(void const* ptr) noexcept {
  offset_t r;
  std::memcpy(&r, &ptr, sizeof(ptr));
  return r;
}
#endif

template <typename T, typename Enable = void>
struct offset_ptr {
  constexpr offset_ptr() noexcept = default;
  constexpr offset_ptr(std::nullptr_t) noexcept : offset_{NULLPTR_OFFSET} {}
  offset_ptr(T const* p) noexcept : offset_{ptr_to_offset(p)} {}

  offset_ptr& operator=(T const* p) noexcept {
    offset_ = ptr_to_offset(p);
    return *this;
  }
  offset_ptr& operator=(std::nullptr_t) noexcept {
    offset_ = NULLPTR_OFFSET;
    return *this;
  }

  offset_ptr(offset_ptr const& o) noexcept : offset_{ptr_to_offset(o.get())} {}
  offset_ptr(offset_ptr&& o) noexcept : offset_{ptr_to_offset(o.get())} {}
  offset_ptr& operator=(offset_ptr const& o) noexcept {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }
  offset_ptr& operator=(offset_ptr&& o) noexcept {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }

  ~offset_ptr() noexcept = default;

  offset_t ptr_to_offset(T const* p) const noexcept {
    return p == nullptr ? NULLPTR_OFFSET
                        : static_cast<offset_t>(to_offset(p) - to_offset(this));
  }

  explicit operator bool() const noexcept { return offset_ != NULLPTR_OFFSET; }
  explicit operator void*() const noexcept { return get(); }
  explicit operator void const*() const noexcept { return get(); }
  operator T*() const noexcept { return get(); }
  T& operator*() const noexcept { return *get(); }
  T* operator->() const noexcept { return get(); }
  T& operator[](std::size_t const i) const noexcept { return get()[i]; }

  T* get() const noexcept {
    auto const ptr =
        offset_ == NULLPTR_OFFSET
            ? nullptr
            : reinterpret_cast<T*>(reinterpret_cast<intptr_t>(this) + offset_);
    return ptr;
  }

  template <typename Int>
  T* operator+(Int const i) const noexcept {
    return get() + i;
  }

  template <typename Int>
  T* operator-(Int const i) const noexcept {
    return get() - i;
  }

  template <typename X, typename Tag>
  constexpr T* operator+(strong<X, Tag> const& s) {
    return get() + s.v_;
  }

  template <typename X, typename Tag>
  constexpr T* operator-(strong<X, Tag> const& s) {
    return get() - s.v_;
  }

  offset_ptr& operator++() noexcept {
    offset_ = ptr_to_offset(get() + 1);
    return *this;
  }

  offset_ptr& operator--() noexcept {
    offset_ = ptr_to_offset(get() - 1);
    return *this;
  }

  offset_ptr operator++(int) const noexcept { return offset_ptr{get() + 1}; }
  offset_ptr operator--(int) const noexcept { return offset_ptr{get() - 1}; }

  offset_t offset_{NULLPTR_OFFSET};
};

template <typename T>
struct offset_ptr<T, std::enable_if_t<std::is_same_v<void, T>>> {
  constexpr offset_ptr() noexcept = default;
  constexpr offset_ptr(std::nullptr_t) noexcept : offset_{NULLPTR_OFFSET} {}
  offset_ptr(T const* p) noexcept : offset_{ptr_to_offset(p)} {}

  offset_ptr& operator=(T const* p) noexcept {
    offset_ = ptr_to_offset(p);
    return *this;
  }
  offset_ptr& operator=(std::nullptr_t) noexcept {
    offset_ = NULLPTR_OFFSET;
    return *this;
  }

  offset_ptr(offset_ptr const& o) noexcept : offset_{ptr_to_offset(o.get())} {}
  offset_ptr(offset_ptr&& o) noexcept : offset_{ptr_to_offset(o.get())} {}
  offset_ptr& operator=(offset_ptr const& o) noexcept {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }
  offset_ptr& operator=(offset_ptr&& o) noexcept {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }

  offset_t ptr_to_offset(T const* p) const noexcept {
    return p == nullptr ? NULLPTR_OFFSET
                        : static_cast<offset_t>(to_offset(p) - to_offset(this));
  }

  operator bool() const noexcept { return offset_ != NULLPTR_OFFSET; }
  explicit operator void*() const noexcept { return get(); }
  explicit operator void const*() const noexcept { return get(); }
  T* get() const noexcept {
    auto const ptr =
        offset_ == NULLPTR_OFFSET
            ? nullptr
            : reinterpret_cast<T*>(reinterpret_cast<intptr_t>(this) + offset_);
    return ptr;
  }

  friend bool operator==(std::nullptr_t, offset_ptr const& o) noexcept {
    return o.offset_ == NULLPTR_OFFSET;
  }
  friend bool operator==(offset_ptr const& o, std::nullptr_t) noexcept {
    return o.offset_ == NULLPTR_OFFSET;
  }
  friend bool operator!=(std::nullptr_t, offset_ptr const& o) noexcept {
    return o.offset_ != NULLPTR_OFFSET;
  }
  friend bool operator!=(offset_ptr const& o, std::nullptr_t) noexcept {
    return o.offset_ != NULLPTR_OFFSET;
  }

  offset_t offset_{NULLPTR_OFFSET};
};

template <class T>
struct is_pointer_helper : std::false_type {};

template <class T>
struct is_pointer_helper<T*> : std::true_type {};

template <class T>
struct is_pointer_helper<offset_ptr<T>> : std::true_type {};

template <class T>
constexpr bool is_pointer_v = is_pointer_helper<std::remove_cv_t<T>>::value;

template <class T>
struct remove_pointer_helper {
  using type = T;
};

template <class T>
struct remove_pointer_helper<T*> {
  using type = T;
};

template <class T>
struct remove_pointer_helper<offset_ptr<T>> {
  using type = T;
};

template <class T>
struct remove_pointer : remove_pointer_helper<std::remove_cv_t<T>> {};

template <typename T>
using remove_pointer_t = typename remove_pointer<T>::type;

}  // namespace cista

namespace cista {

namespace raw {

template <typename T>
using ptr = T*;

}  // namespace raw

namespace offset {

template <typename T>
using ptr = cista::offset_ptr<T>;

}  // namespace offset

template <typename T>
T* ptr_cast(raw::ptr<T> const p) noexcept {
  return p;
}

template <typename T>
T* ptr_cast(offset::ptr<T> const p) noexcept {
  return p.get();
}

}  // namespace cista

namespace cista {

template <typename E>
void throw_exception(E&& e) {
#if !defined(__cpp_exceptions) || __cpp_exceptions < 199711L
  abort();
#else
  throw e;
#endif
}

}  // namespace cista

#include <iterator>
#include <type_traits>


#include <functional>
#include <type_traits>

namespace cista {

namespace detail {

template <typename T>
struct decay {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
struct decay<std::reference_wrapper<T>> {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

}  // namespace detail

template <typename T>
using decay_t = typename detail::decay<std::remove_reference_t<T>>::type;

}  // namespace cista

namespace cista {
namespace detail {

using std::begin;
using std::end;

template <typename T, typename = void>
struct is_iterable : std::false_type {};

template <typename T>
struct is_iterable<T, std::void_t<decltype(begin(std::declval<T>())),
                                  decltype(end(std::declval<T>()))>>
    : std::true_type {};

template <typename T, typename = void>
struct it_value {
  using type = void;
};

template <typename T>
struct it_value<T, std::enable_if_t<is_iterable<T>::value>> {
  using type = decay_t<decltype(*begin(std::declval<T>()))>;
};

}  // namespace detail

using detail::is_iterable;

template <typename T>
constexpr bool is_iterable_v = is_iterable<T>::value;

template <typename T>
using it_value_t = typename detail::it_value<T>::type;

}  // namespace cista

#define CISTA_UNUSED_PARAM(param) static_cast<void>(param);

#include <stdexcept>


#include <exception>


namespace cista {

struct cista_exception : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

}  // namespace cista

namespace cista {

inline void verify(bool const condition, char const* msg) {
  if (!condition) {
    throw_exception(cista_exception{msg});
  }
}

}  // namespace cista

namespace cista {

template <typename T, template <typename> typename Ptr,
          bool IndexPointers = false, typename TemplateSizeType = std::uint32_t,
          class Allocator = allocator<T, Ptr>>
struct basic_vector {
  using size_type = base_t<TemplateSizeType>;
  using difference_type = std::ptrdiff_t;
  using access_type = TemplateSizeType;
  using reference = T&;
  using const_reference = T const&;
  using pointer = Ptr<T>;
  using const_pointer = Ptr<T const>;
  using value_type = T;
  using iterator = T*;
  using const_iterator = T const*;
  using allocator_type = Allocator;

  explicit basic_vector(allocator_type const&) noexcept {}
  basic_vector() noexcept = default;

  explicit basic_vector(size_type const size, T init = T{},
                        Allocator const& alloc = Allocator{}) {
    CISTA_UNUSED_PARAM(alloc)
    resize(size, std::move(init));
  }

  basic_vector(std::initializer_list<T> init,
               Allocator const& alloc = Allocator{}) {
    CISTA_UNUSED_PARAM(alloc)
    set(init.begin(), init.end());
  }

  template <typename It>
  basic_vector(It begin_it, It end_it) {
    set(begin_it, end_it);
  }

  basic_vector(basic_vector&& o, Allocator const& alloc = Allocator{}) noexcept
      : el_(o.el_),
        used_size_(o.used_size_),
        allocated_size_(o.allocated_size_),
        self_allocated_(o.self_allocated_) {
    CISTA_UNUSED_PARAM(alloc)
    o.reset();
  }

  basic_vector(basic_vector const& o, Allocator const& alloc = Allocator{}) {
    CISTA_UNUSED_PARAM(alloc)
    set(o);
  }

  basic_vector& operator=(basic_vector&& arr) noexcept {
    deallocate();

    el_ = arr.el_;
    used_size_ = arr.used_size_;
    self_allocated_ = arr.self_allocated_;
    allocated_size_ = arr.allocated_size_;

    arr.reset();
    return *this;
  }

  basic_vector& operator=(basic_vector const& arr) {
    if (&arr != this) {
      set(arr);
    }
    return *this;
  }

  ~basic_vector() { deallocate(); }

  void deallocate() {
    if (!self_allocated_ || el_ == nullptr) {
      return;
    }

    for (auto& el : *this) {
      el.~T();
    }

    std::free(el_);  // NOLINT
    reset();
  }

  allocator_type get_allocator() const noexcept { return {}; }

  T const* data() const noexcept { return begin(); }
  T* data() noexcept { return begin(); }
  T const* begin() const noexcept { return el_; }
  T const* end() const noexcept { return el_ + used_size_; }  // NOLINT
  T const* cbegin() const noexcept { return el_; }
  T const* cend() const noexcept { return el_ + used_size_; }  // NOLINT
  T* begin() noexcept { return el_; }
  T* end() noexcept { return el_ + used_size_; }  // NOLINT

  std::reverse_iterator<T const*> rbegin() const {
    return std::reverse_iterator<T*>(el_ + size());  // NOLINT
  }
  std::reverse_iterator<T const*> rend() const {
    return std::reverse_iterator<T*>(el_);
  }
  std::reverse_iterator<T*> rbegin() {
    return std::reverse_iterator<T*>(el_ + size());  // NOLINT
  }
  std::reverse_iterator<T*> rend() { return std::reverse_iterator<T*>(el_); }

  friend T const* begin(basic_vector const& a) noexcept { return a.begin(); }
  friend T const* end(basic_vector const& a) noexcept { return a.end(); }

  friend T* begin(basic_vector& a) noexcept { return a.begin(); }
  friend T* end(basic_vector& a) noexcept { return a.end(); }

  T const& operator[](access_type const index) const noexcept {
    assert(el_ != nullptr && index < used_size_);
    return el_[to_idx(index)];
  }
  T& operator[](access_type const index) noexcept {
    assert(el_ != nullptr && index < used_size_);
    return el_[to_idx(index)];
  }

  T& at(access_type const index) {
    if (index >= used_size_) {
      throw_exception(std::out_of_range{"vector::at(): invalid index"});
    }
    return (*this)[index];
  }

  T const& at(access_type const index) const {
    return const_cast<basic_vector*>(this)->at(index);
  }

  T const& back() const noexcept { return ptr_cast(el_)[used_size_ - 1]; }
  T& back() noexcept { return ptr_cast(el_)[used_size_ - 1]; }

  T& front() noexcept { return ptr_cast(el_)[0]; }
  T const& front() const noexcept { return ptr_cast(el_)[0]; }

  size_type size() const noexcept { return used_size_; }
  bool empty() const noexcept { return size() == 0U; }

  template <typename It>
  void set(It begin_it, It end_it) {
    auto const range_size = std::distance(begin_it, end_it);
    verify(
        range_size >= 0 && range_size <= std::numeric_limits<size_type>::max(),
        "cista::vector::set: invalid range");

    reserve(static_cast<size_type>(range_size));

    auto copy_source = begin_it;
    auto copy_target = el_;
    for (; copy_source != end_it; ++copy_source, ++copy_target) {
      new (copy_target) T{std::forward<decltype(*copy_source)>(*copy_source)};
    }

    used_size_ = static_cast<size_type>(range_size);
  }

  void set(basic_vector const& arr) {
    if constexpr (std::is_trivially_copyable_v<T>) {
      if (arr.used_size_ != 0U) {
        reserve(arr.used_size_);
        std::memcpy(data(), arr.data(), arr.used_size_ * sizeof(T));
      }
      used_size_ = arr.used_size_;
    } else {
      set(std::begin(arr), std::end(arr));
    }
  }

  friend std::ostream& operator<<(std::ostream& out, basic_vector const& v) {
    out << "[\n  ";
    auto first = true;
    for (auto const& e : v) {
      if (!first) {
        out << ",\n  ";
      }
      out << e;
      first = false;
    }
    return out << "\n]";
  }

  template <typename Arg>
  T* insert(T* it, Arg&& el) {
    auto const old_offset = std::distance(begin(), it);
    auto const old_size = used_size_;

    reserve(used_size_ + 1);
    new (el_ + used_size_) T{std::forward<Arg&&>(el)};
    ++used_size_;

    return std::rotate(begin() + old_offset, begin() + old_size, end());
  }

  template <class InputIt>
  T* insert(T* pos, InputIt first, InputIt last, std::input_iterator_tag) {
    auto const old_offset = std::distance(begin(), pos);
    auto const old_size = used_size_;

    for (; !(first == last); ++first) {
      reserve(used_size_ + 1);
      new (el_ + used_size_) T{std::forward<decltype(*first)>(*first)};
      ++used_size_;
    }

    return std::rotate(begin() + old_offset, begin() + old_size, end());
  }

  template <class FwdIt>
  T* insert(T* pos, FwdIt first, FwdIt last, std::forward_iterator_tag) {
    if (empty()) {
      set(first, last);
      return begin();
    }

    auto const pos_idx = pos - begin();
    auto const new_count = static_cast<size_type>(std::distance(first, last));
    reserve(used_size_ + new_count);
    pos = begin() + pos_idx;

    for (auto src_last = end() - 1, dest_last = end() + new_count - 1;
         !(src_last == pos - 1); --src_last, --dest_last) {
      if (dest_last >= end()) {
        new (dest_last) T(std::move(*src_last));
      } else {
        *dest_last = std::move(*src_last);
      }
    }

    for (auto insert_ptr = pos; !(first == last); ++first, ++insert_ptr) {
      if (insert_ptr >= end()) {
        new (insert_ptr) T(std::forward<decltype(*first)>(*first));
      } else {
        *insert_ptr = std::forward<decltype(*first)>(*first);
      }
    }

    used_size_ += new_count;

    return pos;
  }

  template <class It>
  T* insert(T* pos, It first, It last) {
    return insert(pos, first, last,
                  typename std::iterator_traits<It>::iterator_category());
  }

  void push_back(T const& el) {
    reserve(used_size_ + 1U);
    new (el_ + used_size_) T(el);
    ++used_size_;
  }

  template <typename... Args>
  T& emplace_back(Args&&... el) {
    reserve(used_size_ + 1U);
    new (el_ + used_size_) T{std::forward<Args>(el)...};
    T* ptr = el_ + used_size_;
    ++used_size_;
    return *ptr;
  }

  void resize(size_type const size, T init = T{}) {
    reserve(size);
    for (auto i = used_size_; i < size; ++i) {
      new (el_ + i) T{init};
    }
    used_size_ = size;
  }

  void pop_back() noexcept(noexcept(std::declval<T>().~T())) {
    --used_size_;
    el_[used_size_].~T();
  }

  void clear() {
    for (auto& el : *this) {
      el.~T();
    }
    used_size_ = 0;
  }

  void reserve(size_type new_size) {
    new_size = std::max(allocated_size_, new_size);

    if (allocated_size_ >= new_size) {
      return;
    }

    auto next_size = next_power_of_two(new_size);
    auto num_bytes = static_cast<std::size_t>(next_size) * sizeof(T);
    auto mem_buf = static_cast<T*>(std::malloc(num_bytes));  // NOLINT
    if (mem_buf == nullptr) {
      throw_exception(std::bad_alloc());
    }

    if (size() != 0) {
      try {
        auto move_target = mem_buf;
        for (auto& el : *this) {
          new (move_target++) T(std::move(el));
        }

        for (auto& el : *this) {
          el.~T();
        }
      } catch (...) {
        assert(0);
      }
    }

    auto free_me = el_;
    el_ = mem_buf;
    if (self_allocated_) {
      std::free(free_me);  // NOLINT
    }

    self_allocated_ = true;
    allocated_size_ = next_size;
  }

  T* erase(T* pos) {
    auto const r = pos;
    T* last = end() - 1;
    while (pos < last) {
      std::swap(*pos, *(pos + 1));
      pos = pos + 1;
    }
    pos->~T();
    --used_size_;
    return r;
  }

  T* erase(T* first, T* last) {
    if (first != last) {
      auto const new_end = std::move(last, end(), first);
      for (auto it = new_end; it != end(); ++it) {
        it->~T();
      }
      used_size_ -= static_cast<size_type>(std::distance(new_end, end()));
    }
    return end();
  }

  bool contains(T const* el) const noexcept {
    return el >= begin() && el < end();
  }

  std::size_t index_of(T const* el) const noexcept {
    assert(contains(el));
    return std::distance(begin(), el);
  }

  friend bool operator==(basic_vector const& a,
                         basic_vector const& b) noexcept {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
  }
  friend bool operator!=(basic_vector const& a,
                         basic_vector const& b) noexcept {
    return !(a == b);
  }
  friend bool operator<(basic_vector const& a, basic_vector const& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
  }
  friend bool operator>(basic_vector const& a, basic_vector const& b) noexcept {
    return b < a;
  }
  friend bool operator<=(basic_vector const& a,
                         basic_vector const& b) noexcept {
    return !(a > b);
  }
  friend bool operator>=(basic_vector const& a,
                         basic_vector const& b) noexcept {
    return !(a < b);
  }

  void reset() noexcept {
    el_ = nullptr;
    used_size_ = {};
    allocated_size_ = {};
    self_allocated_ = false;
  }

  Ptr<T> el_{nullptr};
  size_type used_size_{0U};
  size_type allocated_size_{0U};
  bool self_allocated_{false};
  std::uint8_t __fill_0__{0U};
  std::uint16_t __fill_1__{0U};
  std::uint32_t __fill_2__{0U};
};

namespace raw {

template <typename T>
using vector = basic_vector<T, ptr>;

template <typename T>
using indexed_vector = basic_vector<T, ptr, true>;

template <typename Key, typename Value>
using vector_map = basic_vector<Value, ptr, false, Key>;

template <typename It, typename UnaryOperation>
auto to_vec(It s, It e, UnaryOperation&& op)
    -> vector<decay_t<decltype(op(*s))>> {
  vector<decay_t<decltype(op(*s))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(std::distance(s, e)));
  std::transform(s, e, std::back_inserter(v), op);
  return v;
}

template <typename Container, typename UnaryOperation>
auto to_vec(Container const& c, UnaryOperation&& op)
    -> vector<decltype(op(*std::begin(c)))> {
  vector<decltype(op(*std::begin(c)))> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::transform(std::begin(c), std::end(c), std::back_inserter(v), op);
  return v;
}

template <typename Container>
auto to_vec(Container const& c) -> vector<decay_t<decltype(*std::begin(c))>> {
  vector<decay_t<decltype(*std::begin(c))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::copy(std::begin(c), std::end(c), std::back_inserter(v));
  return v;
}
template <typename It, typename UnaryOperation>
auto to_indexed_vec(It s, It e, UnaryOperation&& op)
    -> indexed_vector<decay_t<decltype(op(*s))>> {
  indexed_vector<decay_t<decltype(op(*s))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(std::distance(s, e)));
  std::transform(s, e, std::back_inserter(v), op);
  return v;
}

template <typename Container, typename UnaryOperation>
auto to_indexed_vec(Container const& c, UnaryOperation&& op)
    -> indexed_vector<decay_t<decltype(op(*std::begin(c)))>> {
  indexed_vector<decay_t<decltype(op(*std::begin(c)))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::transform(std::begin(c), std::end(c), std::back_inserter(v), op);
  return v;
}

template <typename Container>
auto to_indexed_vec(Container const& c)
    -> indexed_vector<decay_t<decltype(*std::begin(c))>> {
  indexed_vector<decay_t<decltype(*std::begin(c))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::copy(std::begin(c), std::end(c), std::back_inserter(v));
  return v;
}

}  // namespace raw

namespace offset {

template <typename T>
using vector = basic_vector<T, ptr>;

template <typename T>
using indexed_vector = basic_vector<T, ptr, true>;

template <typename Key, typename Value>
using vector_map = basic_vector<Value, ptr, false, Key>;

template <typename It, typename UnaryOperation>
auto to_vec(It s, It e, UnaryOperation&& op)
    -> vector<decay_t<decltype(op(*s))>> {
  vector<decay_t<decltype(op(*s))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(std::distance(s, e)));
  std::transform(s, e, std::back_inserter(v), op);
  return v;
}

template <typename Container, typename UnaryOperation>
auto to_vec(Container&& c, UnaryOperation&& op)
    -> vector<decltype(op(*std::begin(c)))> {
  vector<decltype(op(*std::begin(c)))> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(c.size()));
  std::transform(std::begin(c), std::end(c), std::back_inserter(v), op);
  return v;
}

template <typename Container>
auto to_vec(Container&& c) -> vector<decay_t<decltype(*std::begin(c))>> {
  vector<decay_t<decltype(*std::begin(c))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::copy(std::begin(c), std::end(c), std::back_inserter(v));
  return v;
}
template <typename It, typename UnaryOperation>
auto to_indexed_vec(It s, It e, UnaryOperation&& op)
    -> indexed_vector<decay_t<decltype(op(*s))>> {
  indexed_vector<decay_t<decltype(op(*s))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(std::distance(s, e)));
  std::transform(s, e, std::back_inserter(v), op);
  return v;
}

template <typename Container, typename UnaryOperation>
auto to_indexed_vec(Container const& c, UnaryOperation&& op)
    -> indexed_vector<decay_t<decltype(op(*std::begin(c)))>> {
  indexed_vector<decay_t<decltype(op(*std::begin(c)))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::transform(std::begin(c), std::end(c), std::back_inserter(v), op);
  return v;
}

template <typename Container>
auto to_indexed_vec(Container const& c)
    -> indexed_vector<decay_t<decltype(*std::begin(c))>> {
  indexed_vector<decay_t<decltype(*std::begin(c))>> v;
  v.reserve(static_cast<typename decltype(v)::size_type>(
      std::distance(std::begin(c), std::end(c))));
  std::copy(std::begin(c), std::end(c), std::back_inserter(v));
  return v;
}

}  // namespace offset

#undef CISTA_TO_VEC

}  // namespace cista

namespace cista {

template <typename Vec, typename Key = typename Vec::size_type>
struct basic_bitvec {
  using block_t = typename Vec::value_type;
  using size_type = typename Vec::size_type;
  static constexpr auto const bits_per_block =
      static_cast<size_type>(sizeof(block_t) * 8);

  constexpr basic_bitvec() noexcept {}
  constexpr basic_bitvec(std::string_view s) noexcept { set(s); }
  constexpr basic_bitvec(Vec&& v) noexcept : blocks_{std::move(v)} {}
  static constexpr basic_bitvec max(std::size_t const size) {
    basic_bitvec ret;
    ret.resize(size);
    for (auto& b : ret.blocks_) {
      b = std::numeric_limits<block_t>::max();
    }
    return ret;
  }

  auto cista_members() noexcept { return std::tie(blocks_); }

  static constexpr size_type num_blocks(size_type num_bits) {
    return static_cast<size_type>(num_bits / bits_per_block +
                                  (num_bits % bits_per_block == 0 ? 0 : 1));
  }

  void resize(size_type const new_size) {
    if (new_size == size_) {
      return;
    }

    if (!empty() && (size_ % bits_per_block) != 0U) {
      blocks_[blocks_.size() - 1] &=
          ~((~block_t{0}) << (size_ % bits_per_block));
    }
    blocks_.resize(num_blocks(new_size));
    size_ = new_size;
  }

  constexpr void set(std::string_view s) noexcept {
    assert(std::all_of(begin(s), end(s),
                       [](char const c) { return c == '0' || c == '1'; }));
    resize(s.size());
    for (auto i = std::size_t{0U}; i != std::min(size_, s.size()); ++i) {
      set(i, s[s.size() - i - 1] != '0');
    }
  }

  constexpr void set(Key const i, bool const val = true) noexcept {
    assert(i < size_);
    assert((i / bits_per_block) < blocks_.size());
    auto& block = blocks_[static_cast<size_type>(i) / bits_per_block];
    auto const bit = i % bits_per_block;
    if (val) {
      block |= (block_t{1U} << bit);
    } else {
      block &= (~block_t{0U} ^ (block_t{1U} << bit));
    }
  }

  void reset() noexcept { blocks_ = {}; }

  bool operator[](Key const i) const noexcept { return test(i); }

  std::size_t count() const noexcept {
    auto sum = std::size_t{0U};
    for (auto i = size_type{0U}; i != blocks_.size() - 1; ++i) {
      sum += popcount(blocks_[i]);
    }
    return sum + popcount(sanitized_last_block());
  }

  constexpr bool test(Key const i) const noexcept {
    if (i >= size_) {
      return false;
    }
    assert((i / bits_per_block) < blocks_.size());
    auto const block =
        blocks_[static_cast<size_type>(to_idx(i)) / bits_per_block];
    auto const bit = (to_idx(i) % bits_per_block);
    return (block & (block_t{1U} << bit)) != 0U;
  }

  template <typename Fn>
  void for_each_set_bit(Fn&& f) const {
    if (empty()) {
      return;
    }
    auto const check_block = [&](size_type const i, block_t const block) {
      if (block != 0U) {
        for (auto bit = size_type{0U}; bit != bits_per_block; ++bit) {
          if ((block & (block_t{1U} << bit)) != 0U) {
            f(Key{i * bits_per_block + bit});
          }
        }
      }
    };
    for (auto i = size_type{0U}; i != blocks_.size() - 1; ++i) {
      check_block(i, blocks_[i]);
    }
    check_block(blocks_.size() - 1, sanitized_last_block());
  }

  size_type size() const noexcept { return size_; }
  bool empty() const noexcept { return size() == 0U; }

  bool any() const noexcept {
    if (empty()) {
      return false;
    }
    for (auto i = size_type{0U}; i != blocks_.size() - 1; ++i) {
      if (blocks_[i] != 0U) {
        return true;
      }
    }
    return sanitized_last_block() != 0U;
  }

  bool none() const noexcept { return !any(); }

  block_t sanitized_last_block() const noexcept {
    if ((size_ % bits_per_block) != 0) {
      return blocks_[blocks_.size() - 1] &
             ~((~block_t{0}) << (size_ % bits_per_block));
    } else {
      return blocks_[blocks_.size() - 1];
    }
  }

  std::string str() const {
    auto s = std::string{};
    s.resize(size_);
    for (auto i = 0U; i != size_; ++i) {
      s[i] = test(size_ - i - 1) ? '1' : '0';
    }
    return s;
  }

  friend bool operator==(basic_bitvec const& a,
                         basic_bitvec const& b) noexcept {
    assert(a.size() == b.size());

    if (a.empty() && b.empty()) {
      return true;
    }

    for (auto i = size_type{0U}; i != a.blocks_.size() - 1; ++i) {
      if (a.blocks_[i] != b.blocks_[i]) {
        return false;
      }
    }
    return a.sanitized_last_block() == b.sanitized_last_block();
  }

  friend bool operator<(basic_bitvec const& a, basic_bitvec const& b) noexcept {
    assert(a.size() == b.size());
    if (a.empty() && b.empty()) {
      return false;
    }

    auto const a_last = a.sanitized_last_block();
    auto const b_last = b.sanitized_last_block();
    if (a_last < b_last) {
      return true;
    } else if (b_last < a_last) {
      return false;
    }

    for (int i = a.blocks_.size() - 2; i != -1; --i) {
      if (a.blocks_[i] < b.blocks_[i]) {
        return true;
      } else if (b.blocks_[i] < a.blocks_[i]) {
        return false;
      }
    }

    return false;
  }
  friend bool operator!=(basic_bitvec const& a,
                         basic_bitvec const& b) noexcept {
    return !(a == b);
  }

  friend bool operator>(basic_bitvec const& a, basic_bitvec const& b) noexcept {
    return !(a.empty() && b.empty()) && b < a;
  }

  friend bool operator<=(basic_bitvec const& a,
                         basic_bitvec const& b) noexcept {
    return (a.empty() && b.empty()) || !(a > b);
  }

  friend bool operator>=(basic_bitvec const& a,
                         basic_bitvec const& b) noexcept {
    return (a.empty() && b.empty()) || !(a < b);
  }

  basic_bitvec& operator&=(basic_bitvec const& o) noexcept {
    assert(size() == o.size());

    for (auto i = 0U; i < blocks_.size(); ++i) {
      blocks_[i] &= o.blocks_[i];
    }
    return *this;
  }

  basic_bitvec& operator|=(basic_bitvec const& o) noexcept {
    assert(size() == o.size());

    for (auto i = 0U; i < blocks_.size(); ++i) {
      blocks_[i] |= o.blocks_[i];
    }
    return *this;
  }

  basic_bitvec& operator^=(basic_bitvec const& o) noexcept {
    assert(size() == o.size());

    for (auto i = 0U; i < blocks_.size(); ++i) {
      blocks_[i] ^= o.blocks_[i];
    }
    return *this;
  }

  basic_bitvec operator~() const noexcept {
    auto copy = *this;
    for (auto& b : copy.blocks_) {
      b = ~b;
    }
    return copy;
  }

  friend basic_bitvec operator&(basic_bitvec const& lhs,
                                basic_bitvec const& rhs) noexcept {
    auto copy = lhs;
    copy &= rhs;
    return copy;
  }

  friend basic_bitvec operator|(basic_bitvec const& lhs,
                                basic_bitvec const& rhs) noexcept {
    auto copy = lhs;
    copy |= rhs;
    return copy;
  }

  friend basic_bitvec operator^(basic_bitvec const& lhs,
                                basic_bitvec const& rhs) noexcept {
    auto copy = lhs;
    copy ^= rhs;
    return copy;
  }

  basic_bitvec& operator>>=(std::size_t const shift) noexcept {
    if (shift >= size_) {
      reset();
      return *this;
    }

    if ((size_ % bits_per_block) != 0) {
      blocks_[blocks_.size() - 1] = sanitized_last_block();
    }

    if (blocks_.size() == 1U) {
      blocks_[0] >>= shift;
      return *this;
    } else {
      if (shift == 0U) {
        return *this;
      }

      auto const shift_blocks = shift / bits_per_block;
      auto const shift_bits = shift % bits_per_block;
      auto const border = blocks_.size() - shift_blocks - 1U;

      if (shift_bits == 0U) {
        for (auto i = std::size_t{0U}; i <= border; ++i) {
          blocks_[i] = blocks_[i + shift_blocks];
        }
      } else {
        for (auto i = std::size_t{0U}; i < border; ++i) {
          blocks_[i] =
              (blocks_[i + shift_blocks] >> shift_bits) |
              (blocks_[i + shift_blocks + 1] << (bits_per_block - shift_bits));
        }
        blocks_[border] = (blocks_[blocks_.size() - 1] >> shift_bits);
      }

      for (auto i = border + 1; i != blocks_.size(); ++i) {
        blocks_[i] = 0U;
      }

      return *this;
    }
  }

  basic_bitvec& operator<<=(std::size_t const shift) noexcept {
    if (shift >= size_) {
      reset();
      return *this;
    }

    if (blocks_.size() == 1U) {
      blocks_[0] <<= shift;
      return *this;
    } else {
      if (shift == 0U) {
        return *this;
      }

      auto const shift_blocks = shift / bits_per_block;
      auto const shift_bits = shift % bits_per_block;

      if (shift_bits == 0U) {
        for (auto i = size_type{blocks_.size() - 1}; i >= shift_blocks; --i) {
          blocks_[i] = blocks_[i - shift_blocks];
        }
      } else {
        for (auto i = size_type{blocks_.size() - 1}; i != shift_blocks; --i) {
          blocks_[i] =
              (blocks_[i - shift_blocks] << shift_bits) |
              (blocks_[i - shift_blocks - 1] >> (bits_per_block - shift_bits));
        }
        blocks_[shift_blocks] = blocks_[0] << shift_bits;
      }

      for (auto i = 0U; i != shift_blocks; ++i) {
        blocks_[i] = 0U;
      }

      return *this;
    }
  }

  basic_bitvec operator>>(std::size_t const i) const noexcept {
    auto copy = *this;
    copy >>= i;
    return copy;
  }

  basic_bitvec operator<<(std::size_t const i) const noexcept {
    auto copy = *this;
    copy <<= i;
    return copy;
  }

  friend std::ostream& operator<<(std::ostream& out, basic_bitvec const& b) {
    return out << b.str();
  }

  size_type size_{0U};
  Vec blocks_;
};

namespace offset {
using bitvec = basic_bitvec<vector<std::uint64_t>>;
}

namespace raw {
using bitvec = basic_bitvec<vector<std::uint64_t>>;
}

}  // namespace cista

#include <cassert>
#include <cinttypes>
#include <cstring>

#include <ostream>
#include <string>
#include <string_view>


#include <type_traits>

namespace cista {

template <typename T, typename = void>
struct is_char_array_helper : std::false_type {};

template <std::size_t N>
struct is_char_array_helper<char const[N]> : std::true_type {};

template <std::size_t N>
struct is_char_array_helper<char[N]> : std::true_type {};

template <typename T>
constexpr bool is_char_array_v = is_char_array_helper<T>::value;

template <typename Ptr>
struct is_string_helper : std::false_type {};

template <class T>
constexpr bool is_string_v = is_string_helper<std::remove_cv_t<T>>::value;

}  // namespace cista

namespace cista {

// This class is a generic string container that stores an extra \0 byte post
// the last byte of the valid data. This makes sure the pointer returned by
// data() can be passed as a C-string.
//
// The content stored within this container can contain binary data, that is,
// any number of \0 bytes is permitted within [data(), data() + size()).
template <typename Ptr = char const*>
struct generic_cstring {
  using msize_t = std::uint32_t;
  using value_type = char;

  static msize_t mstrlen(char const* s) noexcept {
    return static_cast<msize_t>(std::strlen(s));
  }

  static constexpr struct owning_t {
  } owning{};
  static constexpr struct non_owning_t {
  } non_owning{};

  constexpr generic_cstring() noexcept {}
  ~generic_cstring() noexcept { reset(); }

  generic_cstring(std::string_view s, owning_t const) { set_owning(s); }
  generic_cstring(std::string_view s, non_owning_t const) { set_non_owning(s); }
  generic_cstring(std::string const& s, owning_t const) { set_owning(s); }
  generic_cstring(std::string const& s, non_owning_t const) {
    set_non_owning(s);
  }
  generic_cstring(char const* s, owning_t const) { set_owning(s); }
  generic_cstring(char const* s, non_owning_t const) { set_non_owning(s); }

  char* begin() noexcept { return data(); }
  char* end() noexcept { return data() + size(); }
  char const* begin() const noexcept { return data(); }
  char const* end() const noexcept { return data() + size(); }

  friend char const* begin(generic_cstring const& s) { return s.begin(); }
  friend char* begin(generic_cstring& s) { return s.begin(); }
  friend char const* end(generic_cstring const& s) { return s.end(); }
  friend char* end(generic_cstring& s) { return s.end(); }

  bool is_short() const noexcept { return s_.remaining_ >= 0; }

  bool is_owning() const { return is_short() || h_.self_allocated_; }

  void reset() noexcept {
    if (!is_short() && h_.self_allocated_) {
      std::free(data());
    }
    s_ = stack{};
  }

  void set_owning(std::string const& s) {
    set_owning(s.data(), static_cast<msize_t>(s.size()));
  }

  void set_owning(std::string_view s) {
    set_owning(s.data(), static_cast<msize_t>(s.size()));
  }

  void set_owning(char const* str) {
    assert(str != nullptr);
    set_owning(str, mstrlen(str));
  }

  static constexpr msize_t short_length_limit = 15U;

  void set_owning(char const* str, msize_t const len) {
    assert(str != nullptr || len == 0U);
    reset();
    if (len == 0U) {
      return;
    }
    s_.remaining_ = static_cast<int8_t>(
        std::max(static_cast<int32_t>(short_length_limit - len), -1));
    if (is_short()) {
      std::memcpy(s_.s_, str, len);
    } else {
      h_ = heap(len, owning);
      std::memcpy(data(), str, len);
    }
  }

  void set_non_owning(std::string const& v) {
    set_non_owning(v.data(), static_cast<msize_t>(v.size()));
  }

  void set_non_owning(std::string_view v) {
    set_non_owning(v.data(), static_cast<msize_t>(v.size()));
  }

  void set_non_owning(char const* str) {
    set_non_owning(str, str != nullptr ? mstrlen(str) : 0);
  }

  void set_non_owning(char const* str, msize_t const len) {
    assert(str != nullptr || len == 0U);
    reset();
    h_ = heap(str, len, non_owning);
  }

  void move_from(generic_cstring&& s) noexcept {
    std::memcpy(static_cast<void*>(this), &s, sizeof(*this));
    if constexpr (std::is_pointer_v<Ptr>) {
      std::memset(static_cast<void*>(&s), 0, sizeof(*this));
    } else if (!s.is_short()) {
      h_.ptr_ = s.h_.ptr_;
      s.s_ = stack{};
    }
  }

  void copy_from(generic_cstring const& s) {
    reset();
    if (s.is_short()) {
      std::memcpy(static_cast<void*>(this), &s, sizeof(s));
    } else if (s.h_.self_allocated_) {
      set_owning(s.data(), s.size());
    } else {
      set_non_owning(s.data(), s.size());
    }
  }

  bool empty() const noexcept { return size() == 0U; }
  std::string_view view() const noexcept { return {data(), size()}; }
  std::string str() const { return {data(), size()}; }

  operator std::string_view() const { return view(); }

  char& operator[](std::size_t const i) noexcept { return data()[i]; }
  char const& operator[](std::size_t const i) const noexcept {
    return data()[i];
  }

  friend std::ostream& operator<<(std::ostream& out, generic_cstring const& s) {
    return out << s.view();
  }

  friend bool operator==(generic_cstring const& a,
                         generic_cstring const& b) noexcept {
    return a.view() == b.view();
  }

  friend bool operator!=(generic_cstring const& a,
                         generic_cstring const& b) noexcept {
    return a.view() != b.view();
  }

  friend bool operator<(generic_cstring const& a,
                        generic_cstring const& b) noexcept {
    return a.view() < b.view();
  }

  friend bool operator>(generic_cstring const& a,
                        generic_cstring const& b) noexcept {
    return a.view() > b.view();
  }

  friend bool operator<=(generic_cstring const& a,
                         generic_cstring const& b) noexcept {
    return a.view() <= b.view();
  }

  friend bool operator>=(generic_cstring const& a,
                         generic_cstring const& b) noexcept {
    return a.view() >= b.view();
  }

  friend bool operator==(generic_cstring const& a,
                         std::string_view b) noexcept {
    return a.view() == b;
  }

  friend bool operator!=(generic_cstring const& a,
                         std::string_view b) noexcept {
    return a.view() != b;
  }

  friend bool operator<(generic_cstring const& a, std::string_view b) noexcept {
    return a.view() < b;
  }

  friend bool operator>(generic_cstring const& a, std::string_view b) noexcept {
    return a.view() > b;
  }

  friend bool operator<=(generic_cstring const& a,
                         std::string_view b) noexcept {
    return a.view() <= b;
  }

  friend bool operator>=(generic_cstring const& a,
                         std::string_view b) noexcept {
    return a.view() >= b;
  }

  friend bool operator==(std::string_view a,
                         generic_cstring const& b) noexcept {
    return a == b.view();
  }

  friend bool operator!=(std::string_view a,
                         generic_cstring const& b) noexcept {
    return a != b.view();
  }

  friend bool operator<(std::string_view a, generic_cstring const& b) noexcept {
    return a < b.view();
  }

  friend bool operator>(std::string_view a, generic_cstring const& b) noexcept {
    return a > b.view();
  }

  friend bool operator<=(std::string_view a,
                         generic_cstring const& b) noexcept {
    return a <= b.view();
  }

  friend bool operator>=(std::string_view a,
                         generic_cstring const& b) noexcept {
    return a >= b.view();
  }

  friend bool operator==(generic_cstring const& a, char const* b) noexcept {
    return a.view() == std::string_view{b};
  }

  friend bool operator!=(generic_cstring const& a, char const* b) noexcept {
    return a.view() != std::string_view{b};
  }

  friend bool operator<(generic_cstring const& a, char const* b) noexcept {
    return a.view() < std::string_view{b};
  }

  friend bool operator>(generic_cstring const& a, char const* b) noexcept {
    return a.view() > std::string_view{b};
  }

  friend bool operator<=(generic_cstring const& a, char const* b) noexcept {
    return a.view() <= std::string_view{b};
  }

  friend bool operator>=(generic_cstring const& a, char const* b) noexcept {
    return a.view() >= std::string_view{b};
  }

  friend bool operator==(char const* a, generic_cstring const& b) noexcept {
    return std::string_view{a} == b.view();
  }

  friend bool operator!=(char const* a, generic_cstring const& b) noexcept {
    return std::string_view{a} != b.view();
  }

  friend bool operator<(char const* a, generic_cstring const& b) noexcept {
    return std::string_view{a} < b.view();
  }

  friend bool operator>(char const* a, generic_cstring const& b) noexcept {
    return std::string_view{a} > b.view();
  }

  friend bool operator<=(char const* a, generic_cstring const& b) noexcept {
    return std::string_view{a} <= b.view();
  }

  friend bool operator>=(char const* a, generic_cstring const& b) noexcept {
    return std::string_view{a} >= b.view();
  }

  char const* internal_data() const noexcept {
    if constexpr (std::is_pointer_v<Ptr>) {
      return is_short() ? s_.s_ : h_.ptr_;
    } else {
      return is_short() ? s_.s_ : h_.ptr_.get();
    }
  }

  char* data() noexcept { return const_cast<char*>(internal_data()); }
  char const* data() const noexcept { return internal_data(); }

  msize_t size() const noexcept { return is_short() ? s_.size() : h_.size(); }

  struct heap {
    Ptr ptr_{nullptr};
    std::uint32_t size_{0};
    bool self_allocated_{false};
    char __fill__[sizeof(uintptr_t) == 8 ? 2 : 6]{0};
    int8_t minus_one_{-1};  // The offset of this field needs to match the
                            // offset of stack::remaining_ below.

    heap() = default;
    heap(msize_t len, owning_t) {
      char* mem = static_cast<char*>(std::malloc(len + 1));
      if (mem == nullptr) {
        throw_exception(std::bad_alloc{});
      }
      mem[len] = '\0';
      ptr_ = mem;
      size_ = len;
      self_allocated_ = true;
    }
    heap(Ptr ptr, msize_t len, non_owning_t) {
      ptr_ = ptr;
      size_ = len;
    }

    msize_t size() const { return size_; }
  };

  struct stack {
    char s_[short_length_limit]{0};
    int8_t remaining_{
        short_length_limit};  // The remaining capacity the inline buffer still
                              // has. A negative value indicates the buffer is
                              // not inline. In case the inline buffer is fully
                              // occupied, this field also serves as a null
                              // terminator.

    msize_t size() const {
      assert(remaining_ >= 0);
      return short_length_limit - static_cast<msize_t>(remaining_);
    }
  };

  union {
    heap h_;
    stack s_{};
  };
};

template <typename Ptr>
struct basic_cstring : public generic_cstring<Ptr> {
  using base = generic_cstring<Ptr>;

  using base::base;
  using base::operator std::string_view;

  friend std::ostream& operator<<(std::ostream& out, basic_cstring const& s) {
    return out << s.view();
  }

  explicit operator std::string() const { return {base::data(), base::size()}; }

  basic_cstring(std::string_view s) : base{s, base::owning} {}
  basic_cstring(std::string const& s) : base{s, base::owning} {}
  basic_cstring(char const* s) : base{s, base::owning} {}
  basic_cstring(char const* s, typename base::msize_t const len)
      : base{s, len, base::owning} {}

  basic_cstring(basic_cstring const& o) : base{o.view(), base::owning} {}
  basic_cstring(basic_cstring&& o) { base::move_from(std::move(o)); }

  basic_cstring& operator=(basic_cstring const& o) {
    base::set_owning(o.data(), o.size());
    return *this;
  }

  basic_cstring& operator=(basic_cstring&& o) {
    base::move_from(std::move(o));
    return *this;
  }

  basic_cstring& operator=(char const* s) {
    base::set_owning(s);
    return *this;
  }
  basic_cstring& operator=(std::string const& s) {
    base::set_owning(s);
    return *this;
  }
  basic_cstring& operator=(std::string_view s) {
    base::set_owning(s);
    return *this;
  }
};

template <typename Ptr>
struct basic_cstring_view : public generic_cstring<Ptr> {
  using base = generic_cstring<Ptr>;

  using base::base;
  using base::operator std::string_view;

  friend std::ostream& operator<<(std::ostream& out,
                                  basic_cstring_view const& s) {
    return out << s.view();
  }

  basic_cstring_view(std::string_view s) : base{s, base::non_owning} {}
  basic_cstring_view(std::string const& s) : base{s, base::non_owning} {}
  basic_cstring_view(char const* s) : base{s, base::non_owning} {}
  basic_cstring_view(char const* s, typename base::msize_t const len)
      : base{s, len, base::non_owning} {}

  basic_cstring_view(basic_cstring_view const& o) {
    base::set_non_owning(o.data(), o.size());
  }
  basic_cstring_view(basic_cstring_view&& o) {
    base::set_non_owning(o.data(), o.size());
  }
  basic_cstring_view& operator=(basic_cstring_view const& o) {
    base::set_non_owning(o.data(), o.size());
    return *this;
  }
  basic_cstring_view& operator=(basic_cstring_view&& o) {
    base::set_non_owning(o.data(), o.size());
    return *this;
  }

  basic_cstring_view& operator=(char const* s) {
    base::set_non_owning(s);
    return *this;
  }
  basic_cstring_view& operator=(std::string_view s) {
    base::set_non_owning(s);
    return *this;
  }
  basic_cstring_view& operator=(std::string const& s) {
    base::set_non_owning(s);
    return *this;
  }
};

template <typename Ptr>
struct is_string_helper<generic_cstring<Ptr>> : std::true_type {};

template <typename Ptr>
struct is_string_helper<basic_cstring<Ptr>> : std::true_type {};

template <typename Ptr>
struct is_string_helper<basic_cstring_view<Ptr>> : std::true_type {};

namespace raw {
using generic_cstring = generic_cstring<offset::ptr<char const>>;
using cstring = basic_cstring<offset::ptr<char const>>;
}  // namespace raw

namespace offset {
using generic_cstring = generic_cstring<offset::ptr<char const>>;
using cstring = basic_cstring<offset::ptr<char const>>;
}  // namespace offset

}  // namespace cista

#include <cassert>
#include <iterator>
#include <type_traits>


namespace cista {

template <typename DataVec, typename IndexVec>
struct fws_multimap_entry {
  using iterator = typename DataVec::const_iterator;
  using index_t = typename IndexVec::value_type;
  using value_t = typename DataVec::value_type;

  static_assert(!std::is_same_v<std::remove_cv<value_t>, bool>,
                "bool not supported");

  fws_multimap_entry(DataVec const& data, IndexVec const& index,
                     index_t const key)
      : data_{data}, index_start_{index[key]}, index_end_{index[key + 1]} {}

  fws_multimap_entry(DataVec const& data, index_t const start_index,
                     index_t const end_index)
      : data_{data}, index_start_{start_index}, index_end_{end_index} {}

  iterator begin() const { return data_.begin() + index_start_; }
  iterator end() const { return data_.begin() + index_end_; }

  iterator cbegin() const { return begin(); }
  iterator cend() const { return end(); }

  friend iterator begin(fws_multimap_entry const& e) { return e.begin(); }
  friend iterator end(fws_multimap_entry const& e) { return e.end(); }

  value_t const& operator[](index_t const index) const {
    return data_[data_index(index)];
  }

  index_t data_index(index_t const index) const noexcept {
    assert(index_start_ + index < data_.size());
    return index_start_ + index;
  }

  std::size_t size() const noexcept { return index_end_ - index_start_; }
  bool empty() const noexcept { return size() == 0U; }

  DataVec const& data_;
  index_t const index_start_;
  index_t const index_end_;
};

template <typename MapType, typename EntryType>
struct fws_multimap_iterator {
  using iterator_category = std::random_access_iterator_tag;
  using value_type = EntryType;
  using difference_type = int;
  using pointer = value_type*;
  using reference = value_type&;
  using index_t = typename MapType::index_t;

  fws_multimap_iterator(MapType const& map, index_t const index)
      : map_{map}, index_{index} {}

  value_type operator*() const { return {map_[index_]}; }

  fws_multimap_iterator& operator+=(int const n) {
    index_ += n;
    return *this;
  }

  fws_multimap_iterator& operator-=(int const n) {
    index_ -= n;
    return *this;
  }

  fws_multimap_iterator& operator++() {
    ++index_;
    return *this;
  }

  fws_multimap_iterator& operator--() {
    --index_;
    return *this;
  }

  fws_multimap_iterator operator+(int const n) const {
    return {map_, index_ + static_cast<index_t>(n)};
  }

  fws_multimap_iterator operator-(int const n) const {
    return {map_, index_ + static_cast<index_t>(n)};
  }

  int operator-(fws_multimap_iterator const& o) const {
    return index_ - o.index_;
  }

  value_type& operator[](int const n) const { return {map_, index_ + n}; }

  bool operator<(fws_multimap_iterator const& o) const {
    return index_ < o.index_;
  }

  bool operator>(fws_multimap_iterator const& o) const {
    return index_ > o.index_;
  }

  bool operator<=(fws_multimap_iterator const& o) const {
    return index_ <= o.index_;
  }

  bool operator>=(fws_multimap_iterator const& o) const {
    return index_ >= o.index_;
  }

  bool operator==(fws_multimap_iterator const& o) const {
    return &map_ == &o.map_ && index_ == o.index_;
  }

  bool operator!=(fws_multimap_iterator const& o) const {
    return !(*this == o);
  }

protected:
  MapType const& map_;
  index_t index_;
};

template <typename DataVec, typename IndexVec>
struct fws_multimap {
  using value_t = typename DataVec::value_type;
  using index_t = typename IndexVec::value_type;
  using entry_t = fws_multimap_entry<DataVec, IndexVec>;
  using iterator = fws_multimap_iterator<fws_multimap, entry_t>;

  template <typename T>
  struct is_unsigned_strong {
    static constexpr auto const value =
        std::is_unsigned_v<typename index_t::value_t>;
  };

  static_assert(
      std::disjunction_v<
          std::is_unsigned<index_t>,
          std::conjunction<is_strong<index_t>, is_unsigned_strong<index_t>>>,
      "index has to be unsigned");

  void push_back(value_t const& val) {
    assert(!complete_);
    data_.push_back(val);
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    data_.emplace_back(std::forward<Args>(args)...);
  }

  index_t current_key() const { return static_cast<index_t>(index_.size()); }

  void finish_key() {
    assert(!complete_);
    index_.push_back(current_start_);
    current_start_ = static_cast<index_t>(data_.size());
  }

  void finish_map() {
    assert(!complete_);
    index_.push_back(static_cast<index_t>(data_.size()));
    complete_ = true;
  }

  void reserve_index(index_t size) {
    index_.reserve(static_cast<std::size_t>(size) + 1);
  }

  entry_t operator[](index_t const index) const {
    assert(index < index_.size() - 1);
    return {data_, index_, index};
  }

  iterator begin() const { return {*this, 0}; }
  iterator end() const { return {*this, index_.size() - 1}; }

  iterator cbegin() const { return begin(); }
  iterator cend() const { return end(); }

  friend iterator begin(fws_multimap const& e) { return e.begin(); }
  friend iterator end(fws_multimap const& e) { return e.end(); }

  std::size_t index_size() const { return index_.size(); }
  std::size_t data_size() const { return data_.size(); }
  bool finished() const { return complete_; }

  DataVec data_;
  IndexVec index_;
  index_t current_start_{0};
  bool complete_{false};
};

namespace offset {

template <typename K, typename V>
using fws_multimap = fws_multimap<vector<V>, vector<K>>;

}  // namespace offset

namespace raw {

template <typename K, typename V>
using fws_multimap = fws_multimap<vector<V>, vector<K>>;

}  // namespace raw

}  // namespace cista

#include <functional>


#include <cinttypes>
#include <cstring>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <type_traits>


#include <cinttypes>
#include <string_view>

namespace cista {

#if defined(CISTA_XXH3)

// xxh3.h: basic_ios::clear: iostream error

using hash_t = XXH64_hash_t;

constexpr auto const BASE_HASH = 0ULL;

template <typename... Args>
constexpr hash_t hash_combine(hash_t h, Args... val) {
  auto xxh3 = [&](auto const& arg) {
    h = XXH3_64bits_withSeed(&arg, sizeof(arg), h);
  };
  ((xxh3(val)), ...);
  return h;
}

inline hash_t hash(std::string_view s, hash_t h = BASE_HASH) {
  return XXH3_64bits_withSeed(s.data(), s.size(), h);
}

template <std::size_t N>
constexpr hash_t hash(const char (&str)[N], hash_t const h = BASE_HASH) {
  return XXH3_64bits_withSeed(str, N - 1, h);
}

template <typename T>
constexpr std::uint64_t hash(T const& buf, hash_t const h = BASE_HASH) {
  return buf.size() == 0 ? h : XXH3_64bits_withSeed(&buf[0], buf.size(), h);
}

#elif defined(CISTA_WYHASH)

// wyhash.h: basic_ios::clear: iostream error

using hash_t = std::uint64_t;

constexpr auto const BASE_HASH = 34432ULL;

template <typename... Args>
constexpr hash_t hash_combine(hash_t h, Args... val) {
  auto wy = [&](auto const& arg) {
    h = wyhash::wyhash(&arg, sizeof(arg), h, wyhash::_wyp);
  };
  ((wy(val)), ...);
  return h;
}

inline hash_t hash(std::string_view s, hash_t h = BASE_HASH) {
  return wyhash::wyhash(s.data(), s.size(), h, wyhash::_wyp);
}

template <std::size_t N>
constexpr hash_t hash(const char (&str)[N],
                      hash_t const h = BASE_HASH) noexcept {
  return wyhash::wyhash(str, N - 1, h, wyhash::_wyp);
}

template <typename T>
constexpr std::uint64_t hash(T const& buf,
                             hash_t const h = BASE_HASH) noexcept {
  return buf.size() == 0 ? h
                         : wyhash::wyhash(&buf[0], buf.size(), h, wyhash::_wyp);
}

#elif defined(CISTA_WYHASH_FASTEST)


using hash_t = std::uint64_t;

constexpr auto const BASE_HASH = 123ULL;

template <typename... Args>
constexpr hash_t hash_combine(hash_t h, Args... val) {
  auto fh = [&](auto const& arg) {
    h = wyhash::FastestHash(&arg, sizeof(arg), h);
  };
  ((fh(val)), ...);
  return h;
}

inline hash_t hash(std::string_view s, hash_t h = BASE_HASH) {
  return wyhash::FastestHash(s.data(), s.size(), h);
}

template <std::size_t N>
constexpr hash_t hash(const char (&str)[N],
                      hash_t const h = BASE_HASH) noexcept {
  return wyhash::FastestHash(str, N - 1U, h);
}

template <typename T>
constexpr std::uint64_t hash(T const& buf,
                             hash_t const h = BASE_HASH) noexcept {
  return buf.size() == 0U ? h : wyhash::FastestHash(&buf[0U], buf.size(), h);
}

#else  // defined(CISTA_FNV1A)

// Algorithm: 64bit FNV-1a
// Source: http://www.isthe.com/chongo/tech/comp/fnv/

using hash_t = std::uint64_t;

constexpr auto const BASE_HASH = 14695981039346656037ULL;

template <typename... Args>
constexpr hash_t hash_combine(hash_t h, Args... val) noexcept {
  constexpr hash_t fnv_prime = 1099511628211ULL;
  auto fnv = [&](auto arg) noexcept {
    h = (h ^ static_cast<hash_t>(arg)) * fnv_prime;
  };
  ((fnv(val)), ...);
  return h;
}

constexpr hash_t hash(std::string_view s, hash_t h = BASE_HASH) noexcept {
  auto const ptr = s.data();
  for (std::size_t i = 0U; i < s.size(); ++i) {
    h = hash_combine(h, static_cast<std::uint8_t>(ptr[i]));
  }
  return h;
}

template <std::size_t N>
constexpr hash_t hash(const char (&str)[N],
                      hash_t const h = BASE_HASH) noexcept {
  return hash(std::string_view{str, N - 1U}, h);
}

template <typename T>
constexpr std::uint64_t hash(T const& buf,
                             hash_t const h = BASE_HASH) noexcept {
  return buf.size() == 0U
             ? h
             : hash(std::string_view{reinterpret_cast<char const*>(&buf[0U]),
                                     buf.size()},
                    h);
}

#endif

}  // namespace cista

namespace cista {

// This class is a generic hash-based container.
// It can be used e.g. as hash set or hash map.
//   - hash map: `T` = `std::pair<Key, Value>`, GetKey = `return entry.first;`
//   - hash set: `T` = `T`, GetKey = `return entry;` (identity)
//
// It is based on the idea of swiss tables:
// https://abseil.io/blog/20180927-swisstables
//
// Original implementation:
// https://github.com/abseil/abseil-cpp/blob/master/absl/container/internal/raw_hash_set.h
//
// Missing features of this implemenation compared to the original:
//   - SSE to speedup the lookup in the ctrl data structure
//   - sanitizer support (Sanitizer[Un]PoisonMemoryRegion)
//   - overloads (conveniance as well to reduce copying) in the interface
//   - allocator support
template <typename T, template <typename> typename Ptr, typename GetKey,
          typename GetValue, typename Hash, typename Eq>
struct hash_storage {
  using entry_t = T;
  using difference_type = ptrdiff_t;
  using size_type = hash_t;
  using key_type =
      decay_t<decltype(std::declval<GetKey>().operator()(std::declval<T>()))>;
  using mapped_type =
      decay_t<decltype(std::declval<GetValue>().operator()(std::declval<T>()))>;
  using group_t = std::uint64_t;
  using h2_t = std::uint8_t;
  static constexpr size_type const WIDTH = 8U;
  static constexpr std::size_t const ALIGNMENT = alignof(T);

  template <typename Key>
  hash_t compute_hash(Key const& k) {
    if constexpr (std::is_same_v<decay_t<Key>, key_type>) {
      return static_cast<size_type>(Hash{}(k));
    } else {
      return static_cast<size_type>(Hash::template create<Key>()(k));
    }
  }

  enum ctrl_t : int8_t {
    EMPTY = -128,  // 10000000
    DELETED = -2,  // 11111110
    END = -1  // 11111111
  };

  struct find_info {
    size_type offset_{}, probe_length_{};
  };

  struct probe_seq {
    constexpr probe_seq(size_type const hash, size_type const mask) noexcept
        : mask_{mask}, offset_{hash & mask_} {}
    size_type offset(size_type const i) const noexcept {
      return (offset_ + i) & mask_;
    }
    void next() noexcept {
      index_ += WIDTH;
      offset_ += index_;
      offset_ &= mask_;
    }
    size_type mask_, offset_, index_{0U};
  };

  struct bit_mask {
    static constexpr auto const SHIFT = 3U;

    constexpr explicit bit_mask(group_t const mask) noexcept : mask_{mask} {}

    bit_mask& operator++() noexcept {
      mask_ &= (mask_ - 1U);
      return *this;
    }

    size_type operator*() const noexcept { return trailing_zeros(); }

    explicit operator bool() const noexcept { return mask_ != 0U; }

    bit_mask begin() const noexcept { return *this; }
    bit_mask end() const noexcept { return bit_mask{0}; }

    size_type trailing_zeros() const noexcept {
      return ::cista::trailing_zeros(mask_) >> SHIFT;
    }

    size_type leading_zeros() const noexcept {
      constexpr int total_significant_bits = 8 << SHIFT;
      constexpr int extra_bits = sizeof(group_t) * 8 - total_significant_bits;
      return ::cista::leading_zeros(mask_ << extra_bits) >> SHIFT;
    }

    friend bool operator!=(bit_mask const& a, bit_mask const& b) noexcept {
      return a.mask_ != b.mask_;
    }

    group_t mask_;
  };

  struct group {
    static constexpr auto MSBS = 0x8080808080808080ULL;
    static constexpr auto LSBS = 0x0101010101010101ULL;
    static constexpr auto GAPS = 0x00FEFEFEFEFEFEFEULL;

    explicit group(ctrl_t const* pos) noexcept {
      std::memcpy(&ctrl_, pos, WIDTH);
#if defined(CISTA_BIG_ENDIAN)
      ctrl_ = endian_swap(ctrl_);
#endif
    }
    bit_mask match(h2_t const hash) const noexcept {
      auto const x = ctrl_ ^ (LSBS * hash);
      return bit_mask{(x - LSBS) & ~x & MSBS};
    }
    bit_mask match_empty() const noexcept {
      return bit_mask{(ctrl_ & (~ctrl_ << 6U)) & MSBS};
    }
    bit_mask match_empty_or_deleted() const noexcept {
      return bit_mask{(ctrl_ & (~ctrl_ << 7U)) & MSBS};
    }
    std::size_t count_leading_empty_or_deleted() const noexcept {
      return (trailing_zeros(((~ctrl_ & (ctrl_ >> 7U)) | GAPS) + 1U) + 7U) >>
             3U;
    }
    group_t ctrl_;
  };

  struct iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = hash_storage::entry_t;
    using reference = hash_storage::entry_t&;
    using pointer = hash_storage::entry_t*;
    using difference_type = ptrdiff_t;

    constexpr iterator() noexcept = default;

    reference operator*() const noexcept { return *entry_; }
    pointer operator->() const noexcept { return entry_; }
    iterator& operator++() noexcept {
      ++ctrl_;
      ++entry_;
      skip_empty_or_deleted();
      return *this;
    }
    iterator operator++(int) noexcept {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    friend bool operator==(iterator const& a, iterator const& b) noexcept {
      return a.ctrl_ == b.ctrl_;
    }
    friend bool operator!=(iterator const& a, iterator const& b) noexcept {
      return !(a == b);
    }

    constexpr iterator(ctrl_t* const ctrl) noexcept : ctrl_(ctrl) {}
    constexpr iterator(ctrl_t* const ctrl, T* const entry) noexcept
        : ctrl_(ctrl), entry_(entry) {}

    void skip_empty_or_deleted() noexcept {
      while (is_empty_or_deleted(*ctrl_)) {
        auto const shift = group{ctrl_}.count_leading_empty_or_deleted();
        ctrl_ += shift;
        entry_ += shift;
      }
    }

    ctrl_t* ctrl_{nullptr};
    T* entry_{nullptr};
  };

  struct const_iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = hash_storage::entry_t;
    using reference = hash_storage::entry_t const&;
    using pointer = hash_storage::entry_t const*;
    using difference_type = ptrdiff_t;

    constexpr const_iterator() noexcept = default;
    const_iterator(iterator i) noexcept : inner_(std::move(i)) {}

    reference operator*() const noexcept { return *inner_; }
    pointer operator->() const noexcept { return inner_.operator->(); }

    const_iterator& operator++() noexcept {
      ++inner_;
      return *this;
    }
    const_iterator operator++(int) noexcept { return inner_++; }

    friend bool operator==(const const_iterator& a,
                           const const_iterator& b) noexcept {
      return a.inner_ == b.inner_;
    }
    friend bool operator!=(const const_iterator& a,
                           const const_iterator& b) noexcept {
      return !(a == b);
    }

    const_iterator(ctrl_t const* ctrl, T const* entry) noexcept
        : inner_(const_cast<ctrl_t*>(ctrl), const_cast<T*>(entry)) {}

    iterator inner_;
  };

  static ctrl_t* empty_group() noexcept {
    alignas(16) static constexpr ctrl_t empty_group[] = {
        END,   EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY};
    return const_cast<ctrl_t*>(empty_group);
  }

  static constexpr bool is_empty(ctrl_t const c) noexcept { return c == EMPTY; }
  static constexpr bool is_full(ctrl_t const c) noexcept { return c >= 0; }
  static constexpr bool is_deleted(ctrl_t const c) noexcept {
    return c == DELETED;
  }
  static constexpr bool is_empty_or_deleted(ctrl_t const c) noexcept {
    return c < END;
  }

  static constexpr std::size_t normalize_capacity(size_type const n) noexcept {
    return n == 0U ? 1U : ~size_type{} >> leading_zeros(n);
  }

  static constexpr size_type h1(size_type const hash) noexcept {
    return (hash >> 7U) ^ 16777619U;
  }

  static constexpr h2_t h2(size_type const hash) noexcept {
    return hash & 0x7FU;
  }

  static constexpr size_type capacity_to_growth(
      size_type const capacity) noexcept {
    return (capacity == 7U) ? 6U : capacity - (capacity / 8U);
  }

  constexpr hash_storage() = default;

  hash_storage(std::initializer_list<T> init) {
    insert(init.begin(), init.end());
  }

  hash_storage(hash_storage&& other) noexcept
      : entries_{other.entries_},
        ctrl_{other.ctrl_},
        size_{other.size_},
        capacity_{other.capacity_},
        growth_left_{other.growth_left_},
        self_allocated_{other.self_allocated_} {
    other.reset();
  }

  hash_storage(hash_storage const& other) {
    if (other.size() != 0U) {
      for (const auto& v : other) {
        emplace(v);
      }
    }
  }

  hash_storage& operator=(hash_storage&& other) noexcept {
    entries_ = other.entries_;
    ctrl_ = other.ctrl_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    growth_left_ = other.growth_left_;
    self_allocated_ = other.self_allocated_;
    other.reset();
    return *this;
  }

  hash_storage& operator=(hash_storage const& other) {
    clear();
    if (other.size() == 0U) {
      return *this;
    }
    for (const auto& v : other) {
      emplace(v);
    }
    return *this;
  }

  ~hash_storage() { clear(); }

  void set_empty_key(key_type const&) noexcept {}
  void set_deleted_key(key_type const&) noexcept {}

  // --- operator[]
  template <typename Key>
  mapped_type& bracket_operator_impl(Key&& key) {
    auto const res = find_or_prepare_insert(std::forward<Key>(key));
    if (res.second) {
      new (entries_ + res.first) T{static_cast<key_type>(key), mapped_type{}};
    }
    return GetValue{}(entries_[res.first]);
  }

  template <typename Key>
  mapped_type& operator[](Key&& key) {
    return bracket_operator_impl(std::forward<Key>(key));
  }

  mapped_type& operator[](key_type const& key) {
    return bracket_operator_impl(key);
  }

  // --- get()
  template <typename Key>
  std::optional<mapped_type> get_impl(Key&& key) {
    if (auto it = find(std::forward<Key>(key)); it != end()) {
      return GetValue{}(*it);
    } else {
      return std::nullopt;
    }
  }

  template <typename Key>
  std::optional<mapped_type> get(Key&& key) const {
    return const_cast<hash_storage*>(this)->get_impl(std::forward<Key>(key));
  }

  // --- at()
  template <typename Key>
  mapped_type& at_impl(Key&& key) {
    auto const it = find(std::forward<Key>(key));
    if (it == end()) {
      throw_exception(std::out_of_range{"hash_storage::at() key not found"});
    }
    return GetValue{}(*it);
  }

  mapped_type& at(key_type const& key) { return at_impl(key); }

  mapped_type const& at(key_type const& key) const {
    return const_cast<hash_storage*>(this)->at(key);
  }

  template <typename Key>
  mapped_type& at(Key&& key) {
    return at_impl(std::forward<Key>(key));
  }

  template <typename Key>
  mapped_type const& at(Key&& key) const {
    return const_cast<hash_storage*>(this)->at(std::forward<Key>(key));
  }

  // --- find()
  template <typename Key>
  iterator find_impl(Key&& key) {
    auto const hash = compute_hash(key);
    for (auto seq = probe_seq{h1(hash), capacity_}; true; seq.next()) {
      group g{ctrl_ + seq.offset_};
      for (auto const i : g.match(h2(hash))) {
        if (Eq{}(GetKey()(entries_[seq.offset(i)]), key)) {
          return iterator_at(seq.offset(i));
        }
      }
      if (g.match_empty()) {
        return end();
      }
    }
  }

  template <typename Key>
  const_iterator find(Key&& key) const {
    return const_cast<hash_storage*>(this)->find_impl(std::forward<Key>(key));
  }

  template <typename Key>
  iterator find(Key&& key) {
    return find_impl(std::forward<Key>(key));
  }

  const_iterator find(key_type const& key) const noexcept {
    return const_cast<hash_storage*>(this)->find_impl(key);
  }

  iterator find(key_type const& key) noexcept { return find_impl(key); }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) {
      emplace(*first);
    }
  }

  // --- erase()
  template <typename Key>
  std::size_t erase_impl(Key&& key) {
    auto it = find(std::forward<Key>(key));
    if (it == end()) {
      return 0U;
    }
    erase(it);
    return 1U;
  }

  std::size_t erase(key_type const& k) { return erase_impl(k); }

  template <typename Key>
  std::size_t erase(Key&& key) {
    return erase_impl(std::forward<Key>(key));
  }

  void erase(iterator const it) noexcept {
    it.entry_->~T();
    erase_meta_only(it);
  }

  std::pair<iterator, bool> insert(T const& entry) { return emplace(entry); }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto entry = T{std::forward<Args>(args)...};
    auto res = find_or_prepare_insert(GetKey()(entry));
    if (res.second) {
      new (entries_ + res.first) T{std::move(entry)};
    }
    return {iterator_at(res.first), res.second};
  }

  iterator begin() noexcept {
    auto it = iterator_at(0U);
    if (ctrl_ != nullptr) {
      it.skip_empty_or_deleted();
    }
    return it;
  }
  iterator end() noexcept { return {ctrl_ + capacity_}; }

  const_iterator begin() const noexcept {
    return const_cast<hash_storage*>(this)->begin();
  }
  const_iterator end() const noexcept {
    return const_cast<hash_storage*>(this)->end();
  }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return end(); }

  friend iterator begin(hash_storage& h) noexcept { return h.begin(); }
  friend const_iterator begin(hash_storage const& h) noexcept {
    return h.begin();
  }
  friend const_iterator cbegin(hash_storage const& h) noexcept {
    return h.begin();
  }
  friend iterator end(hash_storage& h) noexcept { return h.end(); }
  friend const_iterator end(hash_storage const& h) noexcept { return h.end(); }
  friend const_iterator cend(hash_storage const& h) noexcept { return h.end(); }

  bool empty() const noexcept { return size() == 0U; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }
  size_type max_size() const noexcept {
    return std::numeric_limits<std::size_t>::max();
  }

  bool is_free(int index) const noexcept {
    auto const index_before = (index - WIDTH) & capacity_;

    auto const empty_after = group{ctrl_ + index}.match_empty();
    auto const empty_before = group{ctrl_ + index_before}.match_empty();

    return empty_before && empty_after &&
           (empty_after.trailing_zeros() + empty_before.leading_zeros()) <
               WIDTH;
  }

  bool was_never_full(std::size_t const index) const noexcept {
    auto const index_before = (index - WIDTH) & capacity_;

    auto const empty_after = group{ctrl_ + index}.match_empty();
    auto const empty_before = group{ctrl_ + index_before}.match_empty();

    return empty_before && empty_after &&
           (empty_after.trailing_zeros() + empty_before.leading_zeros()) <
               WIDTH;
  }

  void erase_meta_only(const_iterator it) noexcept {
    --size_;
    auto const index = static_cast<std::size_t>(it.inner_.ctrl_ - ctrl_);
    auto const wnf = was_never_full(index);
    set_ctrl(index, static_cast<h2_t>(wnf ? EMPTY : DELETED));
    growth_left_ += wnf;
  }

  void clear() {
    if (capacity_ == 0U) {
      return;
    }

    for (size_type i = 0U; i != capacity_; ++i) {
      if (is_full(ctrl_[i])) {
        entries_[i].~T();
      }
    }

    if (self_allocated_) {
      CISTA_ALIGNED_FREE(ALIGNMENT, entries_);
    }

    partial_reset();
  }

  template <typename Key>
  std::pair<size_type, bool> find_or_prepare_insert(Key&& key) {
    auto const hash = compute_hash(key);
    for (auto seq = probe_seq{h1(hash), capacity_}; true; seq.next()) {
      group g{ctrl_ + seq.offset_};
      for (auto const i : g.match(h2(hash))) {
        if (Eq{}(GetKey()(entries_[seq.offset(i)]), key)) {
          return {seq.offset(i), false};
        }
      }
      if (g.match_empty()) {
        break;
      }
    }
    return {prepare_insert(hash), true};
  }

  find_info find_first_non_full(size_type const hash) const noexcept {
    for (auto seq = probe_seq{h1(hash), capacity_}; true; seq.next()) {
      auto const mask = group{ctrl_ + seq.offset_}.match_empty_or_deleted();
      if (mask) {
        return {seq.offset(*mask), seq.index_};
      }
    }
  }

  size_type prepare_insert(size_type const hash) {
    auto target = find_first_non_full(hash);
    if (growth_left_ == 0U && !is_deleted(ctrl_[target.offset_])) {
      rehash_and_grow_if_necessary();
      target = find_first_non_full(hash);
    }
    ++size_;
    growth_left_ -= (is_empty(ctrl_[target.offset_]) ? 1U : 0U);
    set_ctrl(target.offset_, h2(hash));
    return target.offset_;
  }

  void set_ctrl(size_type const i, h2_t const c) noexcept {
    ctrl_[i] = static_cast<ctrl_t>(c);
    ctrl_[((i - WIDTH) & capacity_) + 1U + ((WIDTH - 1U) & capacity_)] =
        static_cast<ctrl_t>(c);
  }

  void rehash_and_grow_if_necessary() {
    resize(capacity_ == 0U ? 1U : capacity_ * 2U + 1U);
  }

  void reset_growth_left() noexcept {
    growth_left_ = capacity_to_growth(capacity_) - size_;
  }

  void reset_ctrl() noexcept {
    std::memset(ctrl_, EMPTY, static_cast<std::size_t>(capacity_ + WIDTH + 1U));
    ctrl_[capacity_] = END;
  }

  void initialize_entries() {
    self_allocated_ = true;
    auto const size = static_cast<size_type>(
        capacity_ * sizeof(T) + (capacity_ + 1U + WIDTH) * sizeof(ctrl_t));
    entries_ = reinterpret_cast<T*>(
        CISTA_ALIGNED_ALLOC(ALIGNMENT, static_cast<std::size_t>(size)));
    if (entries_ == nullptr) {
      throw_exception(std::bad_alloc{});
    }
#if defined(CISTA_ZERO_OUT)
    std::memset(entries_, 0, size);
#endif
    ctrl_ = reinterpret_cast<ctrl_t*>(
        reinterpret_cast<std::uint8_t*>(ptr_cast(entries_)) +
        capacity_ * sizeof(T));
    reset_ctrl();
    reset_growth_left();
  }

  void resize(size_type const new_capacity) {
    auto const old_ctrl = ctrl_;
    auto const old_entries = entries_;
    auto const old_capacity = capacity_;
    auto const old_self_allocated = self_allocated_;

    capacity_ = new_capacity;
    initialize_entries();

    for (size_type i = 0U; i != old_capacity; ++i) {
      if (is_full(old_ctrl[i])) {
        auto const hash = compute_hash(GetKey()(old_entries[i]));
        auto const target = find_first_non_full(hash);
        auto const new_index = target.offset_;
        set_ctrl(new_index, h2(hash));
        new (entries_ + new_index) T{std::move(old_entries[i])};
        old_entries[i].~T();
      }
    }

    if (old_capacity != 0U && old_self_allocated) {
      CISTA_ALIGNED_FREE(ALIGNMENT, old_entries);
    }
  }

  void partial_reset() noexcept {
    entries_ = nullptr;
    ctrl_ = empty_group();
    size_ = 0U;
    capacity_ = 0U;
    growth_left_ = 0U;
  }

  void reset() noexcept {
    partial_reset();
    self_allocated_ = false;
  }

  void rehash() { resize(capacity_); }

  iterator iterator_at(size_type const i) noexcept {
    return {ctrl_ + i, entries_ + i};
  }
  const_iterator iterator_at(size_type const i) const noexcept {
    return {ctrl_ + i, entries_ + i};
  }

  bool operator==(hash_storage const& b) const noexcept {
    if (size() != b.size()) {
      return false;
    }
    for (auto const& el : *this) {
      if (b.find(GetKey()(el)) == b.end()) {
        return false;
      }
    }
    return true;
  }

  Ptr<T> entries_{nullptr};
  Ptr<ctrl_t> ctrl_{empty_group()};
  size_type size_{0U}, capacity_{0U}, growth_left_{0U};
  bool self_allocated_{false};
};

}  // namespace cista

#include <utility>




#include <functional>
#include <tuple>


#include <type_traits>


// Credits: Implementation by Anatoliy V. Tomilov (@tomilov),
//          based on gist by Rafal T. Janik (@ChemiaAion)
//
// Resources:
// https://playfulprogramming.blogspot.com/2016/12/serializing-structs-with-c17-structured.html
// https://codereview.stackexchange.com/questions/142804/get-n-th-data-member-of-a-struct
// https://stackoverflow.com/questions/39768517/structured-bindings-width
// https://stackoverflow.com/questions/35463646/arity-of-aggregate-in-logarithmic-time
// https://stackoverflow.com/questions/38393302/returning-variadic-aggregates-struct-and-syntax-for-c17-variadic-template-c

namespace cista {

namespace detail {

struct instance {
  template <typename Type>
  operator Type() const;
};

template <typename Aggregate, typename IndexSequence = std::index_sequence<>,
          typename = void>
struct arity_impl : IndexSequence {};

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

template <typename Aggregate, std::size_t... Indices>
struct arity_impl<Aggregate, std::index_sequence<Indices...>,
                  std::void_t<decltype(Aggregate{
                      (static_cast<void>(Indices), std::declval<instance>())...,
                      std::declval<instance>()})>>
    : arity_impl<Aggregate,
                 std::index_sequence<Indices..., sizeof...(Indices)>> {};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

}  // namespace detail

template <typename T>
constexpr std::size_t arity() noexcept {
  return detail::arity_impl<decay_t<T>>().size();
}

}  // namespace cista

namespace cista {

namespace detail {

template <typename T, typename = void>
struct has_cista_members : std::false_type {};

template <typename T>
struct has_cista_members<
    T, std::void_t<decltype(std::declval<std::decay_t<T>>().cista_members())>>
    : std::true_type {};

template <typename T>
inline constexpr auto const has_cista_members_v = has_cista_members<T>::value;

template <typename... Ts, std::size_t... I>
constexpr auto add_const_helper(std::tuple<Ts...>&& t,
                                std::index_sequence<I...>) {
  return std::make_tuple(std::cref(std::get<I>(t))...);
}

template <typename T>
constexpr auto add_const(T&& t) {
  return add_const_helper(
      std::forward<T>(t),
      std::make_index_sequence<std::tuple_size_v<std::decay_t<decltype(t)>>>());
}

template <typename... Ts, std::size_t... I>
auto to_ptrs_helper(std::tuple<Ts...>&& t, std::index_sequence<I...>) {
  return std::make_tuple(&std::get<I>(t)...);
}

template <typename T>
auto to_ptrs(T&& t) {
  return to_ptrs_helper(
      std::forward<T>(t),
      std::make_index_sequence<std::tuple_size_v<std::decay_t<T>>>());
}

}  // namespace detail

template <typename T>
inline constexpr auto to_tuple_works_v =
    detail::has_cista_members_v<T> ||
    (std::is_aggregate_v<T> &&
#if !defined(_MSC_VER) || defined(NDEBUG)
     std::is_standard_layout_v<T> &&
#endif
     !std::is_polymorphic_v<T> && !std::is_union_v<T>);

template <typename T,
          std::enable_if_t<detail::has_cista_members_v<T> && std::is_const_v<T>,
                           void*> = nullptr>
constexpr auto to_tuple(T& t) {
  return detail::add_const(
      const_cast<std::add_lvalue_reference_t<std::remove_const_t<T>>>(t)
          .cista_members());
}

template <typename T, std::enable_if_t<detail::has_cista_members_v<T> &&
                                           !std::is_const_v<T>,
                                       void*> = nullptr>
constexpr auto to_tuple(T&& t) {
  return t.cista_members();
}

template <typename T,
          std::enable_if_t<!detail::has_cista_members_v<T>, void*> = nullptr>
auto to_tuple(T& t) {
  constexpr auto const a = arity<T>();
  static_assert(a <= 64U, "Max. supported members: 64");
  if constexpr (a == 0U) {
    return std::tie();
  } else if constexpr (a == 1U) {
    auto& [p1] = t;
    return std::tie(p1);
  } else if constexpr (a == 2U) {
    auto& [p1, p2] = t;
    return std::tie(p1, p2);
  } else if constexpr (a == 3U) {
    auto& [p1, p2, p3] = t;
    return std::tie(p1, p2, p3);
  } else if constexpr (a == 4U) {
    auto& [p1, p2, p3, p4] = t;
    return std::tie(p1, p2, p3, p4);
  } else if constexpr (a == 5U) {
    auto& [p1, p2, p3, p4, p5] = t;
    return std::tie(p1, p2, p3, p4, p5);
  } else if constexpr (a == 6U) {
    auto& [p1, p2, p3, p4, p5, p6] = t;
    return std::tie(p1, p2, p3, p4, p5, p6);
  } else if constexpr (a == 7U) {
    auto& [p1, p2, p3, p4, p5, p6, p7] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7);
  } else if constexpr (a == 8U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8);
  } else if constexpr (a == 9U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9);
  } else if constexpr (a == 10U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
  } else if constexpr (a == 11U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);
  } else if constexpr (a == 12U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);
  } else if constexpr (a == 13U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);
  } else if constexpr (a == 14U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,
                    p14);
  } else if constexpr (a == 15U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15] =
        t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15);
  } else if constexpr (a == 16U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16);
  } else if constexpr (a == 17U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17);
  } else if constexpr (a == 18U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18);
  } else if constexpr (a == 19U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19);
  } else if constexpr (a == 20U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20);
  } else if constexpr (a == 21U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21);
  } else if constexpr (a == 22U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22);
  } else if constexpr (a == 23U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23);
  } else if constexpr (a == 24U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24);
  } else if constexpr (a == 25U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25);
  } else if constexpr (a == 26U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26);
  } else if constexpr (a == 27U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27);
  } else if constexpr (a == 28U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28);
  } else if constexpr (a == 29U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28,
           p29] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29);
  } else if constexpr (a == 30U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30);
  } else if constexpr (a == 31U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31);
  } else if constexpr (a == 32U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32);
  } else if constexpr (a == 33U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33);
  } else if constexpr (a == 34U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34);
  } else if constexpr (a == 35U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35);
  } else if constexpr (a == 36U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36);
  } else if constexpr (a == 37U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37);
  } else if constexpr (a == 38U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38);
  } else if constexpr (a == 39U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39);
  } else if constexpr (a == 40U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40);
  } else if constexpr (a == 41U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41);
  } else if constexpr (a == 42U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42);
  } else if constexpr (a == 43U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42,
           p43] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43);
  } else if constexpr (a == 44U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44);
  } else if constexpr (a == 45U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45);
  } else if constexpr (a == 46U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46);
  } else if constexpr (a == 47U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47);
  } else if constexpr (a == 48U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48);
  } else if constexpr (a == 49U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49);
  } else if constexpr (a == 50U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50);
  } else if constexpr (a == 51U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51);
  } else if constexpr (a == 52U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52);
  } else if constexpr (a == 53U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53);
  } else if constexpr (a == 54U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54);
  } else if constexpr (a == 55U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55);
  } else if constexpr (a == 56U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56);
  } else if constexpr (a == 57U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56,
           p57] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57);
  } else if constexpr (a == 58U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58);
  } else if constexpr (a == 59U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58, p59] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58, p59);
  } else if constexpr (a == 60U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58, p59, p60] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58, p59, p60);
  } else if constexpr (a == 61U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58, p59, p60, p61] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61);
  } else if constexpr (a == 62U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58, p59, p60, p61, p62] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62);
  } else if constexpr (a == 63U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58, p59, p60, p61, p62, p63] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62,
                    p63);
  } else if constexpr (a == 64U) {
    auto& [p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15,
           p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29,
           p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43,
           p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57,
           p58, p59, p60, p61, p62, p63, p64] = t;
    return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14,
                    p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26,
                    p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38,
                    p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50,
                    p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62,
                    p63, p64);
  }
}

template <typename T>
auto to_ptr_tuple(T&& t) {
  return detail::to_ptrs(to_tuple(std::forward<T>(t)));
}
}  // namespace cista

#define CISTA_COMPARABLE()                               \
  template <typename T>                                  \
  bool operator==(T&& b) const {                         \
    return cista::to_tuple(*this) == cista::to_tuple(b); \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator!=(T&& b) const {                         \
    return cista::to_tuple(*this) != cista::to_tuple(b); \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator<(T&& b) const {                          \
    return cista::to_tuple(*this) < cista::to_tuple(b);  \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator<=(T&& b) const {                         \
    return cista::to_tuple(*this) <= cista::to_tuple(b); \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator>(T&& b) const {                          \
    return cista::to_tuple(*this) > cista::to_tuple(b);  \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator>=(T&& b) const {                         \
    return cista::to_tuple(*this) >= cista::to_tuple(b); \
  }

#define CISTA_FRIEND_COMPARABLE(class_name)                          \
  friend bool operator==(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) == cista::to_tuple(b);                 \
  }                                                                  \
                                                                     \
  friend bool operator!=(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) != cista::to_tuple(b);                 \
  }                                                                  \
                                                                     \
  friend bool operator<(class_name const& a, class_name const& b) {  \
    return cista::to_tuple(a) < cista::to_tuple(b);                  \
  }                                                                  \
                                                                     \
  friend bool operator<=(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) <= cista::to_tuple(b);                 \
  }                                                                  \
                                                                     \
  friend bool operator>(class_name const& a, class_name const& b) {  \
    return cista::to_tuple(a) > cista::to_tuple(b);                  \
  }                                                                  \
                                                                     \
  friend bool operator>=(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) >= cista::to_tuple(b);                 \
  }

namespace cista {

template <typename T1, typename T2>
struct pair {
  CISTA_COMPARABLE()
  using first_type = T1;
  using second_type = T2;
  auto cista_members() { return std::tie(first, second); }
  T1 first{};
  T2 second{};
};

template <typename T1, typename T2>
pair(T1&&, T2&&) -> pair<decay_t<T1>, decay_t<T2>>;

namespace raw {
using cista::pair;
}  // namespace raw

namespace offset {
using cista::pair;
}  // namespace offset

}  // namespace cista

#include <algorithm>
#include <type_traits>


namespace cista {

namespace detail {

template <class F, class Tuple, std::size_t... I>
constexpr bool tuple_equal_impl(F&& is_equal, Tuple&& a, Tuple&& b,
                                std::index_sequence<I...>) {
  return (is_equal(std::get<I>(std::forward<Tuple>(a)),
                   std::get<I>(std::forward<Tuple>(b))) &&
          ...);
}

}  // namespace detail

template <class F, class Tuple>
constexpr decltype(auto) tuple_equal(F&& is_equal, Tuple&& a, Tuple&& b) {
  return detail::tuple_equal_impl(
      std::forward<F>(is_equal), std::forward<Tuple>(a), std::forward<Tuple>(b),
      std::make_index_sequence<
          std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
}

template <typename A, typename B, typename = void>
struct is_eq_comparable : std::false_type {};

template <typename A, typename B>
struct is_eq_comparable<
    A, B, std::void_t<decltype(std::declval<A>() == std::declval<B>())>>
    : std::true_type {};

template <typename A, typename B>
constexpr bool is_eq_comparable_v = is_eq_comparable<A, B>::value;

template <typename T>
struct equal_to;

template <typename T>
struct equal_to {
  template <typename T1>
  constexpr bool operator()(T const& a, T1 const& b) const {
    using Type = decay_t<T>;
    using Type1 = decay_t<T1>;
    if constexpr (is_iterable_v<Type> && is_iterable_v<Type1>) {
      using std::begin;
      using std::end;
      auto const eq = std::equal(
          begin(a), end(a), begin(b), end(b),
          [](auto&& x, auto&& y) { return equal_to<decltype(x)>{}(x, y); });
      return eq;
    } else if constexpr (to_tuple_works_v<Type> && to_tuple_works_v<Type1>) {
      return tuple_equal(
          [](auto&& x, auto&& y) { return equal_to<decltype(x)>{}(x, y); },
          to_tuple(a), to_tuple(b));
    } else if constexpr (is_eq_comparable_v<Type, Type1>) {
      return a == b;
    } else {
      static_assert(is_iterable_v<Type> || is_eq_comparable_v<Type, Type1> ||
                        to_tuple_works_v<Type>,
                    "Implement custom equality");
    }
    return false;
  }
};

template <typename A, typename B>
struct equal_to<pair<A, B>> {
  template <typename T1>
  constexpr bool operator()(pair<A, B> const& a, T1 const& b) const {
    return a.first == b.first && a.second == b.second;
  }
};

}  // namespace cista

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>


#include <cinttypes>
#include <cstring>

#include <ostream>
#include <string>
#include <string_view>


namespace cista {

template <typename Ptr = char const*>
struct generic_string {
  using msize_t = std::uint32_t;
  using value_type = char;

  static msize_t mstrlen(char const* s) noexcept {
    return static_cast<msize_t>(std::strlen(s));
  }

  static constexpr struct owning_t {
  } owning{};
  static constexpr struct non_owning_t {
  } non_owning{};

  constexpr generic_string() noexcept {}
  ~generic_string() noexcept { reset(); }

  generic_string(std::string_view s, owning_t const) { set_owning(s); }
  generic_string(std::string_view s, non_owning_t const) { set_non_owning(s); }
  generic_string(std::string const& s, owning_t const) { set_owning(s); }
  generic_string(std::string const& s, non_owning_t const) {
    set_non_owning(s);
  }
  generic_string(char const* s, owning_t const) { set_owning(s, mstrlen(s)); }
  generic_string(char const* s, non_owning_t const) { set_non_owning(s); }
  generic_string(char const* s, msize_t const len, owning_t const) {
    set_owning(s, len);
  }
  generic_string(char const* s, msize_t const len, non_owning_t const) {
    set_non_owning(s, len);
  }

  char* begin() noexcept { return data(); }
  char* end() noexcept { return data() + size(); }
  char const* begin() const noexcept { return data(); }
  char const* end() const noexcept { return data() + size(); }

  friend char const* begin(generic_string const& s) { return s.begin(); }
  friend char* begin(generic_string& s) { return s.begin(); }
  friend char const* end(generic_string const& s) { return s.end(); }
  friend char* end(generic_string& s) { return s.end(); }

  bool is_short() const noexcept { return s_.is_short_; }

  void reset() noexcept {
    if (!h_.is_short_ && h_.ptr_ != nullptr && h_.self_allocated_) {
      std::free(data());
    }
    h_ = heap{};
  }

  void set_owning(std::string const& s) {
    set_owning(s.data(), static_cast<msize_t>(s.size()));
  }

  void set_owning(std::string_view s) {
    set_owning(s.data(), static_cast<msize_t>(s.size()));
  }

  void set_owning(char const* str) { set_owning(str, mstrlen(str)); }

  static constexpr msize_t short_length_limit = 15U;

  void set_owning(char const* str, msize_t const len) {
    reset();
    if (str == nullptr || len == 0U) {
      return;
    }
    s_.is_short_ = (len <= short_length_limit);
    if (s_.is_short_) {
      std::memcpy(s_.s_, str, len);
      for (auto i = len; i < short_length_limit; ++i) {
        s_.s_[i] = 0;
      }
    } else {
      h_.ptr_ = static_cast<char*>(std::malloc(len));
      if (h_.ptr_ == nullptr) {
        throw_exception(std::bad_alloc{});
      }
      h_.size_ = len;
      h_.self_allocated_ = true;
      std::memcpy(data(), str, len);
    }
  }

  void set_non_owning(std::string const& v) {
    set_non_owning(v.data(), static_cast<msize_t>(v.size()));
  }

  void set_non_owning(std::string_view v) {
    set_non_owning(v.data(), static_cast<msize_t>(v.size()));
  }

  void set_non_owning(char const* str) { set_non_owning(str, mstrlen(str)); }

  void set_non_owning(char const* str, msize_t const len) {
    reset();
    if (str == nullptr || len == 0U) {
      return;
    }

    if (len <= short_length_limit) {
      return set_owning(str, len);
    }

    h_.is_short_ = false;
    h_.self_allocated_ = false;
    h_.ptr_ = str;
    h_.size_ = len;
  }

  void move_from(generic_string&& s) noexcept {
    reset();
    std::memcpy(static_cast<void*>(this), &s, sizeof(*this));
    if constexpr (std::is_pointer_v<Ptr>) {
      std::memset(static_cast<void*>(&s), 0, sizeof(*this));
    } else {
      if (!s.is_short()) {
        h_.ptr_ = s.h_.ptr_;
        s.h_.ptr_ = nullptr;
        s.h_.size_ = 0U;
      }
    }
  }

  void copy_from(generic_string const& s) {
    reset();
    if (s.is_short()) {
      std::memcpy(static_cast<void*>(this), &s, sizeof(s));
    } else if (s.h_.self_allocated_) {
      set_owning(s.data(), s.size());
    } else {
      set_non_owning(s.data(), s.size());
    }
  }

  bool empty() const noexcept { return size() == 0U; }
  std::string_view view() const noexcept { return {data(), size()}; }
  std::string str() const { return {data(), size()}; }

  operator std::string_view() const { return view(); }

  char& operator[](std::size_t const i) noexcept { return data()[i]; }
  char const& operator[](std::size_t const i) const noexcept {
    return data()[i];
  }

  friend std::ostream& operator<<(std::ostream& out, generic_string const& s) {
    return out << s.view();
  }

  friend bool operator==(generic_string const& a,
                         generic_string const& b) noexcept {
    return a.view() == b.view();
  }

  friend bool operator!=(generic_string const& a,
                         generic_string const& b) noexcept {
    return a.view() != b.view();
  }

  friend bool operator<(generic_string const& a,
                        generic_string const& b) noexcept {
    return a.view() < b.view();
  }

  friend bool operator>(generic_string const& a,
                        generic_string const& b) noexcept {
    return a.view() > b.view();
  }

  friend bool operator<=(generic_string const& a,
                         generic_string const& b) noexcept {
    return a.view() <= b.view();
  }

  friend bool operator>=(generic_string const& a,
                         generic_string const& b) noexcept {
    return a.view() >= b.view();
  }

  friend bool operator==(generic_string const& a, std::string_view b) noexcept {
    return a.view() == b;
  }

  friend bool operator!=(generic_string const& a, std::string_view b) noexcept {
    return a.view() != b;
  }

  friend bool operator<(generic_string const& a, std::string_view b) noexcept {
    return a.view() < b;
  }

  friend bool operator>(generic_string const& a, std::string_view b) noexcept {
    return a.view() > b;
  }

  friend bool operator<=(generic_string const& a, std::string_view b) noexcept {
    return a.view() <= b;
  }

  friend bool operator>=(generic_string const& a, std::string_view b) noexcept {
    return a.view() >= b;
  }

  friend bool operator==(std::string_view a, generic_string const& b) noexcept {
    return a == b.view();
  }

  friend bool operator!=(std::string_view a, generic_string const& b) noexcept {
    return a != b.view();
  }

  friend bool operator<(std::string_view a, generic_string const& b) noexcept {
    return a < b.view();
  }

  friend bool operator>(std::string_view a, generic_string const& b) noexcept {
    return a > b.view();
  }

  friend bool operator<=(std::string_view a, generic_string const& b) noexcept {
    return a <= b.view();
  }

  friend bool operator>=(std::string_view a, generic_string const& b) noexcept {
    return a >= b.view();
  }

  friend bool operator==(generic_string const& a, char const* b) noexcept {
    return a.view() == std::string_view{b};
  }

  friend bool operator!=(generic_string const& a, char const* b) noexcept {
    return a.view() != std::string_view{b};
  }

  friend bool operator<(generic_string const& a, char const* b) noexcept {
    return a.view() < std::string_view{b};
  }

  friend bool operator>(generic_string const& a, char const* b) noexcept {
    return a.view() > std::string_view{b};
  }

  friend bool operator<=(generic_string const& a, char const* b) noexcept {
    return a.view() <= std::string_view{b};
  }

  friend bool operator>=(generic_string const& a, char const* b) noexcept {
    return a.view() >= std::string_view{b};
  }

  friend bool operator==(char const* a, generic_string const& b) noexcept {
    return std::string_view{a} == b.view();
  }

  friend bool operator!=(char const* a, generic_string const& b) noexcept {
    return std::string_view{a} != b.view();
  }

  friend bool operator<(char const* a, generic_string const& b) noexcept {
    return std::string_view{a} < b.view();
  }

  friend bool operator>(char const* a, generic_string const& b) noexcept {
    return std::string_view{a} > b.view();
  }

  friend bool operator<=(char const* a, generic_string const& b) noexcept {
    return std::string_view{a} <= b.view();
  }

  friend bool operator>=(char const* a, generic_string const& b) noexcept {
    return std::string_view{a} >= b.view();
  }

  char const* internal_data() const noexcept {
    if constexpr (std::is_pointer_v<Ptr>) {
      return is_short() ? s_.s_ : h_.ptr_;
    } else {
      return is_short() ? s_.s_ : h_.ptr_.get();
    }
  }

  char* data() noexcept { return const_cast<char*>(internal_data()); }
  char const* data() const noexcept { return internal_data(); }

  msize_t size() const noexcept {
    if (is_short()) {
      auto const pos =
          static_cast<char const*>(std::memchr(s_.s_, 0, short_length_limit));
      return (pos != nullptr) ? static_cast<msize_t>(pos - s_.s_)
                              : short_length_limit;
    }
    return h_.size_;
  }

  struct heap {
    bool is_short_{false};
    bool self_allocated_{false};
    std::uint16_t __fill__{0};
    std::uint32_t size_{0};
    Ptr ptr_{nullptr};
  };

  struct stack {
    bool is_short_{true};
    char s_[short_length_limit]{0};
  };

  union {
    heap h_{};
    stack s_;
  };
};

template <typename Ptr>
struct basic_string : public generic_string<Ptr> {
  using base = generic_string<Ptr>;

  using base::base;
  using base::operator std::string_view;

  friend std::ostream& operator<<(std::ostream& out, basic_string const& s) {
    return out << s.view();
  }

  explicit operator std::string() const { return {base::data(), base::size()}; }

  basic_string(std::string_view s) : base{s, base::owning} {}
  basic_string(std::string const& s) : base{s, base::owning} {}
  basic_string(char const* s) : base{s, base::owning} {}
  basic_string(char const* s, typename base::msize_t const len)
      : base{s, len, base::owning} {}

  basic_string(basic_string const& o) : base{o.view(), base::owning} {}
  basic_string(basic_string&& o) { base::move_from(std::move(o)); }

  basic_string& operator=(basic_string const& o) {
    base::set_owning(o.data(), o.size());
    return *this;
  }

  basic_string& operator=(basic_string&& o) {
    base::move_from(std::move(o));
    return *this;
  }

  basic_string& operator=(char const* s) {
    base::set_owning(s);
    return *this;
  }
  basic_string& operator=(std::string const& s) {
    base::set_owning(s);
    return *this;
  }
  basic_string& operator=(std::string_view s) {
    base::set_owning(s);
    return *this;
  }
};

template <typename Ptr>
struct basic_string_view : public generic_string<Ptr> {
  using base = generic_string<Ptr>;

  using base::base;
  using base::operator std::string_view;

  friend std::ostream& operator<<(std::ostream& out,
                                  basic_string_view const& s) {
    return out << s.view();
  }

  basic_string_view(std::string_view s) : base{s, base::non_owning} {}
  basic_string_view(std::string const& s) : base{s, base::non_owning} {}
  basic_string_view(char const* s) : base{s, base::non_owning} {}
  basic_string_view(char const* s, typename base::msize_t const len)
      : base{s, len, base::non_owning} {}

  basic_string_view(basic_string_view const& o) {
    base::set_non_owning(o.data(), o.size());
  }
  basic_string_view(basic_string_view&& o) {
    base::set_non_owning(o.data(), o.size());
  }
  basic_string_view& operator=(basic_string_view const& o) {
    base::set_non_owning(o.data(), o.size());
    return *this;
  }
  basic_string_view& operator=(basic_string_view&& o) {
    base::set_non_owning(o.data(), o.size());
    return *this;
  }

  basic_string_view& operator=(char const* s) {
    base::set_non_owning(s);
    return *this;
  }
  basic_string_view& operator=(std::string_view s) {
    base::set_non_owning(s);
    return *this;
  }
  basic_string_view& operator=(std::string const& s) {
    base::set_non_owning(s);
    return *this;
  }
};

template <typename Ptr>
struct is_string_helper<generic_string<Ptr>> : std::true_type {};

template <typename Ptr>
struct is_string_helper<basic_string<Ptr>> : std::true_type {};

template <typename Ptr>
struct is_string_helper<basic_string_view<Ptr>> : std::true_type {};

namespace raw {
using generic_string = generic_string<ptr<char const>>;
using string = basic_string<ptr<char const>>;
using string_view = basic_string_view<ptr<char const>>;
}  // namespace raw

namespace offset {
using generic_string = generic_string<ptr<char const>>;
using string = basic_string<ptr<char const>>;
using string_view = basic_string_view<ptr<char const>>;
}  // namespace offset

}  // namespace cista

#if __has_include("fmt/ostream.h")


template <typename Ptr>
struct fmt::formatter<cista::basic_string<Ptr>> : ostream_formatter {};

#endif

#include <type_traits>
#include <utility>


namespace cista {

template <typename T, typename Fn>
void for_each_ptr_field(T& t, Fn&& fn) {
  if constexpr (std::is_pointer_v<T>) {
    if (t != nullptr) {
      for_each_ptr_field(*t, std::forward<Fn>(fn));
    }
  } else if constexpr (std::is_scalar_v<T>) {
    fn(t);
  } else {
    std::apply([&](auto&&... args) { (fn(args), ...); }, to_ptr_tuple(t));
  }
}

template <typename T, typename Fn>
void for_each_field(T& t, Fn&& fn) {
  if constexpr (std::is_pointer_v<T>) {
    if (t != nullptr) {
      for_each_field(*t, std::forward<Fn>(fn));
    }
  } else if constexpr (std::is_scalar_v<T>) {
    fn(t);
  } else {
    std::apply([&](auto&&... args) { (fn(args), ...); }, to_tuple(t));
  }
}

template <typename T, typename Fn>
void for_each_field(Fn&& fn) {
  T t{};
  for_each_field<T>(t, std::forward<Fn>(fn));
}

}  // namespace cista

namespace cista {

namespace detail {

template <typename T, typename = void>
struct has_hash : std::false_type {};

template <typename T>
struct has_hash<T, std::void_t<decltype(std::declval<T>().hash())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_std_hash : std::false_type {};

template <typename T>
struct has_std_hash<
    T, std::void_t<decltype(std::declval<std::hash<T>>()(std::declval<T>()))>>
    : std::true_type {};

}  // namespace detail

template <typename T>
inline constexpr bool has_hash_v = detail::has_hash<T>::value;

template <typename T>
inline constexpr bool has_std_hash_v = detail::has_std_hash<T>::value;

template <typename A, typename B>
struct is_hash_equivalent_helper : std::false_type {};

template <typename A, typename B>
constexpr bool is_hash_equivalent_v =
    is_hash_equivalent_helper<std::remove_cv_t<A>, std::remove_cv_t<B>>::value;

template <typename T>
constexpr bool is_string_like_v =
    is_string_v<std::remove_cv_t<T>> || is_char_array_v<T> ||
    std::is_same_v<T, char const*> ||
    std::is_same_v<std::remove_cv_t<T>, std::string> ||
    std::is_same_v<std::remove_cv_t<T>, std::string_view>;

template <typename A, typename B>
constexpr bool is_ptr_same = is_pointer_v<A> && is_pointer_v<B>;

template <typename T>
struct hashing;

template <typename T>
struct hashing {
  template <typename A, typename B>
  static constexpr bool is_hash_equivalent() noexcept {
    using DecayA = decay_t<A>;
    using DecayB = decay_t<B>;
    return is_hash_equivalent_v<DecayA, DecayB> ||
           std::is_same_v<DecayA, DecayB> ||
           (is_string_like_v<DecayA> && is_string_like_v<DecayB>) ||
           std::is_convertible_v<A, B> || is_ptr_same<DecayA, DecayB>;
  }

  template <typename T1>
  static constexpr hashing<T1> create() noexcept {
    static_assert(is_hash_equivalent<T, T1>(), "Incompatible types");
    return hashing<T1>{};
  }

  constexpr hash_t operator()(T const& el,
                              hash_t const seed = BASE_HASH) const {
    using Type = decay_t<T>;
    if constexpr (has_hash_v<Type>) {
      return hash_combine(el.hash(), seed);
    } else if constexpr (is_pointer_v<Type>) {
      return hash_combine(seed, reinterpret_cast<intptr_t>(ptr_cast(el)));
    } else if constexpr (is_char_array_v<Type>) {
      return hash(std::string_view{el, sizeof(el) - 1U}, seed);
    } else if constexpr (is_string_like_v<Type>) {
      using std::begin;
      using std::end;
      return el.size() == 0U
                 ? seed
                 : hash(std::string_view{&(*begin(el)), el.size()}, seed);
    } else if constexpr (std::is_scalar_v<Type>) {
      return hash_combine(seed, el);
    } else if constexpr (is_iterable_v<Type>) {
      auto h = seed;
      for (auto const& v : el) {
        h = hashing<std::decay_t<decltype(v)>>()(v, h);
      }
      return h;
    } else if constexpr (to_tuple_works_v<Type>) {
      auto h = seed;
      for_each_field(el, [&h](auto&& f) {
        h = hashing<std::decay_t<decltype(f)>>{}(f, h);
      });
      return h;
    } else if constexpr (is_strong_v<Type>) {
      return hashing<typename Type::value_t>{}(el.v_, seed);
    } else if constexpr (has_std_hash_v<Type>) {
      return hash_combine(std::hash<Type>()(el), seed);
    } else {
      static_assert(has_hash_v<Type> || std::is_scalar_v<Type> ||
                        has_std_hash_v<Type> || is_iterable_v<Type> ||
                        to_tuple_works_v<Type> || is_strong_v<Type>,
                    "Implement hash");
    }
  }
};

template <typename Rep, typename Period>
struct hashing<std::chrono::duration<Rep, Period>> {
  hash_t operator()(std::chrono::duration<Rep, Period> const& el,
                    hash_t const seed = BASE_HASH) {
    return hashing<Rep>{}(el.count(), seed);
  }
};

template <typename T1, typename T2>
struct hashing<std::pair<T1, T2>> {
  constexpr hash_t operator()(std::pair<T1, T2> const& el,
                              hash_t const seed = BASE_HASH) {
    std::size_t h = seed;
    h = hashing<T1>{}(el.first, h);
    h = hashing<T2>{}(el.second, h);
    return h;
  }
};

template <typename T1, typename T2>
struct hashing<pair<T1, T2>> {
  constexpr hash_t operator()(pair<T1, T2> const& el,
                              hash_t const seed = BASE_HASH) {
    std::size_t h = seed;
    h = hashing<T1>{}(el.first, h);
    h = hashing<T2>{}(el.second, h);
    return h;
  }
};

template <typename... Args>
struct hashing<std::tuple<Args...>> {
  constexpr hash_t operator()(std::tuple<Args...> const& el,
                              hash_t const seed = BASE_HASH) {
    hash_t h = seed;
    std::apply(
        [&h](auto&&... args) {
          ((h = hashing<decltype(args)>{}(args, h)), ...);
        },
        el);
    return h;
  }
};

template <>
struct hashing<char const*> {
  hash_t operator()(char const* el, hash_t const seed = BASE_HASH) {
    return hash(std::string_view{el}, seed);
  }
};

template <typename... Args>
hash_t build_hash(Args const&... args) {
  hash_t h = BASE_HASH;
  ((h = hashing<decltype(args)>{}(args, h)), ...);
  return h;
}

}  // namespace cista

namespace cista {

struct get_first {
  template <typename T>
  auto&& operator()(T&& t) noexcept {
    return t.first;
  }
};

struct get_second {
  template <typename T>
  auto&& operator()(T&& t) noexcept {
    return t.second;
  }
};

namespace raw {
template <typename Key, typename Value, typename Hash = hashing<Key>,
          typename Eq = equal_to<Key>>
using hash_map =
    hash_storage<pair<Key, Value>, ptr, get_first, get_second, Hash, Eq>;
}  // namespace raw

namespace offset {
template <typename Key, typename Value, typename Hash = hashing<Key>,
          typename Eq = equal_to<Key>>
using hash_map =
    hash_storage<pair<Key, Value>, ptr, get_first, get_second, Hash, Eq>;
}  // namespace offset

}  // namespace cista

#include <functional>


namespace cista {

struct identity {
  template <typename T>
  decltype(auto) operator()(T&& t) const noexcept {
    return std::forward<T>(t);
  }
};

namespace raw {
template <typename T, typename Hash = hashing<T>, typename Eq = equal_to<T>>
using hash_set = hash_storage<T, ptr, identity, identity, Hash, Eq>;
}  // namespace raw

namespace offset {
template <typename T, typename Hash = hashing<T>, typename Eq = equal_to<T>>
using hash_set = hash_storage<T, ptr, identity, identity, Hash, Eq>;
}  // namespace offset

}  // namespace cista

#include <cassert>


#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif


#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#include <string>
#endif

#include <cinttypes>
#include <memory>


#include <cstdint>
#include <cstdlib>
#include <cstring>


namespace cista {

struct buffer final {
  constexpr buffer() noexcept : buf_(nullptr), size_(0U) {}

  explicit buffer(std::size_t const size)
      : buf_(std::malloc(size)), size_(size) {
    verify(buf_ != nullptr, "buffer initialization failed");
  }

  explicit buffer(char const* str) : buffer(std::strlen(str)) {
    std::memcpy(buf_, str, size_);
  }

  buffer(char const* str, std::size_t size) : buffer(size) {
    std::memcpy(buf_, str, size_);
  }

  ~buffer() {
    std::free(buf_);
    buf_ = nullptr;
  }

  buffer(buffer const&) = delete;
  buffer& operator=(buffer const&) = delete;

  buffer(buffer&& o) noexcept : buf_(o.buf_), size_(o.size_) { o.reset(); }

  buffer& operator=(buffer&& o) noexcept {
    buf_ = o.buf_;
    size_ = o.size_;
    o.reset();
    return *this;
  }

  std::size_t size() const noexcept { return size_; }

  std::uint8_t* data() noexcept { return static_cast<std::uint8_t*>(buf_); }
  std::uint8_t const* data() const noexcept {
    return static_cast<std::uint8_t const*>(buf_);
  }

  std::uint8_t* begin() noexcept { return data(); }
  std::uint8_t* end() noexcept { return data() + size_; }

  std::uint8_t const* begin() const noexcept { return data(); }
  std::uint8_t const* end() const noexcept { return data() + size_; }

  std::uint8_t& operator[](std::size_t const i) noexcept { return data()[i]; }
  std::uint8_t const& operator[](std::size_t const i) const noexcept {
    return data()[i];
  }

  void reset() noexcept {
    buf_ = nullptr;
    size_ = 0U;
  }

  void* buf_;
  std::size_t size_;
};

}  // namespace cista

#include <cinttypes>
#include <algorithm>

namespace cista {

template <typename Fn>
void chunk(unsigned const chunk_size, std::size_t const total, Fn fn) {
  std::size_t offset = 0U;
  std::size_t remaining = total;
  while (remaining != 0U) {
    auto const curr_chunk_size = static_cast<unsigned>(
        std::min(remaining, static_cast<std::size_t>(chunk_size)));
    fn(offset, curr_chunk_size);
    offset += curr_chunk_size;
    remaining -= curr_chunk_size;
  }
}

}  // namespace cista

#include <cinttypes>


namespace cista {

template <typename T>
static constexpr std::size_t serialized_size(
    void* const param = nullptr) noexcept {
  static_cast<void>(param);
  return sizeof(decay_t<T>);
}

}  // namespace cista

#ifdef _WIN32
namespace cista {

inline std::string last_error_str() {
  auto const err = ::GetLastError();
  if (err == 0) {
    return "no error";
  }

  struct buf {
    ~buf() {
      if (b_ != nullptr) {
        LocalFree(b_);
        b_ = nullptr;
      }
    }
    LPSTR b_ = nullptr;
  } b;
  auto const size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), b.b_, 0,
      nullptr);

  return size == 0 ? std::to_string(err) : std::string{b.b_, size};
}

inline HANDLE open_file(char const* path, char const* mode) {
  bool read = std::strcmp(mode, "r") == 0;
  bool write = std::strcmp(mode, "w+") == 0 || std::strcmp(mode, "r+") == 0;

  verify(read || write, "open file mode not supported");

  DWORD access = read ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
  DWORD create_mode = read ? OPEN_EXISTING : CREATE_ALWAYS;

  auto const f = CreateFileA(path, access, FILE_SHARE_READ, nullptr,
                             create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    throw_exception(std::runtime_error{std::string{"cannot open path="} + path +
                                       ", mode=" + mode + ", message=\"" +
                                       last_error_str() + "\""});
  }
  return f;
}

struct file {
  file() = default;

  file(char const* path, char const* mode)
      : f_(open_file(path, mode)), size_{size()} {}

  ~file() {
    if (f_ != nullptr) {
      CloseHandle(f_);
    }
    f_ = nullptr;
  }

  file(file const&) = delete;
  file& operator=(file const&) = delete;

  file(file&& o) : f_{o.f_}, size_{o.size_} {
    o.f_ = nullptr;
    o.size_ = 0U;
  }

  file& operator=(file&& o) {
    f_ = o.f_;
    size_ = o.size_;
    o.f_ = nullptr;
    o.size_ = 0U;
    return *this;
  }

  std::size_t size() const {
    if (f_ == nullptr) {
      return 0U;
    }
    LARGE_INTEGER filesize;
    verify(GetFileSizeEx(f_, &filesize), "file size error");
    return static_cast<std::size_t>(filesize.QuadPart);
  }

  buffer content() const {
    constexpr auto block_size = 8192u;
    std::size_t const file_size = size();

    auto b = buffer(file_size);

    chunk(block_size, size(), [&](std::size_t const from, unsigned block_size) {
      OVERLAPPED overlapped = {0};
      overlapped.Offset = static_cast<DWORD>(from);
#ifdef _WIN64
      overlapped.OffsetHigh = static_cast<DWORD>(from >> 32u);
#endif
      ReadFile(f_, b.data() + from, static_cast<DWORD>(block_size), nullptr,
               &overlapped);
    });

    return b;
  }

  std::uint64_t checksum(offset_t const start = 0) const {
    constexpr auto const block_size = 512U * 1024U;  // 512kB
    auto c = BASE_HASH;
    char buf[block_size];
    chunk(block_size, size_ - static_cast<std::size_t>(start),
          [&](auto const from, auto const size) {
            OVERLAPPED overlapped = {0};
            overlapped.Offset = static_cast<DWORD>(start + from);
#ifdef _WIN64
            overlapped.OffsetHigh = static_cast<DWORD>((start + from) >> 32U);
#endif
            DWORD bytes_read = {0};
            verify(ReadFile(f_, buf, static_cast<DWORD>(size), &bytes_read,
                            &overlapped),
                   "checksum read error");
            verify(bytes_read == size, "checksum read error bytes read");
            c = hash(std::string_view{buf, size}, c);
          });
    return c;
  }

  template <typename T>
  void write(std::size_t const pos, T const& val) {
    OVERLAPPED overlapped = {0};
    overlapped.Offset = static_cast<DWORD>(pos);
#ifdef _WIN64
    overlapped.OffsetHigh = pos >> 32u;
#endif
    DWORD bytes_written = {0};
    verify(WriteFile(f_, &val, sizeof(T), &bytes_written, &overlapped),
           "write(pos, val) write error");
    verify(bytes_written == sizeof(T),
           "write(pos, val) write error bytes written");
  }

  offset_t write(void const* ptr, std::size_t const size,
                 std::size_t alignment) {
    auto curr_offset = size_;
    if (alignment != 0 && alignment != 1) {
      auto unaligned_ptr = reinterpret_cast<void*>(size_);
      auto space = std::numeric_limits<std::size_t>::max();
      auto const aligned_ptr =
          std::align(alignment, size, unaligned_ptr, space);
      curr_offset = aligned_ptr ? reinterpret_cast<std::uintptr_t>(aligned_ptr)
                                : curr_offset;
    }

    std::uint8_t const buf[16U] = {0U};
    auto const num_padding_bytes = static_cast<DWORD>(curr_offset - size_);
    if (num_padding_bytes != 0U) {
      verify(num_padding_bytes < 16U, "invalid padding size");
      OVERLAPPED overlapped = {0};
      overlapped.Offset = static_cast<std::uint32_t>(size_);
#ifdef _WIN64
      overlapped.OffsetHigh = static_cast<std::uint32_t>(size_ >> 32u);
#endif
      DWORD bytes_written = {0};
      verify(WriteFile(f_, buf, num_padding_bytes, &bytes_written, &overlapped),
             "write padding error");
      verify(bytes_written == num_padding_bytes,
             "write padding error bytes written");
      size_ = curr_offset;
    }

    constexpr auto block_size = 8192u;
    chunk(block_size, size, [&](std::size_t const from, unsigned block_size) {
      OVERLAPPED overlapped = {0};
      overlapped.Offset = 0xFFFFFFFF;
      overlapped.OffsetHigh = 0xFFFFFFFF;
      DWORD bytes_written = {0};
      verify(WriteFile(f_, reinterpret_cast<std::uint8_t const*>(ptr) + from,
                       block_size, &bytes_written, &overlapped),
             "write error");
      verify(bytes_written == block_size, "write error bytes written");
    });

    auto const offset = size_;
    size_ += size;

    return offset;
  }

  HANDLE f_{nullptr};
  std::size_t size_{0U};
};
}  // namespace cista
#else

#include <cstdio>

#include <sys/stat.h>

namespace cista {

struct file {
  file() = default;

  file(char const* path, char const* mode)
      : f_{std::fopen(path, mode)}, size_{size()} {
    verify(f_ != nullptr, "unable to open file");
  }

  ~file() {
    if (f_ != nullptr) {
      std::fclose(f_);
    }
    f_ = nullptr;
  }

  file(file const&) = delete;
  file& operator=(file const&) = delete;

  file(file&& o) : f_{o.f_}, size_{o.size_} {
    o.f_ = nullptr;
    o.size_ = 0U;
  }

  file& operator=(file&& o) {
    f_ = o.f_;
    size_ = o.size_;
    o.f_ = nullptr;
    o.size_ = 0U;
    return *this;
  }

  int fd() const {
    auto const fd = fileno(f_);
    verify(fd != -1, "invalid fd");
    return fd;
  }

  std::size_t size() const {
    if (f_ == nullptr) {
      return 0U;
    }
    struct stat s;
    verify(fstat(fd(), &s) != -1, "fstat error");
    return static_cast<std::size_t>(s.st_size);
  }

  buffer content() {
    auto b = buffer(size());
    verify(std::fread(b.data(), 1U, b.size(), f_) == b.size(), "read error");
    return b;
  }

  std::uint64_t checksum(offset_t const start = 0) const {
    constexpr auto const block_size =
        static_cast<std::size_t>(512U * 1024U);  // 512kB
    verify(size_ >= static_cast<std::size_t>(start), "invalid checksum offset");
    verify(!std::fseek(f_, static_cast<long>(start), SEEK_SET), "fseek error");
    auto c = BASE_HASH;
    char buf[block_size];
    chunk(block_size, size_ - static_cast<std::size_t>(start),
          [&](auto const, auto const s) {
            verify(std::fread(buf, 1U, s, f_) == s, "invalid read");
            c = hash(std::string_view{buf, s}, c);
          });
    return c;
  }

  template <typename T>
  void write(std::size_t const pos, T const& val) {
    verify(!std::fseek(f_, static_cast<long>(pos), SEEK_SET), "seek error");
    verify(std::fwrite(reinterpret_cast<std::uint8_t const*>(&val), 1U,
                       serialized_size<T>(), f_) == serialized_size<T>(),
           "write error");
  }

  offset_t write(void const* ptr, std::size_t const size,
                 std::size_t alignment) {
    auto curr_offset = size_;
    auto seek_offset = long{0};
    auto seek_whence = int{SEEK_END};
    if (alignment > 1U) {
      auto unaligned_ptr = reinterpret_cast<void*>(size_);
      auto space = std::numeric_limits<std::size_t>::max();
      auto const aligned_ptr =
          std::align(alignment, size, unaligned_ptr, space);
      if (aligned_ptr != nullptr) {
        curr_offset = reinterpret_cast<std::uintptr_t>(aligned_ptr);
      }
      seek_offset = static_cast<long>(curr_offset);
      seek_whence = SEEK_SET;
    }
    verify(!std::fseek(f_, seek_offset, seek_whence), "seek error");
    verify(std::fwrite(ptr, 1U, size, f_) == size, "write error");
    size_ = curr_offset + size;
    return static_cast<offset_t>(curr_offset);
  }

  FILE* f_{nullptr};
  std::size_t size_{0U};
};

}  // namespace cista

#endif

namespace cista {

struct mmap {
  static constexpr auto const OFFSET = 0ULL;
  static constexpr auto const ENTIRE_FILE =
      std::numeric_limits<std::size_t>::max();
  enum class protection { READ, WRITE, MODIFY };

  mmap() = default;

  explicit mmap(char const* path, protection const prot = protection::WRITE)
      : f_{path, prot == protection::MODIFY
                     ? "r+"
                     : (prot == protection::READ ? "r" : "w+")},
        prot_{prot},
        size_{f_.size()},
        used_size_{f_.size()},
        addr_{size_ == 0U ? nullptr : map()} {}

  ~mmap() {
    if (addr_ != nullptr) {
      sync();
      size_ = used_size_;
      unmap();
      if (size_ != f_.size()) {
        resize_file();
      }
    }
  }

  mmap(mmap const&) = delete;
  mmap& operator=(mmap const&) = delete;

  mmap(mmap&& o)
      : f_{std::move(o.f_)},
        prot_{o.prot_},
        size_{o.size_},
        used_size_{o.used_size_},
        addr_{o.addr_} {
#ifdef _WIN32
    file_mapping_ = o.file_mapping_;
#endif
    o.addr_ = nullptr;
  }

  mmap& operator=(mmap&& o) {
    f_ = std::move(o.f_);
    prot_ = o.prot_;
    size_ = o.size_;
    used_size_ = o.used_size_;
    addr_ = o.addr_;
#ifdef _WIN32
    file_mapping_ = o.file_mapping_;
#endif
    o.addr_ = nullptr;
    return *this;
  }

  void sync() {
    if ((prot_ == protection::WRITE || prot_ == protection::MODIFY) &&
        addr_ != nullptr) {
#ifdef _WIN32
      verify(::FlushViewOfFile(addr_, size_) != 0, "flush error");
      verify(::FlushFileBuffers(f_.f_) != 0, "flush error");
#else
      verify(::msync(addr_, size_, MS_SYNC) == 0, "sync error");
#endif
    }
  }

  void resize(std::size_t const new_size) {
    verify(prot_ == protection::WRITE || prot_ == protection::MODIFY,
           "read-only not resizable");
    if (size_ < new_size) {
      resize_map(next_power_of_two(new_size));
    }
    used_size_ = new_size;
  }

  void reserve(std::size_t const new_size) {
    verify(prot_ == protection::WRITE || prot_ == protection::MODIFY,
           "read-only not resizable");
    if (size_ < new_size) {
      resize_map(next_power_of_two(new_size));
    }
  }

  std::size_t size() const noexcept { return used_size_; }

  std::string_view view() const noexcept {
    return {static_cast<char const*>(addr_), size()};
  }
  std::uint8_t* data() noexcept { return static_cast<std::uint8_t*>(addr_); }
  std::uint8_t const* data() const noexcept {
    return static_cast<std::uint8_t const*>(addr_);
  }

  std::uint8_t* begin() noexcept { return data(); }
  std::uint8_t* end() noexcept { return data() + used_size_; }
  std::uint8_t const* begin() const noexcept { return data(); }
  std::uint8_t const* end() const noexcept { return data() + used_size_; }

  std::uint8_t& operator[](std::size_t const i) noexcept { return data()[i]; }
  std::uint8_t const& operator[](std::size_t const i) const noexcept {
    return data()[i];
  }

private:
  void unmap() {
#ifdef _WIN32
    if (addr_ != nullptr) {
      verify(::UnmapViewOfFile(addr_), "unmap error");
      addr_ = nullptr;

      verify(::CloseHandle(file_mapping_), "close file mapping error");
      file_mapping_ = nullptr;
    }
#else
    if (addr_ != nullptr) {
      ::munmap(addr_, size_);
      addr_ = nullptr;
    }
#endif
  }

  void* map() {
#ifdef _WIN32
    auto const size_low = static_cast<DWORD>(size_);
#ifdef _WIN64
    auto const size_high = static_cast<DWORD>(size_ >> 32U);
#else
    auto const size_high = static_cast<DWORD>(0U);
#endif
    const auto fm = ::CreateFileMapping(
        f_.f_, 0, prot_ == protection::READ ? PAGE_READONLY : PAGE_READWRITE,
        size_high, size_low, 0);
    verify(fm != NULL, "file mapping error");
    file_mapping_ = fm;

    auto const addr = ::MapViewOfFile(
        fm, prot_ == protection::READ ? FILE_MAP_READ : FILE_MAP_WRITE, OFFSET,
        OFFSET, size_);
    verify(addr != nullptr, "map error");

    return addr;
#else
    auto const addr =
        ::mmap(nullptr, size_,
               prot_ == protection::READ ? PROT_READ : PROT_READ | PROT_WRITE,
               MAP_SHARED, f_.fd(), OFFSET);
    verify(addr != MAP_FAILED, "map error");
    return addr;
#endif
  }

  void resize_file() {
    if (prot_ == protection::READ) {
      return;
    }

#ifdef _WIN32
    LARGE_INTEGER Size = {0};
    verify(::GetFileSizeEx(f_.f_, &Size), "resize: get file size error");

    LARGE_INTEGER Distance = {0};
    Distance.QuadPart = size_ - Size.QuadPart;
    verify(::SetFilePointerEx(f_.f_, Distance, nullptr, FILE_END),
           "resize error");
    verify(::SetEndOfFile(f_.f_), "resize set eof error");
#else
    verify(::ftruncate(f_.fd(), static_cast<off_t>(size_)) == 0,
           "resize error");
#endif
  }

  void resize_map(std::size_t const new_size) {
    if (prot_ == protection::READ) {
      return;
    }

    unmap();
    size_ = new_size;
    resize_file();
    addr_ = map();
  }

  file f_;
  protection prot_;
  std::size_t size_;
  std::size_t used_size_;
  void* addr_;
#ifdef _WIN32
  HANDLE file_mapping_;
#endif
};

}  // namespace cista

namespace cista {

template <typename T, typename Key = std::uint32_t>
struct basic_mmap_vec {
  using size_type = base_t<Key>;
  using difference_type = std::ptrdiff_t;
  using access_type = Key;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T*;
  using value_type = T;
  using iterator = T*;
  using const_iterator = T const*;

  static_assert(std::is_trivially_copyable_v<T>);

  explicit basic_mmap_vec(cista::mmap mmap)
      : mmap_{std::move(mmap)},
        used_size_{static_cast<size_type>(mmap_.size() / sizeof(T))} {}

  void push_back(T const& t) {
    ++used_size_;
    mmap_.resize(sizeof(T) * used_size_);
    (*this)[Key{used_size_ - 1U}] = t;
  }

  template <typename... Args>
  T& emplace_back(Args&&... el) {
    reserve(used_size_ + 1U);
    new (data() + used_size_) T{std::forward<Args>(el)...};
    T* ptr = data() + used_size_;
    ++used_size_;
    return *ptr;
  }

  size_type size() const { return used_size_; }

  T const* data() const noexcept { return begin(); }
  T* data() noexcept { return begin(); }
  T const* begin() const noexcept {
    return reinterpret_cast<T const*>(mmap_.data());
  }
  T const* end() const noexcept { return begin() + used_size_; }  // NOLINT
  T const* cbegin() const noexcept { return begin(); }
  T const* cend() const noexcept { return begin() + used_size_; }  // NOLINT
  T* begin() noexcept { return reinterpret_cast<T*>(mmap_.data()); }
  T* end() noexcept { return begin() + used_size_; }  // NOLINT

  friend T const* begin(basic_mmap_vec const& a) noexcept { return a.begin(); }
  friend T const* end(basic_mmap_vec const& a) noexcept { return a.end(); }

  friend T* begin(basic_mmap_vec& a) noexcept { return a.begin(); }
  friend T* end(basic_mmap_vec& a) noexcept { return a.end(); }

  bool empty() const noexcept { return size() == 0U; }

  T const& operator[](access_type const index) const noexcept {
    assert(index < used_size_);
    return begin()[to_idx(index)];
  }

  T& operator[](access_type const index) noexcept {
    assert(used_size_);
    return begin()[to_idx(index)];
  }

  void reserve(size_type const size) { mmap_.resize(size * sizeof(T)); }

  void resize(size_type const size) {
    mmap_.resize(size * sizeof(T));
    for (auto i = used_size_; i < size; ++i) {
      new (data() + i) T{};
    }
    used_size_ = size;
  }

  template <typename It>
  void set(It begin_it, It end_it) {
    using diff_t =
        std::common_type_t<typename std::iterator_traits<It>::difference_type,
                           size_type>;
    auto const range_size = std::distance(begin_it, end_it);
    verify(range_size >= 0 &&
               static_cast<diff_t>(range_size) <=
                   static_cast<diff_t>(std::numeric_limits<size_type>::max()),
           "cista::vector::set: invalid range");

    reserve(static_cast<size_type>(range_size));

    auto copy_source = begin_it;
    auto copy_target = data();
    for (; copy_source != end_it; ++copy_source, ++copy_target) {
      new (copy_target) T{std::forward<decltype(*copy_source)>(*copy_source)};
    }

    used_size_ = static_cast<size_type>(range_size);
  }

  template <typename Arg>
  T* insert(T* it, Arg&& el) {
    auto const old_offset = std::distance(begin(), it);
    auto const old_size = used_size_;

    reserve(used_size_ + 1);
    new (data() + used_size_) T{std::forward<Arg&&>(el)};
    ++used_size_;

    return std::rotate(begin() + old_offset, begin() + old_size, end());
  }

  template <class InputIt>
  T* insert(T* pos, InputIt first, InputIt last, std::input_iterator_tag) {
    auto const old_offset = std::distance(begin(), pos);
    auto const old_size = used_size_;

    for (; !(first == last); ++first) {
      reserve(used_size_ + 1);
      new (data() + used_size_) T{std::forward<decltype(*first)>(*first)};
      ++used_size_;
    }

    return std::rotate(begin() + old_offset, begin() + old_size, end());
  }

  template <class FwdIt>
  T* insert(T* pos, FwdIt first, FwdIt last, std::forward_iterator_tag) {
    if (empty()) {
      set(first, last);
      return begin();
    }

    auto const pos_idx = pos - begin();
    auto const new_count = static_cast<size_type>(std::distance(first, last));
    reserve(used_size_ + new_count);
    pos = begin() + pos_idx;

    for (auto src_last = end() - 1, dest_last = end() + new_count - 1;
         !(src_last == pos - 1); --src_last, --dest_last) {
      if (dest_last >= end()) {
        new (dest_last) T(std::move(*src_last));
      } else {
        *dest_last = std::move(*src_last);
      }
    }

    for (auto insert_ptr = pos; !(first == last); ++first, ++insert_ptr) {
      if (insert_ptr >= end()) {
        new (insert_ptr) T(std::forward<decltype(*first)>(*first));
      } else {
        *insert_ptr = std::forward<decltype(*first)>(*first);
      }
    }

    used_size_ += new_count;

    return pos;
  }

  template <class It>
  T* insert(T* pos, It first, It last) {
    return insert(pos, first, last,
                  typename std::iterator_traits<It>::iterator_category());
  }

  cista::mmap mmap_;
  size_type used_size_{0U};
};

template <typename T>
using mmap_vec = basic_mmap_vec<T>;

template <typename Key, typename T>
using mmap_vec_map = basic_mmap_vec<Key, T>;

}  // namespace cista

#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>


namespace cista {

template <typename T, typename SizeType, template <typename> typename Vec,
          std::size_t Log2MaxEntriesPerBucket = 20U>
struct dynamic_fws_multimap_base {
  using value_type = T;
  using size_type = base_t<SizeType>;
  using access_t = SizeType;
  using data_vec_t = Vec<value_type>;
  static constexpr auto const MAX_ENTRIES_PER_BUCKET =
      static_cast<size_type>(1ULL << Log2MaxEntriesPerBucket);

  struct index_type {
    size_type begin_{};
    size_type size_{};
    size_type capacity_{};
  };
  using index_vec_t = Vec<index_type>;

  template <bool Const>
  struct bucket {
    friend dynamic_fws_multimap_base;

    using value_type = T;
    using iterator = typename data_vec_t::iterator;
    using const_iterator = typename data_vec_t::const_iterator;

    template <bool IsConst = Const, typename = std::enable_if_t<IsConst>>
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    bucket(bucket<false> const& b) : multimap_{b.multimap_}, index_{b.index_} {}

    size_type index() const noexcept { return index_; }
    size_t size() const noexcept { return get_index().size_; }
    size_type capacity() const noexcept { return get_index().capacity_; }
    bool empty() const noexcept { return size() == 0; }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-conversion"
#endif
    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    operator bucket<true>() {
      return bucket<true>{multimap_, index_};
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    iterator begin() { return mutable_mm().data_.begin() + get_index().begin_; }

    const_iterator begin() const {
      return multimap_.data_.begin() + get_index().begin_;
    }

    iterator end() {
      auto const& index = get_index();
      return std::next(mutable_mm().data_.begin(), index.begin_ + index.size_);
    }

    const_iterator end() const {
      auto const& index = get_index();
      return std::next(multimap_.data_.begin(), index.begin_ + index.size_);
    }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    friend iterator begin(bucket& b) { return b.begin(); }
    friend const_iterator begin(bucket const& b) { return b.begin(); }
    friend iterator end(bucket& b) { return b.end(); }
    friend const_iterator end(bucket const& b) { return b.end(); }

    value_type& operator[](size_type index) {
      return mutable_mm().data_[data_index(index)];
    }

    value_type const& operator[](size_type index) const {
      return multimap_.data_[data_index(index)];
    }

    value_type& at(size_type const index) {
      return mutable_mm().data_[get_and_check_data_index(index)];
    }

    value_type const& at(size_type const index) const {
      return multimap_.data_[get_and_check_data_index(index)];
    }

    value_type& front() { return (*this)[0U]; }
    value_type const& front() const { return (*this)[0U]; }

    value_type& back() {
      assert(!empty());
      return (*this)[static_cast<size_type>(size() - 1U)];
    }

    value_type const& back() const {
      assert(!empty());
      return (*this)[static_cast<size_type>(size() - 1U)];
    }

    size_type data_index(size_type const index) const {
      assert(index < get_index().size_);
      return get_index().begin_ + index;
    }

    size_type bucket_index(const_iterator it) const {
      if (it < begin() || it >= end()) {
        throw_exception(std::out_of_range{
            "dynamic_fws_multimap::bucket::bucket_index() out of range"});
      }
      return std::distance(begin(), it);
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    size_type push_back(value_type const& val) {
      return mutable_mm().push_back_entry(index_, val);
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>,
              typename... Args>
    size_type emplace_back(Args&&... args) {
      return mutable_mm().emplace_back_entry(index_,
                                             std::forward<Args>(args)...);
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    iterator insert(iterator it, value_type const& val) {
      auto insert_it = prepare_insert(it);
      *insert_it = val;
      return insert_it;
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    iterator insert(iterator it, value_type&& val) {
      auto insert_it = prepare_insert(it);
      *insert_it = std::move(val);
      return insert_it;
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    void reserve(size_type const new_size) {
      if (new_size > capacity()) {
        mutable_mm().grow_bucket(index_, get_index(), new_size);
      }
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    void resize(size_type const new_size,
                value_type const init = value_type{}) {
      auto const old_size = size();
      reserve(new_size);
      auto& index = get_index();
      auto& data = mutable_mm().data_;
      if (new_size < old_size) {
        for (auto i = new_size; i < old_size; ++i) {
          data[index.begin_ + i].~T();
        }
        mutable_mm().element_count_ -= old_size - new_size;
      } else if (new_size > old_size) {
        for (auto i = old_size; i < new_size; ++i) {
          data[static_cast<unsigned>(index.begin_ + i)] = init;
        }
        mutable_mm().element_count_ += new_size - old_size;
      }
      index.size_ = new_size;
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    void pop_back() {
      if (!empty()) {
        resize(static_cast<size_type>(size() - 1U));
      }
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    void clear() {
      auto& index = get_index();
      auto& data = mutable_mm().data_;
      for (auto i = index.begin_; i < index.begin_ + index.size_; ++i) {
        data[i].~T();
      }
      mutable_mm().element_count_ -= index.size_;
      index.size_ = 0U;
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    iterator erase(iterator pos) {
      auto last = std::prev(end());
      while (pos < last) {
        std::swap(*pos, *std::next(pos));
        pos = std::next(pos);
      }
      (*pos).~T();
      --get_index().size_;
      --mutable_mm().element_count_;
      return end();
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    iterator erase(iterator first, iterator last) {
      if (first != last) {
        auto const new_end = std::move(last, end(), first);
        for (auto it = new_end; it != end(); it = std::next(it)) {
          (*it).~T();
        }
        auto const count = std::distance(new_end, end());
        get_index().size_ -= count;
        mutable_mm().element_count_ -= count;
      }
      return end();
    }

  protected:
    bucket(dynamic_fws_multimap_base const& multimap, size_type const index)
        : multimap_(multimap), index_(index) {}

    index_type& get_index() { return mutable_mm().index_[index_]; }
    index_type const& get_index() const { return multimap_.index_[index_]; }

    size_type get_and_check_data_index(size_type index) const {
      auto const& idx = get_index();
      if (index >= idx.size_) {
        throw_exception(std::out_of_range{
            "dynamic_fws_multimap::bucket::at() out of range"});
      }
      return idx.begin_ + index;
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    iterator prepare_insert(bucket::iterator it) {
      auto const pos = std::distance(begin(), it);
      auto& index = get_index();
      reserve(index.size_ + 1U);
      it = std::next(begin(), pos);
      std::move_backward(it, end(), std::next(end()));
      ++index.size_;
      ++mutable_mm().element_count_;
      return it;
    }

    dynamic_fws_multimap_base& mutable_mm() noexcept {
      return const_cast<dynamic_fws_multimap_base&>(multimap_);  // NOLINT
    }

    dynamic_fws_multimap_base const& multimap_;
    size_type index_;
  };

  using mutable_bucket = bucket<false>;
  using const_bucket = bucket<true>;

  template <bool Const>
  struct bucket_iterator {
    friend dynamic_fws_multimap_base;
    using iterator_category = std::random_access_iterator_tag;
    using value_type = bucket<Const>;
    using difference_type = int;
    using pointer = value_type;
    using reference = value_type;

    template <bool IsConst = Const, typename = std::enable_if_t<IsConst>>
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    bucket_iterator(bucket_iterator<false> const& it)
        : multimap_{it.multimap_}, index_{it.index_} {}

    value_type operator*() const {
      return const_cast<dynamic_fws_multimap_base&>(multimap_)  // NOLINT
          .at(access_t{index_});
    }

    value_type operator->() const { return multimap_.at(access_t{index_}); }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    value_type operator->() {
      return const_cast<dynamic_fws_multimap_base&>(multimap_)  // NOLINT
          .at(access_t{index_});
    }

    bucket_iterator& operator+=(difference_type n) {
      index_ += n;
      return *this;
    }

    bucket_iterator& operator-=(difference_type n) {
      index_ -= n;
      return *this;
    }

    bucket_iterator& operator++() {
      ++index_;
      return *this;
    }

    bucket_iterator operator++(int) {
      auto old = *this;
      ++(*this);
      return old;
    }

    bucket_iterator& operator--() {
      ++index_;
      return *this;
    }

    bucket_iterator operator--(int) {
      auto old = *this;
      --(*this);
      return old;
    }

    bucket_iterator operator+(difference_type n) const {
      return {multimap_, index_ + n};
    }

    bucket_iterator operator-(difference_type n) const {
      return {multimap_, index_ - n};
    }

    difference_type operator-(bucket_iterator const& rhs) const {
      return static_cast<difference_type>(index_) -
             static_cast<difference_type>(rhs.index_);
    }

    value_type operator[](difference_type n) const {
      return multimap_.at(access_t{index_ + n});
    }

    template <bool IsConst = Const, typename = std::enable_if_t<!IsConst>>
    value_type operator[](difference_type const n) {
      return const_cast<dynamic_fws_multimap_base&>(multimap_)  // NOLINT
          .at(access_t{index_ + n});
    }

    bool operator<(bucket_iterator const& rhs) const {
      return index_ < rhs.index_;
    }
    bool operator<=(bucket_iterator const& rhs) const {
      return index_ <= rhs.index_;
    }
    bool operator>(bucket_iterator const& rhs) const {
      return index_ > rhs.index_;
    }
    bool operator>=(bucket_iterator const& rhs) const {
      return index_ >= rhs.index_;
    }

    bool operator==(bucket_iterator const& rhs) const {
      return index_ == rhs.index_ && &multimap_ == &rhs.multimap_;
    }

    bool operator!=(bucket_iterator const& rhs) const {
      return index_ != rhs.index_ || &multimap_ != &rhs.multimap_;
    }

  protected:
    bucket_iterator(dynamic_fws_multimap_base const& multimap,
                    size_type const index)
        : multimap_{multimap}, index_{index} {}

    dynamic_fws_multimap_base const& multimap_;
    size_type index_;
  };

  using iterator = bucket_iterator<false>;
  using const_iterator = bucket_iterator<true>;

  mutable_bucket operator[](access_t index) {
    if (index >= index_.size()) {
      index_.resize(to_idx(index) + 1U);
    }
    return {*this, to_idx(index)};
  }

  const_bucket operator[](access_t const index) const {
    assert(index < index_.size());
    return {*this, to_idx(index)};
  }

  mutable_bucket at(access_t const index) {
    if (index >= index_.size()) {
      throw_exception(
          std::out_of_range{"dynamic_fws_multimap::at() out of range"});
    }
    return {*this, to_idx(index)};
  }

  const_bucket at(access_t const index) const {
    if (index >= index_.size()) {
      throw_exception(
          std::out_of_range{"dynamic_fws_multimap::at() out of range"});
    }
    return {*this, to_idx(index)};
  }

  mutable_bucket front() { return (*this)[access_t{0U}]; }
  const_bucket front() const { return (*this)[access_t{0U}]; }

  mutable_bucket back() { return (*this)[access_t{size() - 1U}]; }
  const_bucket back() const { return (*this)[access_t{size() - 1U}]; }

  mutable_bucket emplace_back() { return (*this)[access_t{size()}]; }

  mutable_bucket get_or_create(access_t const index) {
    verify(index != std::numeric_limits<size_type>::max(),
           "mutable_fws_multimap::get_or_create: type bound");
    if (to_idx(index) + 1U >= index_.size()) {
      index_.resize(to_idx(index + 1U));
    }
    return {*this, to_idx(index)};
  }

  void erase(access_t const i) {
    if (to_idx(i) < index_.size()) {
      release_bucket(index_[to_idx(i)]);
    }
  }

  size_type size() const noexcept { return index_.size(); }
  size_type data_size() const noexcept { return data_.size(); }
  size_type element_count() const noexcept { return element_count_; }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  std::size_t allocated_size() const noexcept {
    auto size = index_.allocated_size_ * sizeof(index_type) +
                data_.allocated_size_ * sizeof(value_type);
    for (auto const& v : free_buckets_) {
      size += v.allocated_size_ * sizeof(index_type);
    }
    return size;
  }

  constexpr size_type max_entries_per_bucket() const noexcept {
    return MAX_ENTRIES_PER_BUCKET;
  }

  constexpr size_type max_entries_per_bucket_log2() const noexcept {
    return Log2MaxEntriesPerBucket;
  }

  iterator begin() { return {*this, size_type{0U}}; }
  const_iterator begin() const { return {*this, size_type{0U}}; }
  iterator end() {
    return iterator{*this, static_cast<size_type>(index_.size())};
  }
  const_iterator end() const {
    return const_iterator{*this, static_cast<size_type>(index_.size())};
  }

  friend iterator begin(dynamic_fws_multimap_base& m) { return m.begin(); }
  friend const_iterator begin(dynamic_fws_multimap_base const& m) {
    return m.begin();
  }

  friend iterator end(dynamic_fws_multimap_base& m) { return m.end(); }
  friend const_iterator end(dynamic_fws_multimap_base const& m) {
    return m.end();
  }

  data_vec_t& data() noexcept { return data_; }
  data_vec_t const& data() const noexcept { return data_; }

  void reserve(size_type index, size_type data) {
    index_.reserve(index);
    data_.reserve(data);
  }

  void clear() {
    index_.clear();
    data_.clear();
    for (auto& e : free_buckets_) {
      e.clear();
    }
    element_count_ = 0U;
  }

  size_type insert_new_entry(size_type const i) {
    auto const map_index = to_idx(i);
    assert(map_index < index_.size());
    auto& idx = index_[map_index];
    if (idx.size_ == idx.capacity_) {
      grow_bucket(map_index, idx);
    }
    auto const data_index = idx.begin_ + idx.size_;
    ++idx.size_;
    assert(idx.size_ <= idx.capacity_);
    return data_index;
  }

  void grow_bucket(size_type const map_index, index_type& idx) {
    grow_bucket(to_idx(map_index), idx, idx.capacity_ + 1U);
  }

  void grow_bucket(size_type const map_index, index_type& idx,
                   size_type const requested_capacity) {
    /* Currently, only trivially copyable types are supported.
     * Changing this would require to do custom memory management. */
    static_assert(std::is_trivially_copyable_v<T>);

    assert(requested_capacity > 0U);
    auto const new_capacity =
        size_type{cista::next_power_of_two(to_idx(requested_capacity))};
    auto const new_order = get_order(new_capacity);

    verify(new_order <= Log2MaxEntriesPerBucket,
           "dynamic_fws_multimap: too many entries in a bucket");

    auto old_bucket = idx;

    auto free_bucket = get_free_bucket(new_order);
    if (free_bucket) {
      // reuse free bucket
      if (old_bucket.capacity_ != 0U) {
        move_entries(map_index, old_bucket.begin_, free_bucket->begin_,
                     idx.size_);
        release_bucket(old_bucket);
      }
      idx.begin_ = free_bucket->begin_;
      idx.capacity_ = free_bucket->capacity_;
    } else {
      if (idx.begin_ + idx.capacity_ == data_.size()) {
        // last bucket -> resize data vector
        auto const additional_capacity = new_capacity - idx.capacity_;
        data_.resize(data_.size() + additional_capacity);
        idx.capacity_ = new_capacity;
      } else {
        // allocate new bucket at the end
        auto const new_begin = data_.size();
        data_.resize(data_.size() + new_capacity);
        move_entries(map_index, idx.begin_, new_begin, idx.size_);
        idx.begin_ = new_begin;
        idx.capacity_ = new_capacity;
        release_bucket(old_bucket);
      }
    }
  }

  std::optional<index_type> get_free_bucket(size_type const requested_order) {
    assert(requested_order <= Log2MaxEntriesPerBucket);

    auto const pop = [](index_vec_t& vec) -> std::optional<index_type> {
      if (!vec.empty()) {
        auto it = std::prev(vec.end());
        auto const entry = *it;
        vec.erase(it);
        return entry;
      }
      return {};
    };

    return pop(free_buckets_[to_idx(requested_order)]);  // NOLINT
  }

  void release_bucket(index_type& bucket) {
    if (bucket.capacity_ != 0U) {
      auto const order = get_order(bucket.capacity_);
      assert(order <= Log2MaxEntriesPerBucket);
      bucket.size_ = size_type{0U};
      free_buckets_[to_idx(order)].push_back(index_type{bucket});  // NOLINT
      bucket.capacity_ = size_type{0U};
    }
  }

  void move_entries(size_type const /* map_index */,
                    size_type const old_data_index,
                    size_type const new_data_index, size_type const count) {
    if (count == 0U) {
      return;
    }
    auto old_data = &data_[old_data_index];
    auto new_data = &data_[new_data_index];
    if constexpr (std::is_trivially_copyable_v<value_type>) {
      std::memcpy(new_data, old_data, to_idx(count) * sizeof(value_type));
    } else {
      for (auto i = static_cast<size_type>(0); i < count;
           ++i, ++old_data, ++new_data) {
        *new_data = value_type(std::move(*old_data));
        old_data->~T();
      }
    }
  }

  size_type push_back_entry(size_type const map_index, value_type const& val) {
    auto const data_index = insert_new_entry(map_index);
    data_[data_index] = val;
    ++element_count_;
    return data_index;
  }

  template <typename... Args>
  size_type emplace_back_entry(size_type const i, Args&&... args) {
    auto const map_index = to_idx(i);
    auto const data_index = insert_new_entry(map_index);
    data_[data_index] = value_type{std::forward<Args>(args)...};
    ++element_count_;
    return data_index;
  }

  static size_type get_order(size_type const size) {
    return size_type{cista::trailing_zeros(to_idx(size))};
  }

  index_vec_t index_;
  data_vec_t data_;
  array<index_vec_t, Log2MaxEntriesPerBucket + 1U> free_buckets_;
  size_type element_count_{};
};

namespace offset {

template <typename K, typename V, std::size_t LogMaxBucketSize = 20U>
struct mutable_multimap_helper {
  template <typename T>
  using vec = vector<T>;
  using type = dynamic_fws_multimap_base<V, K, vec, LogMaxBucketSize>;
};

template <typename K, typename V, std::size_t LogMaxBucketSize = 20U>
using mutable_fws_multimap =
    typename mutable_multimap_helper<K, V, LogMaxBucketSize>::type;

}  // namespace offset

namespace raw {

template <typename K, typename V, std::size_t LogMaxBucketSize = 20U>
struct mutable_multimap_helper {
  template <typename T>
  using vec = vector<T>;
  using type = dynamic_fws_multimap_base<V, K, vec, LogMaxBucketSize>;
};

template <typename K, typename V, std::size_t LogMaxBucketSize = 20U>
using mutable_fws_multimap =
    typename mutable_multimap_helper<K, V, LogMaxBucketSize>::type;

}  // namespace raw

}  // namespace cista

#include <cinttypes>
#include <string_view>


namespace cista {
template <typename DataVec, typename IndexVec, typename SizeType>
struct const_bucket final {
  using size_type = SizeType;
  using index_value_type = typename IndexVec::value_type;
  using data_value_type = typename DataVec::value_type;

  using value_type = data_value_type;
  using iterator = typename DataVec::const_iterator;
  using const_iterator = typename DataVec::const_iterator;
  using reference = typename DataVec::reference;
  using const_reference = typename DataVec::const_reference;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using pointer = std::add_pointer_t<value_type>;

  const_bucket(DataVec const* data, IndexVec const* index,
               index_value_type const i)
      : data_{data}, index_{index}, i_{to_idx(i)} {}

  friend data_value_type* data(const_bucket b) { return &b[0]; }
  friend index_value_type size(const_bucket b) { return b.size(); }

  template <typename T = std::decay_t<data_value_type>,
            typename = std::enable_if_t<std::is_same_v<T, char>>>
  std::string_view view() const {
    return std::string_view{begin(), size()};
  }

  value_type const& front() const {
    assert(!empty());
    return operator[](0);
  }

  value_type const& back() const {
    assert(!empty());
    return operator[](size() - 1U);
  }

  bool empty() const { return begin() == end(); }

  value_type const& at(std::size_t const i) const {
    verify(i < size(), "bucket::at: index out of range");
    return *(begin() + i);
  }

  value_type const& operator[](std::size_t const i) const {
    assert(is_inside_bucket(i));
    return data()[index()[i_] + i];
  }

  const_bucket operator*() const { return *this; }

  std::size_t size() const { return bucket_end_idx() - bucket_begin_idx(); }

  const_iterator begin() const { return data().begin() + bucket_begin_idx(); }
  const_iterator end() const { return data().begin() + bucket_end_idx(); }

  friend const_iterator begin(const_bucket const& b) { return b.begin(); }
  friend const_iterator end(const_bucket const& b) { return b.end(); }

  friend bool operator==(const_bucket const& a, const_bucket const& b) {
    assert(a.data_ == b.data_);
    assert(a.index_ == b.index_);
    return a.i_ == b.i_;
  }
  friend bool operator!=(const_bucket const& a, const_bucket const& b) {
    assert(a.data_ == b.data_);
    assert(a.index_ == b.index_);
    return a.i_ != b.i_;
  }
  const_bucket& operator++() {
    ++i_;
    return *this;
  }
  const_bucket& operator--() {
    --i_;
    return *this;
  }
  const_bucket operator*() { return *this; }
  const_bucket& operator+=(difference_type const n) {
    i_ += n;
    return *this;
  }
  const_bucket& operator-=(difference_type const n) {
    i_ -= n;
    return *this;
  }
  const_bucket operator+(difference_type const n) const {
    auto tmp = *this;
    tmp += n;
    return tmp;
  }
  const_bucket operator-(difference_type const n) const {
    auto tmp = *this;
    tmp -= n;
    return tmp;
  }
  friend difference_type operator-(const_bucket const& a,
                                   const_bucket const& b) {
    assert(a.data_ == b.data_);
    assert(a.index_ == b.index_);
    return a.i_ - b.i_;
  }

private:
  DataVec const& data() const { return *data_; }
  IndexVec const& index() const { return *index_; }

  std::size_t bucket_begin_idx() const { return to_idx(index()[i_]); }
  std::size_t bucket_end_idx() const { return to_idx(index()[i_ + 1U]); }
  bool is_inside_bucket(std::size_t const i) const {
    return bucket_begin_idx() + i < bucket_end_idx();
  }

  DataVec const* data_;
  IndexVec const* index_;
  size_type i_;
};

template <typename DataVec, typename IndexVec, typename SizeType>
struct bucket final {
  using size_type = SizeType;
  using index_value_type = typename IndexVec::value_type;
  using data_value_type = typename DataVec::value_type;

  using value_type = data_value_type;
  using iterator = typename DataVec::iterator;
  using const_iterator = typename DataVec::iterator;
  using reference = typename DataVec::reference;
  using const_reference = typename DataVec::const_reference;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using pointer = std::add_pointer_t<value_type>;

  bucket(DataVec* data, IndexVec* index, index_value_type const i)
      : data_{data}, index_{index}, i_{to_idx(i)} {}

  friend data_value_type* data(bucket b) { return &b[0]; }
  friend index_value_type size(bucket b) { return b.size(); }

  template <typename T = std::decay_t<data_value_type>,
            typename = std::enable_if_t<std::is_same_v<T, char>>>
  std::string_view view() const {
    return std::string_view{begin(), size()};
  }

  value_type& front() {
    assert(!empty());
    return operator[](0);
  }

  value_type& back() {
    assert(!empty());
    return operator[](size() - 1U);
  }

  bool empty() const { return begin() == end(); }

  value_type& operator[](std::size_t const i) {
    assert(is_inside_bucket(i));
    return data()[to_idx(index()[i_] + i)];
  }

  value_type const& operator[](std::size_t const i) const {
    assert(is_inside_bucket(i));
    return data()[to_idx(index()[i_] + i)];
  }

  value_type const& at(std::size_t const i) const {
    verify(i < size(), "bucket::at: index out of range");
    return *(begin() + i);
  }

  value_type& at(std::size_t const i) {
    verify(i < size(), "bucket::at: index out of range");
    return *(begin() + i);
  }

  reference operator*() const { return *this; }

  operator const_bucket<DataVec, IndexVec, SizeType>() const {
    return {data_, index_, i_};
  }

  index_value_type size() const {
    return bucket_end_idx() - bucket_begin_idx();
  }
  iterator begin() { return data().begin() + bucket_begin_idx(); }
  iterator end() { return data().begin() + bucket_end_idx(); }
  const_iterator begin() const { return data().begin() + bucket_begin_idx(); }
  const_iterator end() const { return data().begin() + bucket_end_idx(); }
  friend iterator begin(bucket const& b) { return b.begin(); }
  friend iterator end(bucket const& b) { return b.end(); }
  friend iterator begin(bucket& b) { return b.begin(); }
  friend iterator end(bucket& b) { return b.end(); }

  friend bool operator==(bucket const& a, bucket const& b) {
    assert(a.data_ == b.data_);
    assert(a.index_ == b.index_);
    return a.i_ == b.i_;
  }
  friend bool operator!=(bucket const& a, bucket const& b) {
    assert(a.data_ == b.data_);
    assert(a.index_ == b.index_);
    return a.i_ != b.i_;
  }
  bucket& operator++() {
    ++i_;
    return *this;
  }
  bucket& operator--() {
    --i_;
    return *this;
  }
  bucket operator*() { return *this; }
  bucket& operator+=(difference_type const n) {
    i_ += n;
    return *this;
  }
  bucket& operator-=(difference_type const n) {
    i_ -= n;
    return *this;
  }
  bucket operator+(difference_type const n) const {
    auto tmp = *this;
    tmp += n;
    return tmp;
  }
  bucket operator-(difference_type const n) const {
    auto tmp = *this;
    tmp -= n;
    return tmp;
  }
  friend difference_type operator-(bucket const& a, bucket const& b) {
    assert(a.data_ == b.data_);
    assert(a.index_ == b.index_);
    return a.i_ - b.i_;
  }

private:
  DataVec& data() const { return *data_; }
  IndexVec& index() const { return *index_; }

  index_value_type bucket_begin_idx() const { return to_idx(index()[i_]); }
  index_value_type bucket_end_idx() const { return to_idx(index()[i_ + 1U]); }
  bool is_inside_bucket(std::size_t const i) const {
    return bucket_begin_idx() + i < bucket_end_idx();
  }

  size_type i_;
  DataVec* data_;
  IndexVec* index_;
};

template <std::size_t Depth, typename DataVec, typename IndexVec,
          typename SizeType>
struct const_meta_bucket {
  using index_value_type = typename IndexVec::value_type;

  using const_iterator = std::conditional_t<
      Depth == 1U, const_bucket<DataVec, IndexVec, SizeType>,
      const_meta_bucket<Depth - 1U, DataVec, IndexVec, SizeType>>;
  using iterator = const_iterator;
  using difference_type = std::ptrdiff_t;
  using value_type = const_iterator;
  using pointer = void;
  using reference = const_meta_bucket;
  using const_reference = const_meta_bucket;
  using iterator_category = std::random_access_iterator_tag;
  using size_type = SizeType;

  const_meta_bucket(DataVec const* data, IndexVec const* index,
                    index_value_type const i)
      : data_{data}, index_{index}, i_{i} {}

  index_value_type size() const { return index()[i_ + 1U] - index()[i_]; }

  iterator begin() const { return {data_, index_ - 1U, index()[i_]}; }
  const_iterator end() const { return {data_, index_ - 1U, index()[i_ + 1U]}; }

  friend iterator begin(const_meta_bucket const& b) { return b.begin(); }
  friend iterator end(const_meta_bucket const& b) { return b.end(); }

  reference operator*() const { return *this; }

  iterator operator[](size_type const i) { return begin() + i; }

  const_meta_bucket& operator++() {
    ++i_;
    return *this;
  }
  const_meta_bucket& operator--() {
    --i_;
    return *this;
  }
  const_meta_bucket& operator+=(difference_type const n) {
    i_ += n;
    return *this;
  }
  const_meta_bucket& operator-=(difference_type const n) {
    i_ -= n;
    return *this;
  }
  const_meta_bucket operator+(difference_type const n) const {
    auto tmp = *this;
    tmp += n;
    return tmp;
  }
  const_meta_bucket operator-(difference_type const n) const {
    auto tmp = *this;
    tmp -= n;
    return tmp;
  }

  friend bool operator==(const_meta_bucket const& a,
                         const_meta_bucket const& b) {
    return a.i_ == b.i_;
  }

  friend bool operator!=(const_meta_bucket const& a,
                         const_meta_bucket const& b) {
    return !(a == b);
  }

private:
  IndexVec const& index() const { return *index_; }
  DataVec const& data() const { return *data_; }

  DataVec const* data_;
  IndexVec const* index_;
  index_value_type i_;
};

template <std::size_t Depth, typename DataVec, typename IndexVec,
          typename SizeType>
struct meta_bucket {
  using index_value_type = typename IndexVec::value_type;

  using iterator =
      std::conditional_t<Depth == 1U, bucket<DataVec, IndexVec, SizeType>,
                         meta_bucket<Depth - 1U, DataVec, IndexVec, SizeType>>;
  using const_iterator = std::conditional_t<
      Depth == 1U, const_bucket<DataVec, IndexVec, SizeType>,
      const_meta_bucket<Depth - 1U, DataVec, IndexVec, SizeType>>;

  using iterator_category = std::random_access_iterator_tag;
  using reference = meta_bucket;
  using const_reference = const_meta_bucket<Depth, DataVec, IndexVec, SizeType>;
  using difference_type = std::ptrdiff_t;
  using size_type = SizeType;

  meta_bucket(DataVec* data, IndexVec* index, index_value_type const i)
      : data_{data}, index_{index}, i_{i} {}

  index_value_type size() const { return index()[i_ + 1U] - index()[i_]; }

  iterator begin() { return {data_, index_ - 1U, index()[i_]}; }
  iterator end() { return {data_, index_ - 1U, index()[i_ + 1U]}; }

  const_iterator begin() const { return {data_, index_ - 1U, index()[i_]}; }
  const_iterator end() const { return {data_, index_ - 1U, index()[i_ + 1U]}; }

  friend iterator begin(meta_bucket& b) { return b.begin(); }
  friend iterator end(meta_bucket& b) { return b.end(); }

  friend const_iterator begin(meta_bucket const& b) { return b.begin(); }
  friend const_iterator end(meta_bucket const& b) { return b.end(); }

  const_reference operator*() const { return {data_, index_, i_}; }
  reference operator*() { return *this; }

  iterator operator[](size_type const i) { return begin() + i; }
  const_iterator operator[](size_type const i) const { return begin() + i; }

  operator const_meta_bucket<Depth, DataVec, IndexVec, SizeType>() const {
    return {data_, index_, i_};
  }

  meta_bucket& operator++() {
    ++i_;
    return *this;
  }

  meta_bucket& operator--() {
    --i_;
    return *this;
  }
  meta_bucket& operator+=(difference_type const n) {
    i_ += n;
    return *this;
  }
  meta_bucket& operator-=(difference_type const n) {
    i_ -= n;
    return *this;
  }
  meta_bucket operator+(difference_type const n) const {
    auto tmp = *this;
    tmp += n;
    return tmp;
  }
  meta_bucket operator-(difference_type const n) const {
    auto tmp = *this;
    tmp -= n;
    return tmp;
  }

  friend bool operator==(meta_bucket const& a, meta_bucket const& b) {
    return a.i_ == b.i_;
  }

  friend bool operator!=(meta_bucket const& a, meta_bucket const& b) {
    return !(a == b);
  }

private:
  IndexVec& index() const { return *index_; }
  DataVec& data() const { return *data_; }

  DataVec* data_;
  IndexVec* index_;
  index_value_type i_;
};

template <typename Key, typename DataVec, typename IndexVec, std::size_t N,
          typename SizeType = std::uint32_t>
struct basic_nvec {
  static_assert(std::is_same_v<typename IndexVec::value_type, base_t<Key>>);

  using data_vec_t = DataVec;
  using index_vec_t = IndexVec;
  using size_type = SizeType;
  using data_value_type = typename DataVec::value_type;
  using index_value_type = typename IndexVec::value_type;

  using bucket_t = bucket<DataVec, IndexVec, SizeType>;
  using const_bucket_t = const_bucket<DataVec, IndexVec, SizeType>;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using iterator = meta_bucket<N - 1U, DataVec, IndexVec, SizeType>;
  using const_iterator = const_meta_bucket<N - 1U, DataVec, IndexVec, SizeType>;
  using reference = iterator;
  using const_reference = const_iterator;
  using value_type = iterator;

  basic_nvec() {
    for (auto& i : index_) {
      i.push_back(0U);
    }
  }

  iterator begin() { return {&data_, &index_.back(), 0U}; }
  iterator end() { return {&data_, &index_.back(), size()}; }

  const_iterator begin() const { return {&data_, &index_.back(), 0U}; }
  const_iterator end() const { return {&data_, &index_.back(), size()}; }

  iterator front() { return begin(); }
  iterator back() { return begin() + size() - 1; }

  const_iterator front() const { return begin(); }
  const_iterator back() const { return begin() + size() - 1; }

  template <typename Container>
  void emplace_back(Container&& bucket) {
    if (index_[0].size() == 0U) {
      for (auto& i : index_) {
        i.push_back(0U);
      }
    }
    add<N - 1>(bucket);
  }

  iterator operator[](Key const k) { return begin() + to_idx(k); }
  const_iterator operator[](Key const k) const { return begin() + to_idx(k); }

  size_type size() const { return index_[N - 1].size() - 1U; }

  template <typename... Indices>
  size_type size(Key const first, Indices... rest) const {
    constexpr auto const I = sizeof...(Indices);
    verify(to_idx(first) < index_[I].size(), "nvec::at: index out of range");
    if (sizeof...(Indices) == 0U) {
      return get_size<N - sizeof...(Indices) - 1>(first);
    } else {
      return get_size<N - sizeof...(Indices) - 1>(index_[I][first], rest...);
    }
  }

  template <typename... Indices>
  bucket_t at(Key const first, Indices... rest) {
    constexpr auto const I = sizeof...(Indices);
    static_assert(I == N - 1);
    verify(to_idx(first) < index_[I].size(), "nvec::at: index out of range");
    return get_bucket(index_[I][first], rest...);
  }

  template <typename... Indices>
  const_bucket_t at(Key const first, Indices... rest) const {
    constexpr auto const I = sizeof...(Indices);
    static_assert(I == N - 1);
    verify(to_idx(first) < index_[I].size(), "nvec::at: index out of range");
    return get_bucket(index_[I][first], rest...);
  }

  auto cista_members() noexcept { return std::tie(index_, data_); }

  template <typename... Rest>
  bucket_t get_bucket(index_value_type const bucket_start,
                      index_value_type const i, Rest... rest) {
    return get_bucket<Rest...>(index_[sizeof...(Rest)][bucket_start + i],
                               rest...);
  }

  bucket_t get_bucket(index_value_type const bucket_start,
                      index_value_type const i) {
    return {&data_, &index_[0], bucket_start + i};
  }

  template <typename... Rest>
  const_bucket_t get_bucket(index_value_type const bucket_start,
                            index_value_type const i, Rest... rest) const {
    return get_bucket<Rest...>(index_[sizeof...(Rest)][bucket_start + i],
                               rest...);
  }

  const_bucket_t get_bucket(index_value_type const bucket_start,
                            index_value_type const i) const {
    return {&data_, &index_[0], bucket_start + i};
  }

  template <std::size_t L, typename Container>
  void add(Container&& c) {
    if constexpr (L == 0) {
      index_[0].push_back(static_cast<size_type>(data_.size() + c.size()));
      data_.insert(std::end(data_), std::make_move_iterator(std::begin(c)),
                   std::make_move_iterator(std::end(c)));
    } else {
      index_[L].push_back(
          static_cast<size_type>(index_[L - 1].size() + c.size() - 1U));
      for (auto&& x : c) {
        add<L - 1>(x);
      }
    }
  }

  template <std::size_t L, typename... Rest>
  size_type get_size(index_value_type const i, index_value_type const j,
                     Rest... rest) const {
    if constexpr (sizeof...(Rest) == 0U) {
      return index_[L][i + j + 1] - index_[L][i + j];
    } else {
      return get_size<L>(index_[L][i + j], rest...);
    }
  }

  template <std::size_t L>
  size_type get_size(index_value_type const i) const {
    return index_[L][i + 1] - index_[L][i];
  }

  array<IndexVec, N> index_;
  DataVec data_;
};

namespace offset {

template <typename K, typename V, std::size_t N,
          typename SizeType = std::uint32_t>
using nvec = basic_nvec<K, vector<V>, vector<base_t<K>>, N, SizeType>;

}  // namespace offset

namespace raw {

template <typename K, typename V, std::size_t N,
          typename SizeType = std::uint32_t>
using nvec = basic_nvec<K, vector<V>, vector<base_t<K>>, N, SizeType>;

}  // namespace raw

}  // namespace cista

#include <optional>
#include <type_traits>
#include <utility>


namespace cista {

template <typename T>
struct optional {
  optional() = default;

  template <typename... Ts>
  optional(Ts&&... t) noexcept(std::is_nothrow_constructible_v<T, Ts...>) {
    new (&storage_[0]) T{std::forward<Ts>(t)...};
    valid_ = true;
  }

  template <typename... Ts>
  optional(std::nullopt_t) noexcept {}

  optional(optional const& other) { copy(other); }
  optional& operator=(optional const& other) { return copy(other); }

  optional(optional&& other) noexcept { move(std::forward<optional>(other)); }
  optional& operator=(optional&& other) noexcept {
    return move(std::forward<optional>(other));
  }

  optional& copy(optional const& other) {
    if (other.has_value()) {
      new (&storage_[0]) T{other.value()};
    }

    valid_ = other.has_value();

    return *this;
  }

  optional& move(optional&& other) {
    if (other.has_value()) {
      new (&storage_[0]) T(std::move(other.value()));
    }

    valid_ = other.has_value();

    return *this;
  }

  T const& value() const {
    if (!valid_) {
      throw_exception(std::bad_optional_access{});
    }
    return *reinterpret_cast<T const*>(&storage_[0]);
  }

  T& value() {
    if (!valid_) {
      throw_exception(std::bad_optional_access{});
    }
    return *reinterpret_cast<T*>(&storage_[0]);
  }

  bool has_value() const noexcept { return valid_; }

  T* operator->() noexcept { return reinterpret_cast<T*>(&storage_[0]); }
  T const* operator->() const noexcept {
    return reinterpret_cast<T const*>(&storage_[0]);
  }

  T& operator*() noexcept { return *reinterpret_cast<T*>(&storage_[0]); }
  T const& operator*() const noexcept {
    return *reinterpret_cast<T const*>(&storage_[0]);
  }

  bool valid_{false};
  alignas(std::alignment_of_v<T>) unsigned char storage_[sizeof(T)];
};

}  // namespace cista

#include <cinttypes>
#include <cstring>
#include <limits>


namespace cista {

template <typename SizeType>
struct page {
  bool valid() const { return capacity_ != 0U; }
  SizeType size() const noexcept { return size_; }

  SizeType size_{0U};
  SizeType capacity_{0U};
  SizeType start_{0U};
};

template <typename DataVec, typename SizeType = typename DataVec::size_type,
          SizeType MinPageSize = next_power_of_two(3U * sizeof(SizeType)),
          SizeType MaxPageSize = 65536U>
struct paged {
  using value_type = typename DataVec::value_type;
  using iterator = typename DataVec::iterator;
  using const_iterator = typename DataVec::const_iterator;
  using reference = typename DataVec::reference;
  using const_reference = typename DataVec::const_reference;
  using size_type = SizeType;
  using page_t = page<SizeType>;

  static_assert(sizeof(value_type) * MinPageSize >= sizeof(page_t));
  static_assert(std::is_trivially_copyable_v<value_type>);

  static constexpr size_type free_list_index(size_type const capacity) {
    return static_cast<size_type>(constexpr_trailing_zeros(capacity) -
                                  constexpr_trailing_zeros(MinPageSize));
  }

  static constexpr size_type free_list_size = free_list_index(MaxPageSize) + 1U;

  page_t resize_page(page_t const& p, size_type const size) {
    if (size <= p.capacity_) {
      return {size, p.capacity_, p.start_};
    } else {
      auto const new_page = create_page(size);
      copy(new_page, p);
      free_page(p);
      return new_page;
    }
  }

  page_t create_page(size_type const size) {
    auto const capacity = next_power_of_two(std::max(MinPageSize, size));
    auto const i = free_list_index(capacity);
    verify(i < free_list_.size(), "paged::create_page: size > max capacity");
    if (!free_list_[i].empty()) {
      auto start = free_list_[i].pop(*this);
      return {size, capacity, start};
    } else {
      auto const start = data_.size();
      data_.resize(data_.size() + capacity);
      return {size, capacity, start};
    }
  }

  void free_page(page_t const& p) {
    if (!p.valid()) {
      return;
    }
    auto const i = free_list_index(p.capacity_);
    verify(i < free_list_.size(), "paged::free_page: size > max capacity");
    free_list_[i].push(*this, p.start_);
  }

  template <typename T>
  T read(size_type const offset) {
    static_assert(std::is_trivially_copyable_v<T>);
    auto x = T{};
    std::memcpy(&x, &data_[offset], sizeof(x));
    return x;
  }

  template <typename T>
  void write(size_type const offset, T const& x) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(&data_[offset], &x, sizeof(T));
  }

  value_type* data(page_t const& p) { return &data_[p.start_]; }
  value_type const* data(page_t const& p) const { return &data_[p.start_]; }

  value_type* begin(page_t const& p) { return data(p); }
  value_type const* begin(page_t const& p) const { return data(p); }

  value_type* end(page_t& p) const { return begin(p) + p.size(); }
  value_type const* end(page_t const& p) const { return begin() + p.size; }

  void copy(page_t const& to, page_t const& from) {
    std::memcpy(data(to), data(from), from.size() * sizeof(value_type));
  }

  template <typename ItA, typename ItB>
  void copy(page_t const& to, ItA begin, ItB end) {
    std::memcpy(data(to), &*begin,
                static_cast<std::size_t>(std::distance(begin, end)));
  }

  struct node {
    bool empty() const {
      return next_ == std::numeric_limits<size_type>::max();
    }

    void push(paged& m, size_type const start) {
      m.write(start, next_);
      next_ = start;
    }

    size_type pop(paged& m) {
      verify(!empty(), "paged: invalid read access to empty free list entry");
      auto const next_start = m.read<size_type>(next_);
      auto start = next_;
      next_ = next_start;
      return start;
    }

    size_type next_{std::numeric_limits<size_type>::max()};
  };

  DataVec data_;
  array<node, free_list_size> free_list_{};
};

}  // namespace cista


namespace cista {

template <typename Index, typename Paged, typename Key>
struct paged_vecvec {
  using index_t = Index;
  using data_t = Paged;

  using page_t = typename Paged::page_t;
  using size_type = typename Paged::size_type;
  using data_value_type = typename Paged::value_type;

  struct const_bucket final {
    using size_type = typename Paged::size_type;
    using data_value_type = typename Paged::value_type;

    using value_type = data_value_type;
    using iterator = typename Paged::const_iterator;
    using const_iterator = typename Paged::const_iterator;
    using reference = typename Paged::const_reference;
    using const_reference = typename Paged::const_reference;

    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;

    const_bucket(paged_vecvec const* pv, Key const i) : pv_{pv}, i_{i} {}

    template <typename T = std::decay_t<data_value_type>,
              typename = std::enable_if_t<std::is_same_v<T, char>>>
    std::string_view view() const {
      return std::string_view{begin(), size()};
    }

    const_iterator begin() const { return pv_->data(i_); }
    const_iterator end() const { return pv_->data(i_) + size(); }
    friend const_iterator begin(const_bucket const& b) { return b.begin(); }
    friend const_iterator end(const_bucket const& b) { return b.end(); }

    value_type const& operator[](std::size_t const i) const {
      assert(i < size());
      return *(begin() + i);
    }

    value_type const& at(std::size_t const i) const {
      verify(i < size(), "paged_vecvec: const_bucket::at: index out of range");
      return *(begin() + i);
    }

    value_type& at(std::size_t const i) {
      verify(i < size(), "paged_vecvec: const_bucket::at: index out of range");
      return *(begin() + i);
    }

    reference operator*() const { return *this; }

    size_type size() const { return pv_->page(i_).size_; }

    friend bool operator==(const_bucket const& a, const_bucket const& b) {
      assert(a.pv_ == b.pv_);
      return a.i_ == b.i_;
    }

    friend bool operator!=(const_bucket const& a, const_bucket const& b) {
      assert(a.pv_ == b.pv_);
      return a.i_ != b.i_;
    }

    const_bucket& operator++() {
      ++i_;
      return *this;
    }
    const_bucket& operator--() {
      --i_;
      return *this;
    }
    const_bucket operator*() { return *this; }
    const_bucket& operator+=(difference_type const n) {
      i_ += n;
      return *this;
    }
    const_bucket& operator-=(difference_type const n) {
      i_ -= n;
      return *this;
    }
    const_bucket operator+(difference_type const n) const {
      auto tmp = *this;
      tmp += n;
      return tmp;
    }
    const_bucket operator-(difference_type const n) const {
      auto tmp = *this;
      tmp -= n;
      return tmp;
    }
    friend difference_type operator-(const_bucket const& a,
                                     const_bucket const& b) {
      assert(a.pv_ == b.pv_);
      return a.i_ - b.i_;
    }

  private:
    paged_vecvec const* pv_;
    Key i_;
  };

  struct bucket final {
    using size_type = typename Paged::size_type;
    using index_value_type = typename Paged::page_t;
    using data_value_type = typename Paged::value_type;

    using value_type = data_value_type;
    using iterator = typename Paged::iterator;
    using const_iterator = typename Paged::iterator;
    using reference = typename Paged::reference;
    using const_reference = typename Paged::const_reference;

    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;

    bucket(paged_vecvec* pv, Key const i) : pv_{pv}, i_{i} {}

    void push_back(data_value_type const& x) {
      auto& p = pv_->page(i_);
      p = pv_->paged_.resize_page(p, p.size_ + 1U);
      (*this)[size() - 1U] = x;
    }

    template <typename T = std::decay_t<data_value_type>,
              typename = std::enable_if_t<std::is_same_v<T, char>>>
    std::string_view view() const {
      return std::string_view{begin(), static_cast<std::size_t>(size())};
    }

    iterator begin() { return pv_->data(i_); }
    iterator end() { return pv_->data(i_) + size(); }
    const_iterator begin() const { return pv_->data(i_); }
    const_iterator end() const { return pv_->data(i_) + size(); }
    friend iterator begin(bucket const& b) { return b.begin(); }
    friend iterator end(bucket const& b) { return b.end(); }
    friend iterator begin(bucket& b) { return b.begin(); }
    friend iterator end(bucket& b) { return b.end(); }

    value_type& operator[](std::size_t const i) {
      assert(i < size());
      return *(begin() + i);
    }

    value_type const& operator[](std::size_t const i) const {
      assert(i < size());
      return *(begin() + i);
    }

    value_type const& at(std::size_t const i) const {
      verify(i < size(), "bucket::at: index out of range");
      return *(begin() + i);
    }

    value_type& at(std::size_t const i) {
      verify(i < size(), "bucket::at: index out of range");
      return *(begin() + i);
    }

    reference operator*() const { return *this; }

    operator const_bucket() const { return {pv_, i_}; }

    size_type size() const { return pv_->page(i_).size_; }

    friend bool operator==(bucket const& a, bucket const& b) {
      assert(a.pv_ == b.pv_);
      return a.i_ == b.i_;
    }

    friend bool operator!=(bucket const& a, bucket const& b) {
      assert(a.pv_ == b.pv_);
      return a.i_ != b.i_;
    }

    bucket& operator++() {
      ++i_;
      return *this;
    }
    bucket& operator--() {
      --i_;
      return *this;
    }
    bucket operator*() { return *this; }
    bucket& operator+=(difference_type const n) {
      i_ += n;
      return *this;
    }
    bucket& operator-=(difference_type const n) {
      i_ -= n;
      return *this;
    }
    bucket operator+(difference_type const n) const {
      auto tmp = *this;
      tmp += n;
      return tmp;
    }
    bucket operator-(difference_type const n) const {
      auto tmp = *this;
      tmp -= n;
      return tmp;
    }
    friend difference_type operator-(bucket const& a, bucket const& b) {
      assert(a.pv_ == b.pv_);
      return a.i_ - b.i_;
    }

  private:
    page_t page() { return pv_->page(i_); }
    pointer data() { pv_->data(i_); }

    std::size_t bucket_begin_idx() const { return page().start_; }
    std::size_t bucket_end_idx() const {
      auto const p = page();
      return p.start_ + p.size_;
    }
    bool is_inside_bucket(std::size_t const i) const {
      return bucket_begin_idx() + i < bucket_end_idx();
    }

    paged_vecvec* pv_;
    Key i_;
  };

  using value_type = bucket;
  using iterator = bucket;
  using const_iterator = bucket;

  bucket operator[](Key const i) { return {this, i}; }
  const_bucket operator[](Key const i) const { return {this, i}; }

  page_t& page(Key const i) { return idx_[to_idx(i)]; }
  page_t const& page(Key const i) const { return idx_[to_idx(i)]; }

  data_value_type const* data(Key const i) const {
    return data(idx_[to_idx(i)]);
  }
  data_value_type* data(Key const i) { return data(idx_[to_idx(i)]); }
  data_value_type const* data(page_t const& p) const { return paged_.data(p); }
  data_value_type* data(page_t const& p) { return paged_.data(p); }

  const_bucket at(Key const i) const {
    verify(to_idx(i) < idx_.size(), "paged_vecvec::at: index out of range");
    return operator[](i);
  }

  bucket at(Key const i) {
    verify(to_idx(i) < idx_.size(), "paged_vecvec::at: index out of range");
    return operator[](i);
  }

  bucket front() { return at(Key{0}); }
  bucket back() { return at(Key{size() - 1}); }

  const_bucket front() const { return at(Key{0}); }
  const_bucket back() const { return at(Key{size() - 1}); }

  size_type size() const { return idx_.size(); }
  bool empty() const { return idx_.empty(); }

  bucket begin() { return front(); }
  bucket end() { return operator[](Key{size()}); }
  const_bucket begin() const { return front(); }
  const_bucket end() const { return operator[](Key{size()}); }

  friend bucket begin(paged_vecvec& m) { return m.begin(); }
  friend bucket end(paged_vecvec& m) { return m.end(); }
  friend const_bucket begin(paged_vecvec const& m) { return m.begin(); }
  friend const_bucket end(paged_vecvec const& m) { return m.end(); }

  template <typename Container,
            typename = std::enable_if_t<std::is_convertible_v<
                decltype(*std::declval<Container>().begin()), data_value_type>>>
  void emplace_back(Container&& bucket) {
    auto p = paged_.create_page(static_cast<size_type>(bucket.size()));
    paged_.copy(p, std::begin(bucket), std::end(bucket));
    idx_.emplace_back(p);
  }

  template <typename T = data_value_type,
            typename = std::enable_if_t<std::is_convertible_v<T, char const>>>
  void emplace_back(char const* s) {
    return emplace_back(std::string_view{s});
  }

  void resize(size_type const size) {
    for (auto i = size; i < idx_.size(); ++i) {
      paged_.free_page(idx_[i]);
    }
    idx_.resize(size);
  }

  Paged paged_;
  Index idx_;
};

}  // namespace cista

#include <cinttypes>
#include <type_traits>
#include <utility>


namespace cista {

template <typename... Ts>
struct tuple;

template <std::size_t I, typename... Ts>
struct tuple_element;

template <std::size_t I, typename T, typename... Ts>
struct tuple_element<I, tuple<T, Ts...>> : tuple_element<I - 1U, tuple<Ts...>> {
};

template <typename T, typename... Ts>
struct tuple_element<0U, tuple<T, Ts...>> {
  using type = T;
};

template <std::size_t I, typename T, typename... Ts>
struct tuple_element<I, T, Ts...> : tuple_element<I - 1U, Ts...> {};

template <typename T, typename... Ts>
struct tuple_element<0U, T, Ts...> {
  using type = T;
};

template <std::size_t I, typename... Ts>
using tuple_element_t = typename tuple_element<I, Ts...>::type;

template <typename T, typename... Ts>
constexpr std::size_t max_align_of() {
  if constexpr (sizeof...(Ts) == 0U) {
    return alignof(T);
  } else {
    return std::max(alignof(T), max_align_of<Ts...>());
  }
}

template <typename T, typename... Ts>
constexpr std::size_t get_offset(std::size_t const current_idx,
                                 std::size_t current_offset = 0U) {
  if (auto const misalign = current_offset % alignof(T); misalign != 0U) {
    current_offset += (alignof(T) - misalign) % alignof(T);
  }

  if (current_idx == 0U) {
    return current_offset;
  }

  current_offset += sizeof(T);

  if constexpr (sizeof...(Ts) == 0U) {
    return current_idx == 1 ? current_offset + sizeof(T) : current_offset;
  } else {
    return get_offset<Ts...>(current_idx - 1U, current_offset);
  }
}

template <typename... Ts>
constexpr std::size_t get_total_size() {
  return get_offset<Ts...>(sizeof...(Ts) + 1U);
}

template <std::size_t I, typename T, typename... Ts>
constexpr auto get_arg(T&& arg, Ts&&... args) {
  if constexpr (I == 0U) {
    return std::forward<T>(arg);
  } else {
    return get_arg<I - 1U>(std::forward<Ts>(args)...);
  }
}

template <std::size_t I, typename... Ts>
auto& get(tuple<Ts...>& t) {
  using return_t = tuple_element_t<I, Ts...>;
  return *std::launder(reinterpret_cast<return_t*>(t.template get_ptr<I>()));
}

template <std::size_t I, typename... Ts>
auto const& get(tuple<Ts...> const& t) {
  using return_t = tuple_element_t<I, Ts...>;
  return *std::launder(
      reinterpret_cast<return_t const*>(t.template get_ptr<I>()));
}

template <std::size_t I, typename... Ts>
auto& get(tuple<Ts...>&& t) {
  using return_t = tuple_element_t<I, Ts...>;
  return *std::launder(reinterpret_cast<return_t*>(t.template get_ptr<I>()));
}

template <typename... Ts>
struct tuple {
  template <std::size_t... Is>
  using seq_t = std::index_sequence<Is...>;
  static constexpr auto Indices = std::make_index_sequence<sizeof...(Ts)>{};

  tuple() { default_construct(Indices); }

  tuple(tuple const& o) { copy_construct(o, Indices); }

  tuple& operator=(tuple const& o) {
    if (&o != this) {
      copy_assign(o, Indices);
    }

    return *this;
  }

  tuple(tuple&& o) noexcept { move_construct(o, Indices); }

  tuple& operator=(tuple&& o) noexcept {
    move_assign(o, Indices);
    return *this;
  }

  tuple(Ts&&... args) {  // NOLINT
    move_construct_from_args(Indices, std::forward<Ts>(args)...);
  }

  ~tuple() { destruct(Indices); }

  template <std::size_t... Is>
  constexpr void move_construct_from_args(seq_t<Is...>, Ts&&... args) {
    (new (get_ptr<Is>())
         tuple_element_t<Is, Ts...>(get_arg<Is>(std::forward<Ts>(args)...)),
     ...);
  }

  template <std::size_t... Is>
  constexpr void default_construct(seq_t<Is...>) {
    ((new (get_ptr<Is>()) tuple_element_t<Is, Ts...>{}), ...);
  }

  template <typename From, std::size_t... Is>
  constexpr void copy_construct(From const& from, seq_t<Is...>) {
    (new (get_ptr<Is>()) tuple_element_t<Is, Ts...>(get<Is>(from)), ...);
  }

  template <typename From, std::size_t... Is>
  constexpr void copy_assign(From const& from, seq_t<Is...>) {
    ((get<Is>(*this) = get<Is>(from)), ...);
  }

  template <typename From, std::size_t... Is>
  constexpr void move_construct(From&& from, seq_t<Is...>) {
    (new (get_ptr<Is>()) tuple_element_t<Is, Ts...>(std::move(get<Is>(from))),
     ...);
  }

  template <typename From, std::size_t... Is>
  constexpr void move_assign(From&& from, seq_t<Is...>) {
    ((get<Is>(*this) = std::move(get<Is>(from))), ...);
  }

  template <typename T>
  constexpr void destruct(T& t) {
    static_assert(!std::is_array_v<T>);
    t.~T();
  }

  template <std::size_t... Is>
  constexpr void destruct(seq_t<Is...>) {
    (destruct(get<Is>(*this)), ...);
  }

  template <std::size_t I>
  constexpr auto get_ptr() {
    return reinterpret_cast<char*>(&mem_) + get_offset<Ts...>(I);
  }

  template <std::size_t I>
  constexpr auto get_ptr() const {
    return reinterpret_cast<char const*>(&mem_) + get_offset<Ts...>(I);
  }

  std::aligned_storage_t<get_total_size<Ts...>(), max_align_of<Ts...>()> mem_;
};

template <typename Head, typename... Tail>
tuple(Head&& first, Tail&&... tail) -> tuple<Head, Tail...>;

template <typename Tuple>
struct is_tuple : std::false_type {};

template <typename... T>
struct is_tuple<tuple<T...>> : std::true_type {};

template <typename T>
inline constexpr auto is_tuple_v = is_tuple<T>::value;

template <typename T>
struct tuple_size;

template <typename... T>
struct tuple_size<tuple<T...>>
    : public std::integral_constant<std::size_t, sizeof...(T)> {};

template <typename T>
constexpr std::size_t tuple_size_v = tuple_size<std::decay_t<T>>::value;

template <typename F, typename Tuple, std::size_t... I>
constexpr decltype(auto) apply_impl(std::index_sequence<I...>, F&& f,
                                    Tuple&& t) {
  return std::invoke(std::forward<F>(f), get<I>(std::forward<Tuple>(t))...);
}

template <typename F, typename Tuple,
          typename Enable = std::enable_if_t<is_tuple_v<std::decay_t<Tuple>>>>
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
  return apply_impl(std::make_index_sequence<tuple_size_v<Tuple>>{},
                    std::forward<F>(f), std::forward<Tuple>(t));
}

template <typename F, typename Tuple, std::size_t... I>
constexpr decltype(auto) apply_impl(std::index_sequence<I...>, F&& f,
                                    Tuple const& a, Tuple const& b) {
  return (std::invoke(std::forward<F>(f), get<I>(std::forward<Tuple>(a)),
                      get<I>(std::forward<Tuple>(b))),
          ...);
}

template <typename F, typename Tuple>
constexpr decltype(auto) apply(F&& f, Tuple const& a, Tuple const& b) {
  return apply_impl(
      std::make_index_sequence<tuple_size_v<std::remove_reference_t<Tuple>>>{},
      std::forward<F>(f), std::forward<Tuple>(a), std::forward<Tuple>(b));
}

template <typename T1, typename T2, std::size_t... I>
constexpr decltype(auto) eq(std::index_sequence<I...>, T1 const& a,
                            T2 const& b) {
  return ((get<I>(a) == get<I>(b)) && ...);
}

template <typename T1, typename T2>
std::enable_if_t<is_tuple_v<decay_t<T1>> && is_tuple_v<decay_t<T2>>, bool>
operator==(T1&& a, T2&& b) {
  return eq(
      std::make_index_sequence<tuple_size_v<std::remove_reference_t<T1>>>{}, a,
      b);
}

template <typename Tuple>
std::enable_if_t<is_tuple_v<decay_t<Tuple>>, bool> operator!=(Tuple const& a,
                                                              Tuple const& b) {
  return !(a == b);
}

template <typename Tuple, std::size_t Index = 0U>
bool lt(Tuple const& a, Tuple const& b) {
  if constexpr (Index == tuple_size_v<Tuple>) {
    return false;
  } else {
    if (get<Index>(a) < get<Index>(b)) {
      return true;
    }
    if (get<Index>(b) < get<Index>(a)) {
      return false;
    }
    return lt<Tuple, Index + 1U>(a, b);
  }
}

template <typename Tuple>
std::enable_if_t<is_tuple_v<decay_t<Tuple>>, bool> operator<(Tuple const& a,
                                                             Tuple const& b) {
  return lt(a, b);
}

template <typename Tuple>
std::enable_if_t<is_tuple_v<decay_t<Tuple>>, bool> operator<=(Tuple const& a,
                                                              Tuple const& b) {
  return !(b < a);
}

template <typename Tuple>
std::enable_if_t<is_tuple_v<decay_t<Tuple>>, bool> operator>(Tuple const& a,
                                                             Tuple const& b) {
  return b < a;
}

template <typename Tuple>
std::enable_if_t<is_tuple_v<decay_t<Tuple>>, bool> operator>=(Tuple const& a,
                                                              Tuple const& b) {
  return !(a < b);
}

}  // namespace cista

namespace std {

template <typename... Pack>
struct tuple_size<cista::tuple<Pack...>>
    : cista::tuple_size<cista::tuple<Pack...>> {};

template <std::size_t I, typename... Pack>
struct tuple_element<I, cista::tuple<Pack...>>
    : cista::tuple_element<I, cista::tuple<Pack...>> {};

}  // namespace std

#include <cinttypes>
#include <cstddef>

#include <utility>


namespace cista {

template <typename T, typename Ptr = T*>
struct basic_unique_ptr {
  using value_t = T;

  basic_unique_ptr() = default;

  explicit basic_unique_ptr(T* el, bool take_ownership = true)
      : el_{el}, self_allocated_{take_ownership} {}

  basic_unique_ptr(basic_unique_ptr const&) = delete;
  basic_unique_ptr& operator=(basic_unique_ptr const&) = delete;

  basic_unique_ptr(basic_unique_ptr&& o) noexcept
      : el_{o.el_}, self_allocated_{o.self_allocated_} {
    o.el_ = nullptr;
    o.self_allocated_ = false;
  }

  basic_unique_ptr& operator=(basic_unique_ptr&& o) noexcept {
    reset();
    el_ = o.el_;
    self_allocated_ = o.self_allocated_;
    o.el_ = nullptr;
    o.self_allocated_ = false;
    return *this;
  }

  basic_unique_ptr(std::nullptr_t) noexcept {}
  basic_unique_ptr& operator=(std::nullptr_t) {
    reset();
    return *this;
  }

  ~basic_unique_ptr() { reset(); }

  void reset() {
    if (self_allocated_ && el_ != nullptr) {
      delete el_;
      el_ = nullptr;
      self_allocated_ = false;
    }
  }

  explicit operator bool() const noexcept { return el_ != nullptr; }

  friend bool operator==(basic_unique_ptr const& a, std::nullptr_t) noexcept {
    return a.el_ == nullptr;
  }
  friend bool operator==(std::nullptr_t, basic_unique_ptr const& a) noexcept {
    return a.el_ == nullptr;
  }
  friend bool operator!=(basic_unique_ptr const& a, std::nullptr_t) noexcept {
    return a.el_ != nullptr;
  }
  friend bool operator!=(std::nullptr_t, basic_unique_ptr const& a) noexcept {
    return a.el_ != nullptr;
  }

  T* get() const noexcept { return el_; }
  T* operator->() noexcept { return el_; }
  T& operator*() noexcept { return *el_; }
  T const& operator*() const noexcept { return *el_; }
  T const* operator->() const noexcept { return el_; }

  Ptr el_{nullptr};
  bool self_allocated_{false};
  std::uint8_t __fill_0__{0U};
  std::uint16_t __fill_1__{0U};
  std::uint32_t __fill_2__{0U};
};

namespace raw {

template <typename T>
using unique_ptr = basic_unique_ptr<T, ptr<T>>;

template <typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) {
  return unique_ptr<T>{new T{std::forward<Args>(args)...}, true};
}

}  // namespace raw

namespace offset {

template <typename T>
using unique_ptr = basic_unique_ptr<T, ptr<T>>;

template <typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) {
  return unique_ptr<T>{new T{std::forward<Args>(args)...}, true};
}

}  // namespace offset

}  // namespace cista

#include <cinttypes>
#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>

// cista/exception.h": basic_ios::clear: iostream error
// cista/hashing.h": basic_ios::clear: iostream error

namespace cista {

template <std::size_t Size, typename Enable = void>
struct bytes_to_integer_type {};

template <std::size_t Size>
struct bytes_to_integer_type<Size, std::enable_if_t<Size == 1U>> {
  using type = std::uint8_t;
};

template <std::size_t Size>
struct bytes_to_integer_type<Size, std::enable_if_t<Size == 2U>> {
  using type = std::uint16_t;
};

template <typename... T>
constexpr std::size_t bytes() noexcept {
  return (sizeof...(T) > std::numeric_limits<std::uint8_t>::max()) ? 2U : 1U;
}

template <typename... T>
using variant_index_t = typename bytes_to_integer_type<bytes<T...>()>::type;

constexpr auto const TYPE_NOT_FOUND = std::numeric_limits<std::size_t>::max();

template <typename Arg, typename... T>
constexpr std::size_t index_of_type() noexcept {
  constexpr std::array<bool, sizeof...(T)> matches = {
      {std::is_same<std::decay_t<Arg>, std::decay_t<T>>::value...}};
  for (std::size_t i = 0U; i < sizeof...(T); ++i) {
    if (matches[i]) {
      return i;
    }
  }
  return TYPE_NOT_FOUND;
}

template <int N, typename... T>
struct type_at_index;

template <typename First, typename... Rest>
struct type_at_index<0, First, Rest...> {
  using type = First;
};

template <int N, typename First, typename... Rest>
struct type_at_index<N, First, Rest...> {
  using type = typename type_at_index<N - 1, Rest...>::type;
};

template <std::size_t Index, typename... T>
using type_at_index_t = typename type_at_index<Index, T...>::type;

template <typename... T>
struct variant {
  using index_t = variant_index_t<T...>;
  static constexpr auto NO_VALUE = std::numeric_limits<index_t>::max();

  constexpr variant() noexcept : idx_{NO_VALUE} {}

  template <typename Arg,
            typename = std::enable_if_t<
                index_of_type<std::decay_t<Arg>, T...>() != TYPE_NOT_FOUND>>
  constexpr variant(Arg&& arg)
      : idx_{static_cast<index_t>(index_of_type<Arg, T...>())} {
#if defined(CISTA_ZERO_OUT)
    std::memset(&storage_, 0, sizeof(storage_));
#endif
    using Type = std::decay_t<Arg>;
    new (&storage_) Type(std::forward<Arg>(arg));
  }

  variant(variant const& o) : idx_{o.idx_} {
    o.apply([this](auto&& el) {
      using Type = std::decay_t<decltype(el)>;
      new (&storage_) Type(std::forward<decltype(el)>(el));
    });
  }
  variant(variant&& o) : idx_{o.idx_} {
    o.apply([this](auto&& el) {
      using Type = std::decay_t<decltype(el)>;
      new (&storage_) Type(std::move(el));
    });
  }

  variant& operator=(variant const& o) {
    return o.apply([&](auto&& el) -> variant& { return *this = el; });
  }
  variant& operator=(variant&& o) {
    return o.apply(
        [&](auto&& el) -> variant& { return *this = std::move(el); });
  }

  template <typename Arg,
            typename = std::enable_if_t<
                index_of_type<std::decay_t<Arg>, T...>() != TYPE_NOT_FOUND>>
  variant& operator=(Arg&& arg) {
    if (index_of_type<Arg, T...>() == idx_) {
      apply([&](auto&& el) {
        if constexpr (std::is_same_v<std::decay_t<decltype(el)>, Arg>) {
          el = std::move(arg);
        }
      });
      return *this;
    }
    destruct();
    idx_ = static_cast<index_t>(index_of_type<Arg, T...>());
    new (&storage_) std::decay_t<Arg>{std::forward<Arg>(arg)};
    return *this;
  }
#if _MSVC_LANG >= 202002L || __cplusplus >= 202002L
  constexpr
#endif
      ~variant() {
    destruct();
  }

  constexpr bool valid() const noexcept { return index() != NO_VALUE; }

  constexpr operator bool() const noexcept { return valid(); }

  friend bool operator==(variant const& a, variant const& b) noexcept {
    return a.idx_ == b.idx_
               ? apply([](auto&& u, auto&& v) -> bool { return u == v; },
                       a.idx_, a, b)
               : false;
  }

  friend bool operator!=(variant const& a, variant const& b) noexcept {
    return a.idx_ == b.idx_
               ? apply([](auto&& u, auto&& v) -> bool { return u != v; },
                       a.idx_, a, b)
               : true;
  }

  friend bool operator<(variant const& a, variant const& b) noexcept {
    return a.idx_ == b.idx_
               ? apply([](auto&& u, auto&& v) -> bool { return u < v; }, a.idx_,
                       a, b)
               : a.idx_ < b.idx_;
  }

  friend bool operator>(variant const& a, variant const& b) noexcept {
    return a.idx_ == b.idx_
               ? apply([](auto&& u, auto&& v) -> bool { return u > v; }, a.idx_,
                       a, b)
               : a.idx_ > b.idx_;
  }

  friend bool operator<=(variant const& a, variant const& b) noexcept {
    return a.idx_ == b.idx_
               ? apply([](auto&& u, auto&& v) -> bool { return u <= v; },
                       a.idx_, a, b)
               : a.idx_ <= b.idx_;
  }

  friend bool operator>=(variant const& a, variant const& b) noexcept {
    return a.idx_ == b.idx_
               ? apply([](auto&& u, auto&& v) -> bool { return u >= v; },
                       a.idx_, a, b)
               : a.idx_ >= b.idx_;
  }

  template <typename Arg, typename... CtorArgs>
  Arg& emplace(CtorArgs&&... ctor_args) {
    static_assert(index_of_type<Arg, T...>() != TYPE_NOT_FOUND);
    destruct();
    idx_ = static_cast<index_t>(index_of_type<Arg, T...>());
    return *(new (&storage_)
                 std::decay_t<Arg>{std::forward<CtorArgs>(ctor_args)...});
  }

  template <std::size_t I, typename... CtorArgs>
  type_at_index_t<I, T...>& emplace(CtorArgs&&... ctor_args) {
    static_assert(I < sizeof...(T));
    destruct();
    idx_ = I;
    return *(new (&storage_) std::decay_t<type_at_index_t<I, T...>>{
        std::forward<CtorArgs>(ctor_args)...});
  }

  constexpr std::size_t index() const noexcept { return idx_; }

  void swap(variant& o) {
    if (idx_ == o.idx_) {
      apply(
          [](auto&& a, auto&& b) {
            using std::swap;
            swap(a, b);
          },
          idx_, *this, o);
    } else {
      variant tmp{std::move(o)};
      o = std::move(*this);
      *this = std::move(tmp);
    }
  }

  constexpr void destruct() {
    if (idx_ != NO_VALUE) {
      apply([](auto&& el) {
        using el_type = std::decay_t<decltype(el)>;
        el.~el_type();
      });
    }
  }

  template <typename F>
  auto apply(F&& f) -> decltype(f(std::declval<type_at_index_t<0U, T...>>())) {
    if (idx_ == NO_VALUE) {
      throw_exception(std::runtime_error{"variant::apply: no value"});
    }
    return apply(std::forward<F>(f), idx_, *this);
  }

  template <typename F>
  auto apply(F&& f) const
      -> decltype(f(std::declval<type_at_index_t<0U, T...>>())) {
    if (idx_ == NO_VALUE) {
      throw_exception(std::runtime_error{"variant::apply: no value"});
    }
    return apply(std::forward<F>(f), idx_, *this);
  }

#ifdef _MSC_VER
  static __declspec(noreturn) void noret() {}
#endif

  template <typename F, std::size_t B = 0U, typename... Vs>
  static auto apply(F&& f, index_t const idx, Vs&&... vs)
      -> decltype(f((vs, std::declval<type_at_index_t<0U, T...>>())...)) {
    switch (idx) {
      case B + 0U:
        if constexpr (B + 0U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 0U, T...>>()...);
        }
        [[fallthrough]];
      case B + 1U:
        if constexpr (B + 1U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 1U, T...>>()...);
        }
        [[fallthrough]];
      case B + 2U:
        if constexpr (B + 2U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 2U, T...>>()...);
        }
        [[fallthrough]];
      case B + 3U:
        if constexpr (B + 3U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 3U, T...>>()...);
        }
        [[fallthrough]];
      case B + 4U:
        if constexpr (B + 4U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 4U, T...>>()...);
        }
        [[fallthrough]];
      case B + 5U:
        if constexpr (B + 5U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 5U, T...>>()...);
        }
        [[fallthrough]];
      case B + 6U:
        if constexpr (B + 6U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 6U, T...>>()...);
        }
        [[fallthrough]];
      case B + 7U:
        if constexpr (B + 7U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 7U, T...>>()...);
        }
        [[fallthrough]];
      case B + 8U:
        if constexpr (B + 8U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 8U, T...>>()...);
        }
        [[fallthrough]];
      case B + 9U:
        if constexpr (B + 9U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 9U, T...>>()...);
        }
        [[fallthrough]];
      case B + 10U:
        if constexpr (B + 10U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 10U, T...>>()...);
        }
        [[fallthrough]];
      case B + 11U:
        if constexpr (B + 11U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 11U, T...>>()...);
        }
        [[fallthrough]];
      case B + 12U:
        if constexpr (B + 12U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 12U, T...>>()...);
        }
        [[fallthrough]];
      case B + 13U:
        if constexpr (B + 13U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 13U, T...>>()...);
        }
        [[fallthrough]];
      case B + 14U:
        if constexpr (B + 14U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 14U, T...>>()...);
        }
        [[fallthrough]];
      case B + 15U:
        if constexpr (B + 15U < sizeof...(T)) {
          return f(vs.template as<type_at_index_t<B + 15U, T...>>()...);
        }
        break;
    }

    if constexpr (B + 15U < sizeof...(T)) {
      return apply<F, B + 16U>(std::forward<F>(f), std::forward<Vs>(vs)...);
    }

#ifndef _MSC_VER
    __builtin_unreachable();
#else
    noret();
#endif
  }

  template <typename AsType>
  AsType& as() noexcept {
    return *reinterpret_cast<AsType*>(&storage_);
  }

  template <typename AsType>
  AsType const& as() const noexcept {
    return *reinterpret_cast<AsType const*>(&storage_);
  }

  hash_t hash() const noexcept {
    return apply([&](auto&& val) {
      auto const idx = index();
      auto h = hashing<decltype(idx)>{}(idx, BASE_HASH);
      h = hashing<decltype(val)>{}(val, h);
      return h;
    });
  }

  index_t idx_{NO_VALUE};
  std::aligned_union_t<0, T...> storage_{};
};

template <typename T, typename... Ts>
bool holds_alternative(variant<Ts...> const& v) noexcept {
  return v.idx_ == index_of_type<std::decay_t<T>, Ts...>();
}

template <std::size_t I, typename... Ts>
constexpr cista::type_at_index_t<I, Ts...> const& get(
    cista::variant<Ts...> const& v) noexcept {
  return v.template as<cista::type_at_index_t<I, Ts...>>();
}

template <std::size_t I, typename... Ts>
constexpr cista::type_at_index_t<I, Ts...>& get(
    cista::variant<Ts...>& v) noexcept {
  return v.template as<cista::type_at_index_t<I, Ts...>>();
}

template <class T, class... Ts>
constexpr T const& get(cista::variant<Ts...> const& v) noexcept {
  static_assert(cista::index_of_type<T, Ts...>() != cista::TYPE_NOT_FOUND);
  return v.template as<T>();
}
template <class T, class... Ts>
constexpr T& get(cista::variant<Ts...>& v) noexcept {
  static_assert(cista::index_of_type<T, Ts...>() != cista::TYPE_NOT_FOUND);
  return v.template as<T>();
}

template <class T, class... Ts>
constexpr std::add_pointer_t<T> get_if(cista::variant<Ts...>& v) noexcept {
  static_assert(cista::index_of_type<T, Ts...>() != cista::TYPE_NOT_FOUND);
  return v.idx_ == cista::index_of_type<T, Ts...>() ? &v.template as<T>()
                                                    : nullptr;
}

template <std::size_t I, typename... Ts>
constexpr std::add_pointer_t<cista::type_at_index_t<I, Ts...> const> get_if(
    cista::variant<Ts...> const& v) noexcept {
  static_assert(I < sizeof...(Ts));
  return v.idx_ == I ? &v.template as<cista::type_at_index_t<I, Ts...>>()
                     : nullptr;
}

template <std::size_t I, typename... Ts>
constexpr std::add_pointer_t<cista::type_at_index_t<I, Ts...>> get_if(
    cista::variant<Ts...>& v) noexcept {
  static_assert(I < sizeof...(Ts));
  return v.idx_ == I ? &v.template as<cista::type_at_index_t<I, Ts...>>()
                     : nullptr;
}

template <class T, class... Ts>
constexpr std::add_pointer_t<T const> get_if(
    cista::variant<Ts...> const& v) noexcept {
  static_assert(cista::index_of_type<T, Ts...>() != cista::TYPE_NOT_FOUND);
  return v.idx_ == cista::index_of_type<T, Ts...>() ? &v.template as<T>()
                                                    : nullptr;
}

template <class T>
struct variant_size;

template <class... T>
struct variant_size<variant<T...>>
    : std::integral_constant<std::size_t, sizeof...(T)> {};

template <class T>
constexpr std::size_t variant_size_v = variant_size<T>::value;

namespace raw {
using cista::variant;
}  // namespace raw

namespace offset {
using cista::variant;
}  // namespace offset

}  // namespace cista

namespace std {

template <typename Visitor, typename... T>
constexpr auto visit(Visitor&& vis, cista::variant<T...>&& v) {
  v.apply(vis);
}

using cista::get;

}  // namespace std

#include <cassert>
#include <iterator>
#include <type_traits>


namespace cista {

template <typename Key, typename DataVec, typename IndexVec>
struct basic_vecvec {
  using data_value_type = typename DataVec::value_type;
  using index_value_type = typename IndexVec::value_type;

  struct bucket final {
    using value_type = data_value_type;
    using iterator = typename DataVec::iterator;
    using const_iterator = typename DataVec::iterator;

    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;
    using reference = bucket;

    bucket(basic_vecvec* map, index_value_type const i)
        : map_{map}, i_{to_idx(i)} {}

    friend data_value_type* data(bucket b) { return &b[0]; }
    friend index_value_type size(bucket b) { return b.size(); }

    data_value_type const* data() const { return empty() ? nullptr : &front(); }

    template <typename T = std::decay_t<data_value_type>,
              typename = std::enable_if_t<std::is_same_v<T, char>>>
    std::string_view view() const {
      return std::string_view{begin(), size()};
    }

    value_type& front() {
      assert(!empty());
      return operator[](0);
    }

    value_type& back() {
      assert(!empty());
      return operator[](size() - 1U);
    }

    bool empty() const { return begin() == end(); }

    template <typename Args>
    void push_back(Args&& args) {
      map_->data_.insert(std::next(std::begin(map_->data_), bucket_end_idx()),
                         std::forward<Args>(args));
      for (auto i = i_ + 1; i != map_->bucket_starts_.size(); ++i) {
        ++map_->bucket_starts_[i];
      }
    }

    value_type& operator[](std::size_t const i) {
      assert(is_inside_bucket(i));
      return map_->data_[to_idx(map_->bucket_starts_[i_] + i)];
    }

    value_type const& operator[](std::size_t const i) const {
      assert(is_inside_bucket(i));
      return map_->data_[to_idx(map_->bucket_starts_[i_] + i)];
    }

    value_type const& at(std::size_t const i) const {
      verify(i < size(), "bucket::at: index out of range");
      return *(begin() + i);
    }

    value_type& at(std::size_t const i) {
      verify(i < size(), "bucket::at: index out of range");
      return *(begin() + i);
    }

    std::size_t size() const { return bucket_end_idx() - bucket_begin_idx(); }
    iterator begin() { return map_->data_.begin() + bucket_begin_idx(); }
    iterator end() { return map_->data_.begin() + bucket_end_idx(); }
    const_iterator begin() const {
      return map_->data_.begin() + bucket_begin_idx();
    }
    const_iterator end() const {
      return map_->data_.begin() + bucket_end_idx();
    }
    friend iterator begin(bucket const& b) { return b.begin(); }
    friend iterator end(bucket const& b) { return b.end(); }
    friend iterator begin(bucket& b) { return b.begin(); }
    friend iterator end(bucket& b) { return b.end(); }

    friend bool operator==(bucket const& a, bucket const& b) {
      assert(a.map_ == b.map_);
      return a.i_ == b.i_;
    }
    friend bool operator!=(bucket const& a, bucket const& b) {
      assert(a.map_ == b.map_);
      return a.i_ != b.i_;
    }
    bucket& operator++() {
      ++i_;
      return *this;
    }
    bucket& operator--() {
      --i_;
      return *this;
    }
    bucket operator*() const { return *this; }
    bucket& operator+=(difference_type const n) {
      i_ += n;
      return *this;
    }
    bucket& operator-=(difference_type const n) {
      i_ -= n;
      return *this;
    }
    bucket operator+(difference_type const n) const {
      auto tmp = *this;
      tmp += n;
      return tmp;
    }
    bucket operator-(difference_type const n) const {
      auto tmp = *this;
      tmp -= n;
      return tmp;
    }
    friend difference_type operator-(bucket const& a, bucket const& b) {
      assert(a.map_ == b.map_);
      return a.i_ - b.i_;
    }

  private:
    index_value_type bucket_begin_idx() const {
      return to_idx(map_->bucket_starts_[i_]);
    }
    index_value_type bucket_end_idx() const {
      return to_idx(map_->bucket_starts_[i_ + 1U]);
    }
    bool is_inside_bucket(std::size_t const i) const {
      return bucket_begin_idx() + i < bucket_end_idx();
    }

    basic_vecvec* map_;
    index_value_type i_;
  };

  struct const_bucket final {
    using value_type = data_value_type;
    using iterator = typename DataVec::const_iterator;
    using const_iterator = typename DataVec::const_iterator;

    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<value_type>;
    using reference = std::add_lvalue_reference<value_type>;

    const_bucket(basic_vecvec const* map, index_value_type const i)
        : map_{map}, i_{to_idx(i)} {}

    friend data_value_type const* data(const_bucket b) { return b.data(); }
    friend index_value_type size(const_bucket b) { return b.size(); }

    data_value_type const* data() const { return empty() ? nullptr : &front(); }

    template <typename T = std::decay_t<data_value_type>,
              typename = std::enable_if_t<std::is_same_v<T, char>>>
    std::string_view view() const {
      return std::string_view{begin(), size()};
    }

    value_type const& front() const {
      assert(!empty());
      return operator[](0);
    }

    value_type const& back() const {
      assert(!empty());
      return operator[](size() - 1U);
    }

    bool empty() const { return begin() == end(); }

    value_type const& at(std::size_t const i) const {
      verify(i < size(), "bucket::at: index out of range");
      return *(begin() + i);
    }

    value_type const& operator[](std::size_t const i) const {
      assert(is_inside_bucket(i));
      return map_->data_[map_->bucket_starts_[i_] + i];
    }

    index_value_type size() const {
      return bucket_end_idx() - bucket_begin_idx();
    }
    const_iterator begin() const {
      return map_->data_.begin() + bucket_begin_idx();
    }
    const_iterator end() const {
      return map_->data_.begin() + bucket_end_idx();
    }
    friend const_iterator begin(const_bucket const& b) { return b.begin(); }
    friend const_iterator end(const_bucket const& b) { return b.end(); }

    friend bool operator==(const_bucket const& a, const_bucket const& b) {
      assert(a.map_ == b.map_);
      return a.i_ == b.i_;
    }
    friend bool operator!=(const_bucket const& a, const_bucket const& b) {
      assert(a.map_ == b.map_);
      return a.i_ != b.i_;
    }
    const_bucket& operator++() {
      ++i_;
      return *this;
    }
    const_bucket& operator--() {
      --i_;
      return *this;
    }
    const_bucket operator*() const { return *this; }
    const_bucket& operator+=(difference_type const n) {
      i_ += n;
      return *this;
    }
    const_bucket& operator-=(difference_type const n) {
      i_ -= n;
      return *this;
    }
    const_bucket operator+(difference_type const n) const {
      auto tmp = *this;
      tmp += n;
      return tmp;
    }
    const_bucket operator-(difference_type const n) const {
      auto tmp = *this;
      tmp -= n;
      return tmp;
    }
    friend difference_type operator-(const_bucket const& a,
                                     const_bucket const& b) {
      assert(a.map_ == b.map_);
      return a.i_ - b.i_;
    }

  private:
    std::size_t bucket_begin_idx() const {
      return to_idx(map_->bucket_starts_[i_]);
    }
    std::size_t bucket_end_idx() const {
      return to_idx(map_->bucket_starts_[i_ + 1]);
    }
    bool is_inside_bucket(std::size_t const i) const {
      return bucket_begin_idx() + i < bucket_end_idx();
    }

    std::size_t i_;
    basic_vecvec const* map_;
  };

  using value_type = bucket;
  using iterator = bucket;
  using const_iterator = const_bucket;

  bucket operator[](Key const i) { return {this, to_idx(i)}; }
  const_bucket operator[](Key const i) const { return {this, to_idx(i)}; }

  const_bucket at(Key const i) const {
    verify(to_idx(i) < bucket_starts_.size(),
           "basic_vecvec::at: index out of range");
    return {this, to_idx(i)};
  }

  bucket at(Key const i) {
    verify(to_idx(i) < bucket_starts_.size(),
           "basic_vecvec::at: index out of range");
    return {this, to_idx(i)};
  }

  bucket front() { return at(Key{0}); }
  bucket back() { return at(Key{size() - 1}); }

  const_bucket front() const { return at(Key{0}); }
  const_bucket back() const { return at(Key{size() - 1}); }

  index_value_type size() const {
    return empty() ? 0U : bucket_starts_.size() - 1;
  }
  bool empty() const { return bucket_starts_.empty(); }

  template <typename Container,
            typename = std::enable_if_t<std::is_convertible_v<
                decltype(*std::declval<Container>().begin()), data_value_type>>>
  void emplace_back(Container&& bucket) {
    if (bucket_starts_.empty()) {
      bucket_starts_.emplace_back(index_value_type{0U});
    }
    bucket_starts_.emplace_back(
        static_cast<index_value_type>(data_.size() + bucket.size()));
    data_.insert(std::end(data_),  //
                 std::make_move_iterator(std::begin(bucket)),
                 std::make_move_iterator(std::end(bucket)));
  }

  bucket add_back_sized(std::size_t const bucket_size) {
    if (bucket_starts_.empty()) {
      bucket_starts_.emplace_back(index_value_type{0U});
    }
    data_.resize(data_.size() + bucket_size);
    bucket_starts_.emplace_back(static_cast<index_value_type>(data_.size()));
    return at(Key{size() - 1U});
  }

  template <typename X>
  std::enable_if_t<std::is_convertible_v<std::decay_t<X>, data_value_type>>
  emplace_back(std::initializer_list<X>&& x) {
    if (bucket_starts_.empty()) {
      bucket_starts_.emplace_back(index_value_type{0U});
    }
    bucket_starts_.emplace_back(
        static_cast<index_value_type>(data_.size() + x.size()));
    data_.insert(std::end(data_),  //
                 std::make_move_iterator(std::begin(x)),
                 std::make_move_iterator(std::end(x)));
  }

  template <typename T = data_value_type,
            typename = std::enable_if_t<std::is_convertible_v<T, char const>>>
  void emplace_back(char const* s) {
    return emplace_back(std::string_view{s});
  }

  void resize(std::size_t const new_size) {
    auto const old_size = bucket_starts_.size();
    bucket_starts_.resize(
        static_cast<typename IndexVec::size_type>(new_size + 1U));
    for (auto i = old_size; i < new_size + 1U; ++i) {
      bucket_starts_[i] = data_.size();
    }
  }

  bucket begin() { return bucket{this, 0U}; }
  bucket end() { return bucket{this, size()}; }
  const_bucket begin() const { return const_bucket{this, 0U}; }
  const_bucket end() const { return const_bucket{this, size()}; }

  friend bucket begin(basic_vecvec& m) { return m.begin(); }
  friend bucket end(basic_vecvec& m) { return m.end(); }
  friend const_bucket begin(basic_vecvec const& m) { return m.begin(); }
  friend const_bucket end(basic_vecvec const& m) { return m.end(); }

  DataVec data_;
  IndexVec bucket_starts_;
};

namespace offset {

template <typename K, typename V, typename SizeType = base_t<K>>
using vecvec = basic_vecvec<K, vector<V>, vector<SizeType>>;

}  // namespace offset

namespace raw {

template <typename K, typename V, typename SizeType = base_t<K>>
using vecvec = basic_vecvec<K, vector<V>, vector<SizeType>>;

}  // namespace raw

}  // namespace cista


// Based on:
// https://stackoverflow.com/a/32210953 (MSVC)
// https://stackoverflow.com/a/27054190 (GCC/Clang)

#if !defined(CISTA_BIG_ENDIAN) && !defined(CISTA_LITTLE_ENDIAN)

#if defined(__APPLE__)
#include <machine/endian.h>
#elif defined(__GNUC__)
#include <endian.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(REG_DWORD) && REG_DWORD == REG_DWORD_BIG_ENDIAN ||               \
    defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN ||                 \
    defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
    defined(__AARCH64EB__) || defined(_MIBSEB) || defined(__MIBSEB) ||       \
    defined(__MIBSEB__)
#define CISTA_BIG_ENDIAN
#elif defined(REG_DWORD) && REG_DWORD == REG_DWORD_LITTLE_ENDIAN ||       \
    defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN ||           \
    defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) ||                   \
    defined(__THUMBEL__) || defined(__AARCH64EL__) || defined(_MIPSEL) || \
    defined(__MIPSEL) || defined(__MIPSEL__)
#define CISTA_LITTLE_ENDIAN
#else
#error "architecture: unknown byte order"
#endif

#endif

#include <type_traits>

namespace cista {

enum class mode {
  NONE = 0U,
  UNCHECKED = 1U << 0U,
  WITH_VERSION = 1U << 1U,
  WITH_INTEGRITY = 1U << 2U,
  SERIALIZE_BIG_ENDIAN = 1U << 3U,
  DEEP_CHECK = 1U << 4U,
  CAST = 1U << 5U,
  WITH_STATIC_VERSION = 1U << 6U,
  SKIP_INTEGRITY = 1U << 7U,
  SKIP_VERSION = 1U << 8U,
  _CONST = 1U << 29U,
  _PHASE_II = 1U << 30U
};

constexpr mode operator|(mode const& a, mode const& b) noexcept {
  return mode{static_cast<std::underlying_type_t<mode>>(a) |
              static_cast<std::underlying_type_t<mode>>(b)};
}

constexpr mode operator&(mode const& a, mode const& b) noexcept {
  return mode{static_cast<std::underlying_type_t<mode>>(a) &
              static_cast<std::underlying_type_t<mode>>(b)};
}

constexpr bool is_mode_enabled(mode const in, mode const flag) noexcept {
  return (in & flag) == flag;
}

constexpr bool is_mode_disabled(mode const in, mode const flag) noexcept {
  return (in & flag) == mode::NONE;
}

}  // namespace cista

// Based on
// https://github.com/google/flatbuffers/blob/master/include/flatbuffers/base.h

#if defined(_MSC_VER)
#define CISTA_BYTESWAP_16 _byteswap_ushort
#define CISTA_BYTESWAP_32 _byteswap_ulong
#define CISTA_BYTESWAP_64 _byteswap_uint64
#else
#define CISTA_BYTESWAP_16 __builtin_bswap16
#define CISTA_BYTESWAP_32 __builtin_bswap32
#define CISTA_BYTESWAP_64 __builtin_bswap64
#endif

namespace cista {

template <typename T>
constexpr T endian_swap(T const t) noexcept {
  static_assert(sizeof(T) == 1U || sizeof(T) == 2U || sizeof(T) == 4U ||
                sizeof(T) == 8U);

  if constexpr (sizeof(T) == 1U) {
    return t;
  } else if constexpr (sizeof(T) == 2U) {
    union {
      T t;
      std::uint16_t i;
    } u{t};
    u.i = CISTA_BYTESWAP_16(u.i);
    return u.t;
  } else if constexpr (sizeof(T) == 4U) {
    union {
      T t;
      std::uint32_t i;
    } u{t};
    u.i = CISTA_BYTESWAP_32(u.i);
    return u.t;
  } else if constexpr (sizeof(T) == 8U) {
    union {
      T t;
      std::uint64_t i;
    } u{t};
    u.i = CISTA_BYTESWAP_64(u.i);
    return u.t;
  }
}

template <mode Mode>
constexpr bool endian_conversion_necessary() noexcept {
  if constexpr ((Mode & mode::SERIALIZE_BIG_ENDIAN) ==
                mode::SERIALIZE_BIG_ENDIAN) {
#if defined(CISTA_BIG_ENDIAN)
    return false;
#else
    return true;
#endif
  } else {
#if defined(CISTA_LITTLE_ENDIAN)
    return false;
#else
    return true;
#endif
  }
}

template <mode Mode, typename T>
constexpr T convert_endian(T t) noexcept {
  if constexpr (endian_conversion_necessary<Mode>()) {
    return endian_swap(t);
  } else {
    return t;
  }
}

}  // namespace cista

#undef CISTA_BYTESWAP_16
#undef CISTA_BYTESWAP_32
#undef CISTA_BYTESWAP_64

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>


namespace cista {

using byte_buf = std::vector<std::uint8_t>;

template <typename Buf = byte_buf>
struct buf {
  buf() = default;
  explicit buf(Buf&& buf) : buf_{std::forward<Buf>(buf)} {}

  std::uint8_t* addr(offset_t const offset) noexcept {
    return (&buf_[0U]) + offset;
  }
  std::uint8_t* base() noexcept { return &buf_[0U]; }

  std::uint64_t checksum(offset_t const start = 0U) const noexcept {
    return hash(std::string_view{
        reinterpret_cast<char const*>(&buf_[static_cast<std::size_t>(start)]),
        buf_.size() - static_cast<std::size_t>(start)});
  }

  template <typename T>
  void write(std::size_t const pos, T const& val) {
    verify(buf_.size() >= pos + serialized_size<T>(), "out of bounds write");
    std::memcpy(&buf_[pos], &val, serialized_size<T>());
  }

  offset_t write(void const* ptr, std::size_t const num_bytes,
                 std::size_t alignment = 0U) {
    auto start = static_cast<offset_t>(size());
    if (alignment > 1U && buf_.size() != 0U) {
      auto unaligned_ptr = static_cast<void*>(addr(start));
      auto space = std::numeric_limits<std::size_t>::max();
      auto const aligned_ptr =
          std::align(alignment, num_bytes, unaligned_ptr, space);
      auto const new_offset = static_cast<offset_t>(
          aligned_ptr ? static_cast<std::uint8_t*>(aligned_ptr) - base() : 0U);
      auto const adjustment = static_cast<offset_t>(new_offset - start);
      start += adjustment;
    }

    auto const space_left =
        static_cast<int64_t>(buf_.size()) - static_cast<int64_t>(start);
    if (space_left < static_cast<int64_t>(num_bytes)) {
      auto const missing = static_cast<std::size_t>(
          static_cast<int64_t>(num_bytes) - space_left);
      buf_.resize(buf_.size() + missing);
    }
    std::memcpy(addr(start), ptr, num_bytes);

    return start;
  }

  std::uint8_t& operator[](std::size_t const i) noexcept { return buf_[i]; }
  std::uint8_t const& operator[](std::size_t const i) const noexcept {
    return buf_[i];
  }
  std::size_t size() const noexcept { return buf_.size(); }
  void reset() { buf_.resize(0U); }

  Buf buf_;
};

template <typename Buf>
buf(Buf&&) -> buf<Buf>;

}  // namespace cista


#include <type_traits>

namespace cista {

template <typename T>
struct indexed : public T {
  using value_type = T;
  using T::T;
  using T::operator=;
};

template <typename Ptr>
struct is_indexed_helper : std::false_type {};

template <typename T>
struct is_indexed_helper<indexed<T>> : std::true_type {};

template <class T>
constexpr bool is_indexed_v = is_indexed_helper<std::remove_cv_t<T>>::value;

}  // namespace cista

#if __has_include("fmt/ostream.h")


template <typename T>
struct fmt::formatter<cista::indexed<T>> : ostream_formatter {};

#endif

// Credits: Manu Sánchez (@Manu343726)
// https://github.com/Manu343726/ctti/blob/master/include/ctti/detail/pretty_function.hpp

#include <string>
#include <string_view>

namespace cista {

#if defined(_MSC_VER)
#define CISTA_SIG __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
#define CISTA_SIG __PRETTY_FUNCTION__
#else
#error unsupported compiler
#endif

inline void remove_all(std::string& s, std::string_view substr) {
  auto pos = std::size_t{};
  while ((pos = s.find(substr, pos)) != std::string::npos) {
    s.erase(pos, substr.length());
  }
}

inline void canonicalize_type_name(std::string& s) {
  remove_all(s, "{anonymous}::");  // GCC
  remove_all(s, "(anonymous namespace)::");  // Clang
  remove_all(s, "`anonymous-namespace'::");  // MSVC
  remove_all(s, "struct");  // MSVC "struct my_struct" vs "my_struct"
  remove_all(s, "const");  // MSVC "char const*"" vs "const char*"
  remove_all(s, " ");  // MSVC
}

template <typename T>
constexpr std::string_view type_str() {
#if defined(__clang__)
  constexpr std::string_view prefix =
      "std::string_view cista::type_str() [T = ";
  constexpr std::string_view suffix = "]";
#elif defined(_MSC_VER)
  constexpr std::string_view prefix =
      "class std::basic_string_view<char,struct std::char_traits<char> > "
      "__cdecl cista::type_str<";
  constexpr std::string_view suffix = ">(void)";
#else
  constexpr std::string_view prefix =
      "constexpr std::string_view cista::type_str() [with T = ";
  constexpr std::string_view suffix =
      "; std::string_view = std::basic_string_view<char>]";
#endif

  auto sig = std::string_view{CISTA_SIG};
  sig.remove_prefix(prefix.size());
  sig.remove_suffix(suffix.size());
  return sig;
}

template <typename T>
std::string canonical_type_str() {
  auto const base = type_str<T>();
  std::string s{base.data(), base.length()};
  canonicalize_type_name(s);
  return s;
}

}  // namespace cista

#undef CISTA_SIG

namespace cista {

template <typename T>
constexpr hash_t static_type2str_hash() noexcept {
  return hash_combine(hash(type_str<decay_t<T>>()), sizeof(T));
}

template <typename T>
constexpr auto to_member_type_list() {
  auto t = T{};
  return cista::to_tuple(t);
}

template <typename T>
using tuple_representation_t = decltype(to_member_type_list<T>());

template <typename K, typename V, std::size_t Size>
struct count_map {
  using key_type = K;
  using mapped_type = V;
  struct value_type {
    key_type first;
    mapped_type second;
  };
  using const_iterator = typename std::array<value_type, Size>::const_iterator;

  constexpr count_map() = default;

  constexpr std::pair<mapped_type, bool> add(key_type k) noexcept {
    auto const it = find(k);
    if (it == end()) {
      arr_[size_] = value_type{k, static_cast<mapped_type>(size_)};
      auto const index = size_;
      ++size_;
      return {static_cast<mapped_type>(index), true};
    } else {
      return {it->second, false};
    }
  }

  constexpr const_iterator find(key_type k) const noexcept {
    for (auto it = begin(); it != end(); ++it) {
      if (it->first == k) {
        return it;
      }
    }
    return end();
  }

  constexpr const_iterator begin() const noexcept { return std::begin(arr_); }
  constexpr const_iterator end() const noexcept {
    return std::next(
        begin(),
        static_cast<
            typename std::iterator_traits<const_iterator>::difference_type>(
            size_));
  }

  std::size_t size_{0U};
  std::array<value_type, Size> arr_{};
};

template <std::size_t NMaxTypes>
struct hash_data {
  constexpr hash_data combine(hash_t const h) const noexcept {
    hash_data r;
    r.done_ = done_;
    r.h_ = hash_combine(h_, h);
    return r;
  }
  count_map<hash_t, unsigned, NMaxTypes> done_;
  hash_t h_{BASE_HASH};
};

template <typename T>
constexpr T* null() noexcept {
  return static_cast<T*>(nullptr);
}

template <typename T, std::size_t NMaxTypes>
constexpr hash_data<NMaxTypes> static_type_hash(T const*,
                                                hash_data<NMaxTypes>) noexcept;

template <typename Tuple, std::size_t NMaxTypes, std::size_t I>
constexpr auto hash_tuple_element(hash_data<NMaxTypes> const h) noexcept {
  using element_type = std::decay_t<std::tuple_element_t<I, Tuple>>;
  return static_type_hash(null<element_type>(), h);
}

template <typename Tuple, std::size_t NMaxTypes, std::size_t... I>
constexpr auto hash_tuple(Tuple const*, hash_data<NMaxTypes> h,
                          std::index_sequence<I...>) noexcept {
  ((h = hash_tuple_element<Tuple, NMaxTypes, I>(h)), ...);
  return h;
}

template <typename T, std::size_t NMaxTypes>
constexpr hash_data<NMaxTypes> static_type_hash(
    T const*, hash_data<NMaxTypes> h) noexcept {
  using Type = decay_t<T>;

  auto const base_hash = static_type2str_hash<Type>();
  auto const [ordering, inserted] = h.done_.add(base_hash);
  if (!inserted) {
    return h.combine(ordering);
  }

  if constexpr (is_pointer_v<Type>) {
    using PointeeType = remove_pointer_t<Type>;
    if constexpr (std::is_same_v<std::remove_const_t<PointeeType>, void>) {
      return h.combine(hash("void*"));
    } else {
      h = h.combine(hash("pointer"));
      return static_type_hash(static_cast<remove_pointer_t<Type>*>(nullptr), h);
    }
  } else if constexpr (std::is_integral_v<Type>) {
    return h.combine(hash("i")).combine(sizeof(Type));
  } else if constexpr (std::is_scalar_v<Type>) {
    return h.combine(static_type2str_hash<T>());
  } else {
    static_assert(to_tuple_works_v<Type>, "Please implement custom type hash.");
    using tuple_t = tuple_representation_t<T>;
    return hash_tuple<tuple_t>(
        null<tuple_t>(), h.combine(hash("struct")),
        std::make_index_sequence<std::tuple_size_v<tuple_t>>());
  }
}

template <typename Rep, typename Period, std::size_t NMaxTypes>
constexpr auto static_type_hash(std::chrono::duration<Rep, Period> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("duration"));
  h = static_type_hash(null<Rep>(), h);
  h = static_type_hash(null<Period>(), h);
  return h;
}

template <typename A, typename B, std::size_t NMaxTypes>
constexpr auto static_type_hash(std::pair<A, B> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("pair"));
  h = static_type_hash(null<A>(), h);
  h = static_type_hash(null<B>(), h);
  return h;
}

template <typename Clock, typename Duration, std::size_t NMaxTypes>
constexpr auto static_type_hash(std::chrono::time_point<Clock, Duration> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("timepoint"));
  h = static_type_hash(null<Duration>(), h);
  h = static_type_hash(null<Clock>(), h);
  return h;
}

template <typename T, std::size_t Size, std::size_t NMaxTypes>
constexpr auto static_type_hash(array<T, Size> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = static_type_hash(null<T>(), h);
  return h.combine(hash("array")).combine(Size);
}

template <typename T, template <typename> typename Ptr, bool Indexed,
          typename TemplateSizeType, std::size_t NMaxTypes>
constexpr auto static_type_hash(
    basic_vector<T, Ptr, Indexed, TemplateSizeType> const*,
    hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("vector"));
  return static_type_hash(null<T>(), h);
}

template <typename T, typename Ptr, std::size_t NMaxTypes>
constexpr auto static_type_hash(basic_unique_ptr<T, Ptr> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("unique_ptr"));
  return static_type_hash(null<T>(), h);
}

template <typename T, template <typename> typename Ptr, typename GetKey,
          typename GetValue, typename Hash, typename Eq, std::size_t NMaxTypes>
constexpr auto static_type_hash(
    hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq> const*,
    hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("hash_storage"));
  return static_type_hash(null<T>(), h);
}

template <std::size_t NMaxTypes, typename... T>
constexpr auto static_type_hash(variant<T...> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("variant"));
  ((h = static_type_hash(null<T>(), h)), ...);
  return h;
}

template <std::size_t NMaxTypes, typename... T>
constexpr auto static_type_hash(tuple<T...> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("tuple"));
  ((h = static_type_hash(null<T>(), h)), ...);
  return h;
}

template <typename Ptr, std::size_t NMaxTypes>
constexpr auto static_type_hash(generic_string<Ptr> const*,
                                hash_data<NMaxTypes> h) noexcept {
  return h.combine(hash("string"));
}

template <typename Ptr, std::size_t NMaxTypes>
constexpr auto static_type_hash(basic_string<Ptr> const*,
                                hash_data<NMaxTypes> h) noexcept {
  return h.combine(hash("string"));
}

template <typename Ptr, std::size_t NMaxTypes>
constexpr auto static_type_hash(basic_string_view<Ptr> const*,
                                hash_data<NMaxTypes> h) noexcept {
  return h.combine(hash("string"));
}

template <typename T, std::size_t NMaxTypes>
constexpr auto static_type_hash(indexed<T> const*,
                                hash_data<NMaxTypes> h) noexcept {
  return static_type_hash(null<T>(), h);
}

template <typename T, typename Tag, std::size_t NMaxTypes>
constexpr auto static_type_hash(strong<T, Tag> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = static_type_hash(null<T>(), h);
  return h;
}

template <typename T, std::size_t NMaxTypes>
constexpr auto static_type_hash(optional<T> const*,
                                hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("optional"));
  h = static_type_hash(null<T>(), h);
  return h;
}

template <typename T, typename SizeType, template <typename> typename Vec,
          std::size_t Log2MaxEntriesPerBucket, std::size_t NMaxTypes>
constexpr auto static_type_hash(
    dynamic_fws_multimap_base<T, SizeType, Vec, Log2MaxEntriesPerBucket> const*,
    hash_data<NMaxTypes> h) noexcept {
  h = h.combine(hash("dynamic_fws_multimap"));
  h = static_type_hash(null<Vec<SizeType>>(), h);
  h = static_type_hash(null<Vec<T>>(), h);
  h = h.combine(Log2MaxEntriesPerBucket);
  return h;
}

template <typename T, std::size_t NMaxTypes = 128U>
constexpr hash_t static_type_hash() noexcept {
  return static_type_hash(null<T>(), hash_data<NMaxTypes>{}).h_;
}

}  // namespace cista

#include <chrono>
#include <map>


namespace cista {

template <typename T>
hash_t type2str_hash() noexcept {
  return hash_combine(hash(canonical_type_str<decay_t<T>>()), sizeof(T));
}

template <typename T>
hash_t type_hash(T const&, hash_t, std::map<hash_t, unsigned>&) noexcept;

template <typename Rep, typename Period>
hash_t type_hash(std::chrono::duration<Rep, Period> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("duration"));
  h = type_hash(Rep{}, h, done);
  h = hash_combine(hash(canonical_type_str<Period>()), h);
  return h;
}

template <typename Clock, typename Duration>
hash_t type_hash(std::chrono::time_point<Clock, Duration> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("timepoint"));
  h = type_hash(Duration{}, h, done);
  h = hash_combine(hash(canonical_type_str<Clock>()), h);
  return h;
}

template <typename T, std::size_t Size>
hash_t type_hash(array<T, Size> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("array"));
  h = hash_combine(h, Size);
  return type_hash(T{}, h, done);
}

template <typename T>
hash_t type_hash(T const& el, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  using Type = decay_t<T>;

  auto const base_hash = type2str_hash<Type>();
  auto [it, inserted] =
      done.try_emplace(base_hash, static_cast<unsigned>(done.size()));
  if (!inserted) {
    return hash_combine(h, it->second);
  }

  if constexpr (is_pointer_v<Type>) {
    using PointeeType = remove_pointer_t<Type>;
    if constexpr (std::is_same_v<PointeeType, void>) {
      return hash_combine(h, hash("void*"));
    } else {
      return type_hash(remove_pointer_t<Type>{},
                       hash_combine(h, hash("pointer")), done);
    }
  } else if constexpr (std::is_integral_v<Type>) {
    return hash_combine(h, hash("i"), sizeof(Type));
  } else if constexpr (std::is_scalar_v<Type>) {
    return hash_combine(h, type2str_hash<T>());
  } else {
    static_assert(to_tuple_works_v<Type>, "Please implement custom type hash.");
    h = hash_combine(h, hash("struct"));
    for_each_field(el, [&](auto const& member) noexcept {
      h = type_hash(member, h, done);
    });
    return h;
  }
}

template <typename A, typename B>
hash_t type_hash(pair<A, B> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = type_hash(A{}, h, done);
  h = type_hash(B{}, h, done);
  return hash_combine(h, hash("pair"));
}

template <typename T, template <typename> typename Ptr, bool Indexed,
          typename TemplateSizeType>
hash_t type_hash(basic_vector<T, Ptr, Indexed, TemplateSizeType> const&,
                 hash_t h, std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("vector"));
  return type_hash(T{}, h, done);
}

template <typename T, typename Ptr>
hash_t type_hash(basic_unique_ptr<T, Ptr> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("unique_ptr"));
  return type_hash(T{}, h, done);
}

template <typename T, template <typename> typename Ptr, typename GetKey,
          typename GetValue, typename Hash, typename Eq>
hash_t type_hash(hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq> const&,
                 hash_t h, std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("hash_storage"));
  return type_hash(T{}, h, done);
}

template <typename... T>
hash_t type_hash(variant<T...> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("variant"));
  ((h = type_hash(T{}, h, done)), ...);
  return h;
}

template <typename... T>
hash_t type_hash(tuple<T...> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("tuple"));
  ((h = type_hash(T{}, h, done)), ...);
  return h;
}

template <typename Ptr>
hash_t type_hash(generic_string<Ptr> const&, hash_t h,
                 std::map<hash_t, unsigned>&) noexcept {
  return hash_combine(h, hash("string"));
}

template <typename Ptr>
hash_t type_hash(basic_string<Ptr> const&, hash_t h,
                 std::map<hash_t, unsigned>&) noexcept {
  return hash_combine(h, hash("string"));
}

template <typename Ptr>
hash_t type_hash(basic_string_view<Ptr> const&, hash_t h,
                 std::map<hash_t, unsigned>&) noexcept {
  return hash_combine(h, hash("string"));
}

template <typename T>
hash_t type_hash(indexed<T> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  return type_hash(T{}, h, done);
}

template <typename T, typename Tag>
hash_t type_hash(strong<T, Tag> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("strong"));
  h = type_hash(T{}, h, done);
  h = hash_combine(hash(canonical_type_str<Tag>()), h);
  return h;
}

template <typename T>
hash_t type_hash(optional<T> const&, hash_t h,
                 std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("optional"));
  h = type_hash(T{}, h, done);
  return h;
}

template <typename T, typename SizeType, template <typename> typename Vec,
          std::size_t Log2MaxEntriesPerBucket>
hash_t type_hash(
    dynamic_fws_multimap_base<T, SizeType, Vec, Log2MaxEntriesPerBucket> const&,
    hash_t h, std::map<hash_t, unsigned>& done) noexcept {
  h = hash_combine(h, hash("dynamic_fws_multimap"));
  h = type_hash(Vec<SizeType>{}, h, done);
  h = type_hash(Vec<T>{}, h, done);
  h = hash_combine(Log2MaxEntriesPerBucket, h);
  return h;
}

template <typename T>
hash_t type_hash() {
  auto done = std::map<hash_t, unsigned>{};
  return type_hash(T{}, BASE_HASH, done);
}

}  // namespace cista

#ifndef cista_member_offset
#define cista_member_offset(Type, Member)                            \
  ([]() {                                                            \
    if constexpr (std::is_standard_layout_v<Type>) {                 \
      return static_cast<::cista::offset_t>(offsetof(Type, Member)); \
    } else {                                                         \
      return ::cista::member_offset(null<Type>(), &Type::Member);    \
    }                                                                \
  }())
#endif

namespace cista {

template <typename T, typename Member>
cista::offset_t member_offset(T const* t, Member const* m) {
  static_assert(std::is_trivially_copyable_v<T>);
  return (reinterpret_cast<std::uint8_t const*>(m) -
          reinterpret_cast<std::uint8_t const*>(t));
}

template <typename T, typename Member>
offset_t member_offset(T const* t, Member T::*m) {
  static_assert(std::is_trivially_copyable_v<T>);
  return (reinterpret_cast<std::uint8_t const*>(&(t->*m)) -
          reinterpret_cast<std::uint8_t const*>(t));
}

// =============================================================================
// SERIALIZE
// -----------------------------------------------------------------------------
struct pending_offset {
  void const* origin_ptr_;
  offset_t pos_;
};

struct vector_range {
  bool contains(void const* begin, void const* ptr) const noexcept {
    auto const ptr_int = reinterpret_cast<uintptr_t>(ptr);
    auto const from = reinterpret_cast<uintptr_t>(begin);
    auto const to =
        reinterpret_cast<uintptr_t>(begin) + static_cast<uintptr_t>(size_);
    return ptr_int >= from && ptr_int < to;
  }

  offset_t offset_of(void const* begin, void const* ptr) const noexcept {
    return start_ + reinterpret_cast<intptr_t>(ptr) -
           reinterpret_cast<intptr_t>(begin);
  }

  offset_t start_;
  std::size_t size_;
};

template <typename Target, mode Mode>
struct serialization_context {
  static constexpr auto const MODE = Mode;

  explicit serialization_context(Target& t) : t_{t} {}

  static bool compare(std::pair<void const*, vector_range> const& a,
                      std::pair<void const*, vector_range> const& b) noexcept {
    return a.first < b.first;
  }

  offset_t write(void const* ptr, std::size_t const size,
                 std::size_t const alignment = 0) {
    return t_.write(ptr, size, alignment);
  }

  template <typename T>
  void write(offset_t const pos, T const& val) {
    t_.write(static_cast<std::size_t>(pos), val);
  }

  template <typename T>
  bool resolve_pointer(offset_ptr<T> const& ptr, offset_t const pos,
                       bool const add_pending = true) {
    return resolve_pointer(ptr.get(), pos, add_pending);
  }

  template <typename Ptr>
  bool resolve_pointer(Ptr ptr, offset_t const pos,
                       bool const add_pending = true) {
    if (std::is_same_v<decay_t<remove_pointer_t<Ptr>>, void> && add_pending) {
      write(pos, convert_endian<MODE>(NULLPTR_OFFSET));
      return true;
    }
    if (ptr == nullptr) {
      write(pos, convert_endian<MODE>(NULLPTR_OFFSET));
      return true;
    }
    if (auto const it = offsets_.find(ptr_cast(ptr)); it != end(offsets_)) {
      write(pos, convert_endian<MODE>(it->second - pos));
      return true;
    }
    if (auto const offset = resolve_vector_range_ptr(ptr); offset.has_value()) {
      write(pos, convert_endian<MODE>(*offset - pos));
      return true;
    }
    if (add_pending) {
      write(pos, convert_endian<MODE>(NULLPTR_OFFSET));
      pending_.emplace_back(pending_offset{ptr_cast(ptr), pos});
      return true;
    }
    return false;
  }

  template <typename Ptr>
  std::optional<offset_t> resolve_vector_range_ptr(Ptr ptr) {
    if (vector_ranges_.empty()) {
      return std::nullopt;
    }
    auto const vec_it = vector_ranges_.upper_bound(ptr);
    if (vec_it == begin(vector_ranges_)) {
      return std::nullopt;
    }
    auto const pred = std::prev(vec_it);
    return pred->second.contains(pred->first, ptr)
               ? std::make_optional(pred->second.offset_of(pred->first, ptr))
               : std::nullopt;
  }

  std::uint64_t checksum(offset_t const from) const noexcept {
    return t_.checksum(from);
  }

  cista::raw::hash_map<void const*, offset_t> offsets_;
  std::map<void const*, vector_range> vector_ranges_;
  std::vector<pending_offset> pending_;
  Target& t_;
};

template <typename Ctx, typename T>
void serialize(Ctx& c, T const* origin, offset_t const pos) {
  using Type = decay_t<T>;
  if constexpr (std::is_union_v<Type>) {
    static_assert(std::is_standard_layout_v<Type> &&
                  std::is_trivially_copyable_v<Type>);
  } else if constexpr (is_pointer_v<Type>) {
    c.resolve_pointer(*origin, pos);
  } else if constexpr (is_indexed_v<Type>) {
    c.offsets_.emplace(origin, pos);
    serialize(c, static_cast<typename Type::value_type const*>(origin), pos);
  } else if constexpr (!std::is_scalar_v<Type>) {
    static_assert(to_tuple_works_v<Type>, "Please implement custom serializer");
    for_each_ptr_field(*origin, [&](auto& member) {
      auto const member_offset =
          static_cast<offset_t>(reinterpret_cast<intptr_t>(member) -
                                reinterpret_cast<intptr_t>(origin));
      serialize(c, member, pos + member_offset);
    });
  } else if constexpr (std::numeric_limits<Type>::is_integer) {
    c.write(pos, convert_endian<Ctx::MODE>(*origin));
  } else {
    CISTA_UNUSED_PARAM(origin)
    CISTA_UNUSED_PARAM(pos)
  }
}

template <typename Ctx, typename T, template <typename> typename Ptr,
          bool Indexed, typename TemplateSizeType>
void serialize(Ctx& c,
               basic_vector<T, Ptr, Indexed, TemplateSizeType> const* origin,
               offset_t const pos) {
  using Type = basic_vector<T, Ptr, Indexed, TemplateSizeType>;

  auto const size = serialized_size<T>() * origin->used_size_;
  auto const start = origin->empty()
                         ? NULLPTR_OFFSET
                         : c.write(static_cast<T const*>(origin->el_), size,
                                   std::alignment_of_v<T>);

  c.write(pos + cista_member_offset(Type, el_),
          convert_endian<Ctx::MODE>(
              start == NULLPTR_OFFSET
                  ? start
                  : start - cista_member_offset(Type, el_) - pos));
  c.write(pos + cista_member_offset(Type, allocated_size_),
          convert_endian<Ctx::MODE>(origin->used_size_));
  c.write(pos + cista_member_offset(Type, used_size_),
          convert_endian<Ctx::MODE>(origin->used_size_));
  c.write(pos + cista_member_offset(Type, self_allocated_), false);

  if constexpr (Indexed) {
    if (origin->el_ != nullptr) {
      c.vector_ranges_.emplace(origin->el_, vector_range{start, size});
    }
  }

  if (origin->el_ != nullptr) {
    auto i = 0U;
    for (auto it = start; it != start + static_cast<offset_t>(size);
         it += serialized_size<T>()) {
      serialize(c, static_cast<T const*>(origin->el_ + i++), it);
    }
  }
}

template <typename Ctx, typename Ptr>
void serialize(Ctx& c, generic_string<Ptr> const* origin, offset_t const pos) {
  using Type = generic_string<Ptr>;

  if (origin->is_short()) {
    return;
  }

  auto const start = (origin->h_.ptr_ == nullptr)
                         ? NULLPTR_OFFSET
                         : c.write(origin->data(), origin->size());
  c.write(pos + cista_member_offset(Type, h_.ptr_),
          convert_endian<Ctx::MODE>(
              start == NULLPTR_OFFSET
                  ? start
                  : start - cista_member_offset(Type, h_.ptr_) - pos));
  c.write(pos + cista_member_offset(Type, h_.size_),
          convert_endian<Ctx::MODE>(origin->h_.size_));
  c.write(pos + cista_member_offset(Type, h_.self_allocated_), false);
}

template <typename Ctx, typename T, typename SizeType,
          template <typename> typename Vec, std::size_t Log2MaxEntriesPerBucket>
void serialize(Ctx& c,
               dynamic_fws_multimap_base<T, SizeType, Vec,
                                         Log2MaxEntriesPerBucket> const* origin,
               offset_t const pos) {
  using Type =
      dynamic_fws_multimap_base<T, SizeType, Vec, Log2MaxEntriesPerBucket>;
  serialize(c, &origin->index_, pos + cista_member_offset(Type, index_));
  serialize(c, &origin->data_, pos + cista_member_offset(Type, data_));
  serialize(c, &origin->free_buckets_,
            pos + cista_member_offset(Type, free_buckets_));
  serialize(c, &origin->element_count_,
            pos + cista_member_offset(Type, element_count_));
}

template <typename Ctx, typename Ptr>
void serialize(Ctx& c, generic_cstring<Ptr> const* origin, offset_t const pos) {
  using Type = generic_cstring<Ptr>;

  if (origin->is_short()) {
    return;
  }

  const auto* data = origin->data();
  auto size = origin->size();
  std::string buf;
  if (!origin->is_owning()) {
    buf = origin->str();
    data = buf.data();
    size = buf.size();
  }
  auto capacity = size + 1;

  auto const start = c.write(data, capacity);
  c.write(pos + cista_member_offset(Type, h_.ptr_),
          convert_endian<Ctx::MODE>(start - cista_member_offset(Type, h_.ptr_) -
                                    pos));
  c.write(pos + cista_member_offset(Type, h_.size_),
          convert_endian<Ctx::MODE>(origin->h_.size_));
  c.write(pos + cista_member_offset(Type, h_.self_allocated_), false);
  c.write(pos + cista_member_offset(Type, h_.minus_one_),
          static_cast<char>(-1));
}

template <typename Ctx, typename Ptr>
void serialize(Ctx& c, basic_string<Ptr> const* origin, offset_t const pos) {
  serialize(c, static_cast<generic_string<Ptr> const*>(origin), pos);
}

template <typename Ctx, typename Ptr>
void serialize(Ctx& c, basic_string_view<Ptr> const* origin,
               offset_t const pos) {
  serialize(c, static_cast<generic_string<Ptr> const*>(origin), pos);
}

template <typename Ctx, typename Ptr>
void serialize(Ctx& c, basic_cstring<Ptr> const* origin, offset_t const pos) {
  serialize(c, static_cast<generic_cstring<Ptr> const*>(origin), pos);
}

template <typename Ctx, typename T, typename Ptr>
void serialize(Ctx& c, basic_unique_ptr<T, Ptr> const* origin,
               offset_t const pos) {
  using Type = basic_unique_ptr<T, Ptr>;

  auto const start =
      origin->el_ == nullptr
          ? NULLPTR_OFFSET
          : c.write(origin->el_, serialized_size<T>(), std::alignment_of_v<T>);

  c.write(pos + cista_member_offset(Type, el_),
          convert_endian<Ctx::MODE>(
              start == NULLPTR_OFFSET
                  ? start
                  : start - cista_member_offset(Type, el_) - pos));
  c.write(pos + cista_member_offset(Type, self_allocated_), false);

  if (origin->el_ != nullptr) {
    c.offsets_[origin->el_] = start;
    serialize(c, ptr_cast(origin->el_), start);
  }
}

template <typename Ctx, typename T, template <typename> typename Ptr,
          typename GetKey, typename GetValue, typename Hash, typename Eq>
void serialize(Ctx& c,
               hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq> const* origin,
               offset_t const pos) {
  using Type = hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq>;

  auto const start =
      origin->entries_ == nullptr
          ? NULLPTR_OFFSET
          : c.write(origin->entries_,
                    static_cast<std::size_t>(
                        origin->capacity_ * serialized_size<T>() +
                        (origin->capacity_ + 1 + Type::WIDTH) *
                            sizeof(typename Type::ctrl_t)),
                    std::alignment_of_v<T>);
  auto const ctrl_start =
      start == NULLPTR_OFFSET
          ? c.write(Type::empty_group(), 16U * sizeof(typename Type::ctrl_t),
                    std::alignment_of_v<typename Type::ctrl_t>)
          : start +
                static_cast<offset_t>(origin->capacity_ * serialized_size<T>());

  c.write(pos + cista_member_offset(Type, entries_),
          convert_endian<Ctx::MODE>(
              start == NULLPTR_OFFSET
                  ? start
                  : start - cista_member_offset(Type, entries_) - pos));
  c.write(pos + cista_member_offset(Type, ctrl_),
          convert_endian<Ctx::MODE>(
              ctrl_start == NULLPTR_OFFSET
                  ? ctrl_start
                  : ctrl_start - cista_member_offset(Type, ctrl_) - pos));

  c.write(pos + cista_member_offset(Type, self_allocated_), false);

  c.write(pos + cista_member_offset(Type, size_),
          convert_endian<Ctx::MODE>(origin->size_));
  c.write(pos + cista_member_offset(Type, capacity_),
          convert_endian<Ctx::MODE>(origin->capacity_));
  c.write(pos + cista_member_offset(Type, growth_left_),
          convert_endian<Ctx::MODE>(origin->growth_left_));

  if (origin->entries_ != nullptr) {
    auto i = 0u;
    for (auto it = start;
         it != start + static_cast<offset_t>(origin->capacity_ *
                                             serialized_size<T>());
         it += serialized_size<T>(), ++i) {
      if (Type::is_full(origin->ctrl_[i])) {
        serialize(c, static_cast<T*>(origin->entries_ + i), it);
      }
    }
  }
}

template <typename Ctx, typename Rep, typename Period>
void serialize(Ctx& c, std::chrono::duration<Rep, Period> const* origin,
               offset_t const pos) {
  static_assert(sizeof(origin->count() == sizeof(*origin)));
  c.write(pos, convert_endian<Ctx::MODE>(origin->count()));
}

template <typename Ctx, typename Clock, typename Dur>
void serialize(Ctx& c, std::chrono::time_point<Clock, Dur> const* origin,
               offset_t const pos) {
  static_assert(sizeof(origin->time_since_epoch().count()) == sizeof(*origin));
  c.write(pos, convert_endian<Ctx::MODE>(origin->time_since_epoch().count()));
}

template <typename Ctx, std::size_t Size>
void serialize(Ctx& c, bitset<Size> const* origin, offset_t const pos) {
  serialize(c, &origin->blocks_, pos);
}

template <typename Ctx, typename T, std::size_t Size>
void serialize(Ctx& c, array<T, Size> const* origin, offset_t const pos) {
  auto const size =
      static_cast<offset_t>(serialized_size<T>() * origin->size());
  auto i = 0U;
  for (auto it = pos; it != pos + size; it += serialized_size<T>()) {
    serialize(c, &(*origin)[i++], it);
  }
}

template <typename Ctx, typename A, typename B>
void serialize(Ctx& c, pair<A, B> const* origin, offset_t const pos) {
  using Type = decay_t<decltype(*origin)>;
  serialize(c, &origin->first, pos + cista_member_offset(Type, first));
  serialize(c, &origin->second, pos + cista_member_offset(Type, second));
}

template <typename Ctx, typename A, typename B>
void serialize(Ctx& c, std::pair<A, B> const* origin, offset_t const pos) {
  using Type = decay_t<decltype(*origin)>;
  serialize(c, &origin->first, pos + cista_member_offset(Type, first));
  serialize(c, &origin->second, pos + cista_member_offset(Type, second));
}

template <typename Ctx, typename... T>
void serialize(Ctx& c, variant<T...> const* origin, offset_t const pos) {
  using Type = decay_t<decltype(*origin)>;
  c.write(pos + cista_member_offset(Type, idx_),
          convert_endian<Ctx::MODE>(origin->idx_));
  auto const offset = cista_member_offset(Type, storage_);
  origin->apply([&](auto&& t) { serialize(c, &t, pos + offset); });
}

template <typename Ctx, typename T>
void serialize(Ctx& c, optional<T> const* origin, offset_t const pos) {
  using Type = decay_t<decltype(*origin)>;

  if (origin->valid_) {
    serialize(c, &origin->value(), pos + cista_member_offset(Type, storage_));
  }
}

template <typename Ctx, typename... T>
void serialize(Ctx& c, tuple<T...> const* origin,
               cista::offset_t const offset) {
  ::cista::apply(
      [&](auto&&... args) {
        (serialize(c, &args,
                   offset + (reinterpret_cast<intptr_t>(&args) -
                             reinterpret_cast<intptr_t>(origin))),
         ...);
      },
      *origin);
}

template <typename Ctx, typename T, typename Tag>
void serialize(Ctx& c, strong<T, Tag> const* origin,
               cista::offset_t const offset) {
  serialize(c, &origin->v_, offset);
}

constexpr offset_t integrity_start(mode const m) noexcept {
  offset_t start = 0;
  if (is_mode_enabled(m, mode::WITH_VERSION) ||
      is_mode_enabled(m, mode::WITH_STATIC_VERSION) ||
      is_mode_enabled(m, mode::SKIP_VERSION)) {
    start += sizeof(std::uint64_t);
  }
  return start;
}

constexpr offset_t data_start(mode const m) noexcept {
  auto start = integrity_start(m);
  if (is_mode_enabled(m, mode::WITH_INTEGRITY) ||
      is_mode_enabled(m, mode::SKIP_INTEGRITY)) {
    start += sizeof(std::uint64_t);
  }
  return start;
}

template <mode const Mode = mode::NONE, typename Target, typename T>
void serialize(Target& t, T& value) {
  serialization_context<Target, Mode> c{t};

  if constexpr (is_mode_enabled(Mode, mode::WITH_VERSION) ||
                is_mode_enabled(Mode, mode::WITH_STATIC_VERSION)) {
    static_assert(is_mode_enabled(Mode, mode::WITH_VERSION) ^
                      is_mode_enabled(Mode, mode::WITH_STATIC_VERSION),
                  "WITH_VERSION cannot be combined with WITH_STATIC_VERSION");

    if constexpr (is_mode_enabled(Mode, mode::WITH_VERSION)) {
      auto const h = convert_endian<Mode>(type_hash<decay_t<T>>());
      c.write(&h, sizeof(h));
    } else {
      constexpr auto const type_hash = static_type_hash<decay_t<T>>();
      auto const h = convert_endian<Mode>(type_hash);
      c.write(&h, sizeof(h));
    }
  }

  auto integrity_offset = offset_t{0};
  if constexpr (is_mode_enabled(Mode, mode::WITH_INTEGRITY)) {
    auto const h = hash_t{};
    integrity_offset = c.write(&h, sizeof(h));
  }

  serialize(c, &value,
            c.write(&value, serialized_size<T>(),
                    std::alignment_of_v<decay_t<decltype(value)>>));

  for (auto& p : c.pending_) {
    if (!c.resolve_pointer(p.origin_ptr_, p.pos_, false)) {
      printf("warning: dangling pointer at %" PRI_O " (origin=%p)\n", p.pos_,
             p.origin_ptr_);
    }
  }

  if constexpr (is_mode_enabled(Mode, mode::WITH_INTEGRITY)) {
    auto const csum =
        c.checksum(integrity_offset + static_cast<offset_t>(sizeof(hash_t)));
    c.write(integrity_offset, convert_endian<Mode>(csum));
  }
}

template <mode const Mode = mode::NONE, typename T>
byte_buf serialize(T& el) {
  auto b = buf{};
  serialize<Mode>(b, el);
  return std::move(b.buf_);
}

// =============================================================================
// DESERIALIZE
// -----------------------------------------------------------------------------
template <typename Arg, typename... Args>
Arg checked_addition(Arg a1, Args... aN) {
  using Type = decay_t<Arg>;

  auto add_if_ok = [&](Arg const x) {
    if (x == 0) {
      return;
    }
    if (((x < 0) && (a1 < std::numeric_limits<Type>::min() - x)) ||
        ((x > 0) && (a1 > std::numeric_limits<Type>::max() - x))) {
      throw_exception(std::overflow_error("addition overflow"));
    }
    a1 = a1 + x;
  };
  (add_if_ok(aN), ...);
  return a1;
}

template <typename Arg, typename... Args>
Arg checked_multiplication(Arg a1, Args... aN) {
  using Type = decay_t<Arg>;
  auto multiply_if_ok = [&](auto x) {
    if (a1 != 0 && ((std::numeric_limits<Type>::max() / a1) < x)) {
      throw_exception(std::overflow_error("addition overflow"));
    }
    a1 = a1 * x;
  };
  (multiply_if_ok(aN), ...);
  return a1;
}

template <mode Mode>
struct deserialization_context {
  static constexpr auto const MODE = Mode;

  deserialization_context(std::uint8_t const* from, std::uint8_t const* to)
      : from_{reinterpret_cast<intptr_t>(from)},
        to_{reinterpret_cast<intptr_t>(to)} {}

  template <typename T>
  void convert_endian(T& el) const {
    if constexpr (endian_conversion_necessary<MODE>()) {
      el = ::cista::convert_endian<MODE>(el);
    }
  }

  template <typename Ptr>
  void deserialize_ptr(Ptr** ptr) const {
    auto const offset =
        reinterpret_cast<offset_t>(::cista::convert_endian<MODE>(*ptr));
    static_assert(is_mode_disabled(MODE, mode::_CONST),
                  "raw pointer deserialize is not const");
    *ptr = offset == NULLPTR_OFFSET
               ? nullptr
               : reinterpret_cast<Ptr*>(
                     checked_addition(reinterpret_cast<offset_t>(ptr), offset));
  }

  template <typename T>
  constexpr static std::size_t type_size() noexcept {
    using Type = decay_t<T>;
    if constexpr (std::is_same_v<Type, void>) {
      return 0U;
    } else {
      return sizeof(Type);
    }
  }

  template <typename T>
  void check_ptr(offset_ptr<T> const& el,
                 std::size_t const size = type_size<T>()) const {
    if (el != nullptr) {
      checked_addition(el.offset_, reinterpret_cast<offset_t>(&el));
      check_ptr(el.get(), size);
    }
  }

  template <typename T>
  void check_ptr(T* el, std::size_t const size = type_size<T>()) const {
    if constexpr ((MODE & mode::UNCHECKED) == mode::UNCHECKED) {
      return;
    }

    if (el == nullptr || to_ == 0U) {
      return;
    }

    auto const pos = reinterpret_cast<intptr_t>(el);
    verify(pos >= from_, "underflow");
    verify(checked_addition(pos, static_cast<intptr_t>(size)) <= to_,
           "overflow");
    verify(
        size < static_cast<std::size_t>(std::numeric_limits<intptr_t>::max()),
        "size out of bounds");

    if constexpr (!std::is_same_v<std::remove_const_t<T>, void>) {
      verify((pos & static_cast<intptr_t>(std::alignment_of<decay_t<T>>() -
                                          1U)) == 0U,
             "ptr alignment");
    }
  }

  static void check_bool(bool const& b) {
    auto const val = *reinterpret_cast<std::uint8_t const*>(&b);
    verify(val <= 1U, "valid bool");
  }

  void require(bool condition, char const* msg) const {
    if constexpr ((MODE & mode::UNCHECKED) == mode::UNCHECKED) {
      return;
    }

    verify(condition, msg);
  }

  intptr_t from_, to_;
};

template <mode Mode>
struct deep_check_context : public deserialization_context<Mode> {
  using parent = deserialization_context<Mode>;

  using parent::parent;

  template <typename T>
  bool add_checked(T const* v) const {
    return checked_.emplace(type_hash<T>(), static_cast<void const*>(v)).second;
  }

  std::set<std::pair<hash_t, void const*>> mutable checked_;
};

template <typename T, mode const Mode = mode::NONE>
void check(std::uint8_t const* const from, std::uint8_t const* const to) {
  verify(to - from > data_start(Mode), "invalid range");

  if constexpr ((Mode & mode::WITH_VERSION) == mode::WITH_VERSION) {
    verify(convert_endian<Mode>(*reinterpret_cast<hash_t const*>(from)) ==
               type_hash<T>(),
           "invalid version");
  } else if constexpr ((Mode & mode::WITH_STATIC_VERSION) ==
                       mode::WITH_STATIC_VERSION) {
    verify(convert_endian<Mode>(*reinterpret_cast<hash_t const*>(from)) ==
               static_type_hash<T>(),
           "invalid static version");
  }

  if constexpr ((Mode & mode::WITH_INTEGRITY) == mode::WITH_INTEGRITY) {
    verify(convert_endian<Mode>(*reinterpret_cast<std::uint64_t const*>(
               from + integrity_start(Mode))) ==
               hash(std::string_view{
                   reinterpret_cast<char const*>(from + data_start(Mode)),
                   static_cast<std::size_t>(to - from - data_start(Mode))}),
           "invalid checksum");
  }
}

// --- GENERIC ---
template <typename Ctx, typename T>
void convert_endian_and_ptr(Ctx const& c, T* el) {
  using Type = decay_t<T>;
  if constexpr (std::is_pointer_v<Type>) {
    c.deserialize_ptr(el);
  } else if constexpr (std::numeric_limits<Type>::is_integer) {
    c.convert_endian(*el);
  } else {
    CISTA_UNUSED_PARAM(c)
    CISTA_UNUSED_PARAM(el)
  }
}

template <typename Ctx, typename T>
void check_state(Ctx const& c, T* el) {
  using Type = decay_t<T>;
  if constexpr (std::is_pointer_v<Type> && !std::is_same_v<Type, void*>) {
    c.check_ptr(*el);
  } else {
    CISTA_UNUSED_PARAM(c)
    CISTA_UNUSED_PARAM(el)
  }
}

template <typename Ctx, typename T, typename Fn>
void recurse(Ctx& c, T* el, Fn&& fn) {
  using Type = decay_t<T>;
  if constexpr (is_indexed_v<Type>) {
    fn(static_cast<typename T::value_type*>(el));
  } else if constexpr (to_tuple_works_v<Type>) {
    for_each_ptr_field(*el, [&](auto& f) { fn(f); });
  } else if constexpr (is_mode_enabled(Ctx::MODE, mode::_PHASE_II) &&
                       std::is_pointer_v<Type>) {
    if (*el != nullptr && c.add_checked(el)) {
      fn(*el);
    }
  } else {
    CISTA_UNUSED_PARAM(c)
    CISTA_UNUSED_PARAM(el)
    CISTA_UNUSED_PARAM(fn)
  }
}

template <typename Ctx, typename T>
void deserialize(Ctx const& c, T* el) {
  c.check_ptr(el);
  if constexpr (is_mode_disabled(Ctx::MODE, mode::_PHASE_II)) {
    convert_endian_and_ptr(c, el);
  }
  if constexpr (is_mode_disabled(Ctx::MODE, mode::UNCHECKED)) {
    check_state(c, el);
  }
  recurse(c, el, [&](auto* entry) { deserialize(c, entry); });
}

// --- PAIR<A,B> ---
template <typename Ctx, typename A, typename B, typename Fn>
void recurse(Ctx&, pair<A, B>* el, Fn&& fn) {
  fn(&el->first);
  fn(&el->second);
}

template <typename Ctx, typename A, typename B, typename Fn>
void recurse(Ctx&, std::pair<A, B>* el, Fn&& fn) {
  fn(&el->first);
  fn(&el->second);
}

// --- OFFSET_PTR<T> ---
template <typename Ctx, typename T>
void convert_endian_and_ptr(Ctx const& c, offset_ptr<T>* el) {
  c.convert_endian(el->offset_);
}

template <typename Ctx, typename T>
void check_state(Ctx const& c, offset_ptr<T>* el) {
  c.check_ptr(*el);
}

template <typename Ctx, typename T, typename Fn>
void recurse(Ctx& c, offset_ptr<T>* el, Fn&& fn) {
  if constexpr (is_mode_enabled(Ctx::MODE, mode::_PHASE_II)) {
    if (*el != nullptr && c.add_checked(el)) {
      fn(static_cast<T*>(*el));
    }
  } else {
    CISTA_UNUSED_PARAM(c)
    CISTA_UNUSED_PARAM(el)
    CISTA_UNUSED_PARAM(fn)
  }
}

// --- VECTOR<T> ---
template <typename Ctx, typename T, template <typename> typename Ptr,
          bool Indexed, typename TemplateSizeType>
void convert_endian_and_ptr(
    Ctx const& c, basic_vector<T, Ptr, Indexed, TemplateSizeType>* el) {
  deserialize(c, &el->el_);
  c.convert_endian(el->allocated_size_);
  c.convert_endian(el->used_size_);
}

template <typename Ctx, typename T, template <typename> typename Ptr,
          bool Indexed, typename TemplateSizeType>
void check_state(Ctx const& c,
                 basic_vector<T, Ptr, Indexed, TemplateSizeType>* el) {
  c.check_ptr(el->el_,
              checked_multiplication(
                  static_cast<std::size_t>(el->allocated_size_), sizeof(T)));
  c.check_bool(el->self_allocated_);
  c.require(!el->self_allocated_, "vec self-allocated");
  c.require(el->allocated_size_ == el->used_size_, "vec size mismatch");
  c.require((el->size() == 0U) == (el->el_ == nullptr), "vec size=0 <=> ptr=0");
}

template <typename Ctx, typename T, template <typename> typename Ptr,
          bool Indexed, typename TemplateSizeType, typename Fn>
void recurse(Ctx&, basic_vector<T, Ptr, Indexed, TemplateSizeType>* el,
             Fn&& fn) {
  for (auto& m : *el) {  // NOLINT(clang-analyzer-core.NullDereference)
    fn(&m);
  }
}

// --- STRING ---
template <typename Ctx, typename Ptr>
void convert_endian_and_ptr(Ctx const& c, generic_string<Ptr>* el) {
  if (*reinterpret_cast<std::uint8_t const*>(&el->s_.is_short_) == 0U) {
    deserialize(c, &el->h_.ptr_);
    c.convert_endian(el->h_.size_);
  }
}

template <typename Ctx, typename Ptr>
void check_state(Ctx const& c, generic_string<Ptr>* el) {
  c.check_bool(el->s_.is_short_);
  if (!el->is_short()) {
    c.check_ptr(el->h_.ptr_, el->h_.size_);
    c.check_bool(el->h_.self_allocated_);
    c.require(!el->h_.self_allocated_, "string self-allocated");
    c.require((el->h_.size_ == 0) == (el->h_.ptr_ == nullptr),
              "str size=0 <=> ptr=0");
  }
}

template <typename Ctx, typename Ptr, typename Fn>
void recurse(Ctx&, generic_string<Ptr>*, Fn&&) {}

template <typename Ctx, typename Ptr>
void convert_endian_and_ptr(Ctx const& c, basic_string<Ptr>* el) {
  convert_endian_and_ptr(c, static_cast<generic_string<Ptr>*>(el));
}

template <typename Ctx, typename Ptr>
void check_state(Ctx const& c, basic_string<Ptr>* el) {
  check_state(c, static_cast<generic_string<Ptr>*>(el));
}

template <typename Ctx, typename Ptr, typename Fn>
void recurse(Ctx&, basic_string<Ptr>*, Fn&&) {}

template <typename Ctx, typename Ptr>
void convert_endian_and_ptr(Ctx const& c, basic_string_view<Ptr>* el) {
  convert_endian_and_ptr(c, static_cast<generic_string<Ptr>*>(el));
}

template <typename Ctx, typename Ptr>
void check_state(Ctx const& c, basic_string_view<Ptr>* el) {
  check_state(c, static_cast<generic_string<Ptr>*>(el));
}

template <typename Ctx, typename Ptr, typename Fn>
void recurse(Ctx&, basic_string_view<Ptr>*, Fn&&) {}

// --- UNIQUE_PTR<T> ---
template <typename Ctx, typename T, typename Ptr>
void convert_endian_and_ptr(Ctx const& c, basic_unique_ptr<T, Ptr>* el) {
  deserialize(c, &el->el_);
}

template <typename Ctx, typename T, typename Ptr>
void check_state(Ctx const& c, basic_unique_ptr<T, Ptr>* el) {
  c.check_bool(el->self_allocated_);
  c.require(!el->self_allocated_, "unique_ptr self-allocated");
}

template <typename Ctx, typename T, typename Ptr, typename Fn>
void recurse(Ctx&, basic_unique_ptr<T, Ptr>* el, Fn&& fn) {
  if (el->el_ != nullptr) {
    fn(static_cast<T*>(el->el_));
  }
}

// --- MUTABLE_FWS_MULTIMAP<T> ---
template <typename Ctx, typename T, typename SizeType,
          template <typename> typename Vec, std::size_t Log2MaxEntriesPerBucket,
          typename Fn>
void recurse(
    Ctx&,
    dynamic_fws_multimap_base<T, SizeType, Vec, Log2MaxEntriesPerBucket>* el,
    Fn&& fn) {
  fn(&el->index_);
  fn(&el->data_);
  fn(&el->free_buckets_);
  fn(&el->element_count_);
}

// --- HASH_STORAGE<T> ---
template <typename Ctx, typename T, template <typename> typename Ptr,
          typename GetKey, typename GetValue, typename Hash, typename Eq>
void convert_endian_and_ptr(
    Ctx const& c, hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq>* el) {
  deserialize(c, &el->entries_);
  deserialize(c, &el->ctrl_);
  c.convert_endian(el->size_);
  c.convert_endian(el->capacity_);
  c.convert_endian(el->growth_left_);
}

template <typename Ctx, typename T, template <typename> typename Ptr,
          typename GetKey, typename GetValue, typename Hash, typename Eq>
void check_state(Ctx const& c,
                 hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq>* el) {
  using Type = decay_t<remove_pointer_t<decltype(el)>>;
  c.require(el->ctrl_ != nullptr, "hash storage: ctrl must be set");
  c.check_ptr(
      el->entries_,
      checked_addition(
          checked_multiplication(
              el->capacity_, static_cast<typename Type::size_type>(sizeof(T))),
          checked_addition(el->capacity_, 1U, Type::WIDTH)));
  c.check_ptr(el->ctrl_, checked_addition(el->capacity_, 1U, Type::WIDTH));
  c.require(
      el->entries_ == nullptr ||
          reinterpret_cast<std::uint8_t const*>(ptr_cast(el->ctrl_)) ==
              reinterpret_cast<std::uint8_t const*>(ptr_cast(el->entries_)) +
                  checked_multiplication(
                      static_cast<std::size_t>(el->capacity_), sizeof(T)),
      "hash storage: entries!=null -> ctrl = entries+capacity");
  c.require(
      (el->entries_ == nullptr) == (el->capacity_ == 0U && el->size_ == 0U),
      "hash storage: entries=null <=> size=capacity=0");

  c.check_bool(el->self_allocated_);
  c.require(!el->self_allocated_, "hash storage: self-allocated");

  c.require(el->ctrl_[el->capacity_] == Type::END,
            "hash storage: end ctrl byte");
  c.require(std::all_of(ptr_cast(el->ctrl_),
                        ptr_cast(el->ctrl_) + el->capacity_ + 1U + Type::WIDTH,
                        [](typename Type::ctrl_t const ctrl) {
                          return Type::is_empty(ctrl) ||
                                 Type::is_deleted(ctrl) ||
                                 Type::is_full(ctrl) ||
                                 ctrl == Type::ctrl_t::END;
                        }),
            "hash storage: ctrl bytes must be empty or deleted or full");

  using st_t = typename Type::size_type;
  auto [total_empty, total_full, total_deleted, total_growth_left] =
      std::accumulate(
          ptr_cast(el->ctrl_), ptr_cast(el->ctrl_) + el->capacity_,
          std::tuple{st_t{0U}, st_t{0U}, st_t{0U}, st_t{0}},
          [&](std::tuple<st_t, st_t, st_t, st_t> const acc,
              typename Type::ctrl_t const& ctrl) {
            auto const [empty, full, deleted, growth_left] = acc;
            return std::tuple{
                Type::is_empty(ctrl) ? empty + 1 : empty,
                Type::is_full(ctrl) ? full + 1 : full,
                Type::is_deleted(ctrl) ? deleted + 1 : deleted,
                (Type::is_empty(ctrl) && el->was_never_full(static_cast<st_t>(
                                             &ctrl - el->ctrl_))
                     ? growth_left + 1
                     : growth_left)};
          });

  c.require(el->size_ == total_full, "hash storage: size");
  c.require(total_empty + total_full + total_deleted == el->capacity_,
            "hash storage: empty + full + deleted = capacity");
  c.require(std::min(Type::capacity_to_growth(el->capacity_) - el->size_,
                     total_growth_left) <= el->growth_left_,
            "hash storage: growth left");
}

template <typename Ctx, typename T, template <typename> typename Ptr,
          typename GetKey, typename GetValue, typename Hash, typename Eq,
          typename Fn>
void recurse(Ctx&, hash_storage<T, Ptr, GetKey, GetValue, Hash, Eq>* el,
             Fn&& fn) {
  for (auto& m : *el) {
    fn(&m);
  }
}

// --- BITSET<SIZE> ---
template <typename Ctx, std::size_t Size, typename Fn>
void recurse(Ctx&, bitset<Size>* el, Fn&& fn) {
  fn(&el->blocks_);
}

// --- ARRAY<T> ---
template <typename Ctx, typename T, std::size_t Size, typename Fn>
void recurse(Ctx&, array<T, Size>* el, Fn&& fn) {
  for (auto& m : *el) {
    fn(&m);
  }
}

// --- VARIANT<T...> ---
template <typename Ctx, typename Fn, typename... T>
void convert_endian_and_ptr(Ctx const& c, variant<T...>* el) {
  c.convert_endian(el->idx_);
}

template <typename Ctx, typename Fn, typename... T>
void recurse(Ctx&, variant<T...>* el, Fn&& fn) {
  el->apply([&](auto&& t) { fn(&t); });
}

template <typename Ctx, typename... T>
void check_state(Ctx const& c, variant<T...>* el) {
  c.require(el->index() < sizeof...(T), "variant index");
}

// --- OPTIONAL<T> ---
template <typename Ctx, typename T>
void check_state(Ctx const& c, optional<T>* el) {
  c.check_bool(el->valid_);
}

template <typename Ctx, typename Fn, typename T>
void recurse(Ctx&, optional<T>* el, Fn&& fn) {
  if (el->valid_) {
    fn(&(el->value()));
  }
}

// --- TUPLE<T...> ---
template <typename Ctx, typename... T>
void recurse(Ctx const& c, tuple<T...>* el) {
  apply([&](auto&&... args) { (deserialize(c, &args), ...); }, *el);
}

// --- TIMEPOINT ---
template <typename Ctx, typename Clock, typename Dur>
void convert_endian_and_ptr(Ctx const& c,
                            std::chrono::time_point<Clock, Dur>* el) {
  c.convert_endian(*reinterpret_cast<typename Dur::rep*>(el));
}

// --- DURATION ---
template <typename Ctx, typename Rep, typename Period>
void convert_endian_and_ptr(Ctx const& c,
                            std::chrono::duration<Rep, Period>* el) {
  c.convert_endian(*reinterpret_cast<Rep*>(el));
}

template <typename T, mode const Mode = mode::NONE>
T* deserialize(std::uint8_t* from, std::uint8_t* to = nullptr) {
  if constexpr (is_mode_enabled(Mode, mode::CAST)) {
    CISTA_UNUSED_PARAM(to)
    return reinterpret_cast<T*>(from);
  } else {
    check<T, Mode>(from, to);
    auto const el = reinterpret_cast<T*>(from + data_start(Mode));

    deserialization_context<Mode> c{from, to};
    deserialize(c, el);

    if constexpr ((Mode & mode::DEEP_CHECK) == mode::DEEP_CHECK) {
      deep_check_context<Mode | mode::_PHASE_II> c1{from, to};
      deserialize(c1, el);
    }

    return el;
  }
}

template <typename T, mode const Mode = mode::NONE>
T const* deserialize(std::uint8_t const* from,
                     std::uint8_t const* to = nullptr) {
  static_assert(!endian_conversion_necessary<Mode>(), "cannot be const");
  return deserialize<T, Mode | mode::_CONST>(const_cast<std::uint8_t*>(from),
                                             const_cast<std::uint8_t*>(to));
}

template <typename T, mode const Mode = mode::NONE, typename CharT>
T const* deserialize(CharT const* from, CharT const* to = nullptr) {
  static_assert(sizeof(CharT) == 1U, "byte size entries");
  return deserialize<T, Mode>(reinterpret_cast<std::uint8_t const*>(from),
                              reinterpret_cast<std::uint8_t const*>(to));
}

template <typename T, mode const Mode = mode::NONE, typename CharT>
T* deserialize(CharT* from, CharT* to = nullptr) {
  static_assert(sizeof(CharT) == 1U, "byte size entries");
  return deserialize<T, Mode>(reinterpret_cast<std::uint8_t*>(from),
                              reinterpret_cast<std::uint8_t*>(to));
}

template <typename T, mode const Mode = mode::NONE>
T const* deserialize(std::string_view c) {
  return deserialize<T const, Mode>(&c[0], &c[0] + c.size());
}

template <typename T, mode const Mode = mode::NONE, typename Container>
auto deserialize(Container& c) {
  return deserialize<T, Mode>(&c[0], &c[0] + c.size());
}

template <typename T, mode const Mode = mode::NONE>
T* unchecked_deserialize(std::uint8_t* from, std::uint8_t* to = nullptr) {
  return deserialize<T, Mode | mode::UNCHECKED>(from, to);
}

template <typename T, mode const Mode = mode::NONE, typename Container>
T* unchecked_deserialize(Container& c) {
  return unchecked_deserialize<T, Mode>(&c[0], &c[0] + c.size());
}

template <typename T, mode const Mode = mode::NONE>
T copy_from_potentially_unaligned(std::string_view buf) {
  struct aligned {
    explicit aligned(std::string_view buf)
        : mem_{static_cast<std::uint8_t*>(
              CISTA_ALIGNED_ALLOC(sizeof(max_align_t), buf.size()))} {
      verify(mem_ != nullptr, "failed to allocate aligned memory");
      std::memcpy(mem_, buf.data(), buf.size());
    }
    ~aligned() { CISTA_ALIGNED_FREE(sizeof(max_align_t), mem_); }
    std::uint8_t* mem_;
  };

  auto const is_already_aligned =
      (reinterpret_cast<std::uintptr_t>(buf.data()) % sizeof(max_align_t)) ==
      0U;
  if (is_already_aligned) {
    return *deserialize<T, Mode>(buf);
  }
  auto copy = aligned{buf};
  return *deserialize<T, Mode>(copy.mem_, copy.mem_ + buf.size());
}

namespace raw {
using cista::deserialize;
using cista::unchecked_deserialize;
}  // namespace raw

namespace offset {
using cista::deserialize;
using cista::unchecked_deserialize;
}  // namespace offset

}  // namespace cista

#undef cista_member_offset


#define CISTA_COMPARABLE()                               \
  template <typename T>                                  \
  bool operator==(T&& b) const {                         \
    return cista::to_tuple(*this) == cista::to_tuple(b); \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator!=(T&& b) const {                         \
    return cista::to_tuple(*this) != cista::to_tuple(b); \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator<(T&& b) const {                          \
    return cista::to_tuple(*this) < cista::to_tuple(b);  \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator<=(T&& b) const {                         \
    return cista::to_tuple(*this) <= cista::to_tuple(b); \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator>(T&& b) const {                          \
    return cista::to_tuple(*this) > cista::to_tuple(b);  \
  }                                                      \
                                                         \
  template <typename T>                                  \
  bool operator>=(T&& b) const {                         \
    return cista::to_tuple(*this) >= cista::to_tuple(b); \
  }

#define CISTA_FRIEND_COMPARABLE(class_name)                          \
  friend bool operator==(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) == cista::to_tuple(b);                 \
  }                                                                  \
                                                                     \
  friend bool operator!=(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) != cista::to_tuple(b);                 \
  }                                                                  \
                                                                     \
  friend bool operator<(class_name const& a, class_name const& b) {  \
    return cista::to_tuple(a) < cista::to_tuple(b);                  \
  }                                                                  \
                                                                     \
  friend bool operator<=(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) <= cista::to_tuple(b);                 \
  }                                                                  \
                                                                     \
  friend bool operator>(class_name const& a, class_name const& b) {  \
    return cista::to_tuple(a) > cista::to_tuple(b);                  \
  }                                                                  \
                                                                     \
  friend bool operator>=(class_name const& a, class_name const& b) { \
    return cista::to_tuple(a) >= cista::to_tuple(b);                 \
  }

#include <array>
#include <ostream>

#ifndef CISTA_PRINTABLE_NO_VEC
#include <vector>
#endif


#ifndef CISTA_PRINTABLE_NO_VEC
template <typename T>
std::ostream& operator<<(std::ostream& out, std::vector<T> const& v) {
  out << "[\n  ";
  auto first = true;
  for (auto const& e : v) {
    if (!first) {
      out << ",\n  ";
    }
    using Type = cista::decay_t<T>;
    if constexpr (std::is_enum_v<Type>) {
      out << static_cast<std::underlying_type_t<Type>>(e);
    } else {
      out << e;
    }
    first = false;
  }
  return out << "\n]";
}
#endif

template <typename... T>
constexpr std::array<char const*, sizeof...(T)> to_str_array(T... args) {
  return {args...};
}

#define CISTA_PRINTABLE(class_name, ...)                                    \
  friend std::ostream& operator<<(std::ostream& out, class_name const& o) { \
    constexpr auto const names = to_str_array(__VA_ARGS__);                 \
    bool first = true;                                                      \
    out << '{';                                                             \
    std::size_t i = 0U;                                                     \
    ::cista::for_each_field(o, [&](auto&& f) {                              \
      using Type = ::cista::decay_t<decltype(f)>;                           \
      if (!first) {                                                         \
        out << ", ";                                                        \
      } else {                                                              \
        first = false;                                                      \
      }                                                                     \
      if (i < names.size()) {                                               \
        out << names[i] << '=';                                             \
      }                                                                     \
      if constexpr (std::is_enum_v<Type>) {                                 \
        out << static_cast<std::underlying_type_t<Type>>(f);                \
      } else {                                                              \
        out << f;                                                           \
      }                                                                     \
      ++i;                                                                  \
    });                                                                     \
    return out << '}';                                                      \
  }


#include <limits>
#include <type_traits>

namespace cista {

template <typename T, typename MemberType>
std::size_t member_index(MemberType T::*const member_ptr) {
  auto i = 0U, field_index = std::numeric_limits<unsigned>::max();
  T t{};
  cista::for_each_field(t, [&](auto&& m) {
    if constexpr (std::is_same_v<decltype(&m), decltype(&(t.*member_ptr))>) {
      if (&m == &(t.*member_ptr)) {
        field_index = i;
      }
    }
    ++i;
  });
  return field_index;
}

}  // namespace cista
