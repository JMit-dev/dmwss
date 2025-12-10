#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <functional>

// Explicit sized types
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Force inline for hot paths
#if defined(_MSC_VER)
    #define FORCE_INLINE __forceinline
    #define NEVER_INLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define NEVER_INLINE __attribute__((noinline))
#else
    #define FORCE_INLINE inline
    #define NEVER_INLINE
#endif

// Branch prediction hints
#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x) __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x) (x)
    #define UNLIKELY(x) (x)
#endif

// Alignment for SIMD
#define ALIGN(x) alignas(x)

// Bit manipulation helpers
constexpr u8 get_bit(u8 value, u8 bit) {
    return (value >> bit) & 1;
}

constexpr u8 set_bit(u8 value, u8 bit, bool set) {
    return set ? (value | (1 << bit)) : (value & ~(1 << bit));
}

// Sign extension
constexpr s16 sign_extend_8(u8 value) {
    return static_cast<s16>(static_cast<s8>(value));
}
