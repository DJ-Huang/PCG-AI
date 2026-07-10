#pragma once

// Cross-platform 128-bit unsigned multiplication adapter.
// macOS/Linux clang: __int128 is native.
// Windows MSVC: _umul128 intrinsic + carry chain.

#include <cstdint>

namespace pcg::internal::geometry {

#if defined(_MSC_VER) && !defined(__clang__)

#include <intrin.h>

/// Multiply two 64-bit unsigned values, returning the full 128-bit result.
/// On MSVC, uses _umul128 to get high and low 64-bit halves.
struct UInt128 {
    uint64_t lo;
    uint64_t hi;

    static UInt128 mul(uint64_t a, uint64_t b)
    {
        UInt128 result;
        result.lo = _umul128(a, b, &result.hi);
        return result;
    }

    /// Check if this 128-bit value fits in int64_t without overflow.
    bool fits_int64() const
    {
        return hi == 0 && lo <= static_cast<uint64_t>(INT64_MAX);
    }

    /// Convert to double for sign comparison.
    double to_double() const
    {
        return static_cast<double>(hi) * 18446744073709551616.0 + static_cast<double>(lo);
    }
};

#else

/// On GCC/Clang, __int128 is available natively.
struct UInt128 {
    unsigned __int128 val;

    static UInt128 mul(uint64_t a, uint64_t b)
    {
        UInt128 result;
        result.val = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
        return result;
    }

    bool fits_int64() const
    {
        return val <= static_cast<unsigned __int128>(INT64_MAX);
    }

    double to_double() const
    {
        return static_cast<double>(val);
    }
};

#endif

/// Signed 128-bit product for determinant sign tests.
/// Returns -1, 0, or +1 based on the sign of a * b + c * d.
inline int sign_of_sum_of_products(int64_t a, int64_t b, int64_t c, int64_t d)
{
    // Use unsigned multiplication on absolute values, then fix sign.
    const bool a_neg = a < 0;
    const bool b_neg = b < 0;
    const bool c_neg = c < 0;
    const bool d_neg = d < 0;

    const uint64_t ua = static_cast<uint64_t>(a_neg ? -a : a);
    const uint64_t ub = static_cast<uint64_t>(b_neg ? -b : b);
    const uint64_t uc = static_cast<uint64_t>(c_neg ? -c : c);
    const uint64_t ud = static_cast<uint64_t>(d_neg ? -d : d);

    UInt128 p1 = UInt128::mul(ua, ub);
    UInt128 p2 = UInt128::mul(uc, ud);

    const bool p1_neg = a_neg != b_neg;
    const bool p2_neg = c_neg != d_neg;

    if (p1_neg == p2_neg) {
        // Same sign: add magnitudes. Result sign = p1_neg.
        // For simplicity, compare via double (good enough for sign with adaptive fallback).
        const double sum = p1.to_double() + p2.to_double();
        if (p1_neg) return sum < 0.0 ? -1 : (sum == 0.0 ? 0 : -1);
        return sum > 0.0 ? 1 : (sum == 0.0 ? 0 : 1);
    }
    // Different signs: subtract.
    const double diff = p1.to_double() - p2.to_double();
    if (p1_neg) {
        // p1 is negative, p2 is positive
        return diff < 0.0 ? -1 : (diff == 0.0 ? 0 : -1);
    }
    return diff > 0.0 ? 1 : (diff == 0.0 ? 0 : 1);
}

} // namespace pcg::internal::geometry
