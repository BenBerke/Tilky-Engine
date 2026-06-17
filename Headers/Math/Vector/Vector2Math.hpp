//
// Created by berke on 5/29/2026.
//

#ifndef TILKY_ENGINE_VECTOR2MATH_HPP
#define TILKY_ENGINE_VECTOR2MATH_HPP

#include "Vector2.hpp"
#include <immintrin.h>

#include "../Constants.hpp"

namespace Vector2Math {

    inline float Dot(const Vector2& a, const Vector2& b) {
        const __m128 dotReg = _mm_dp_ps(a.reg, b.reg, 0x31);
        return _mm_cvtss_f32(dotReg);
    }

    inline float LengthSquared(const Vector2& v) {
        const __m128 lenSqReg = _mm_dp_ps(v.reg, v.reg, 0x31);
        return _mm_cvtss_f32(lenSqReg);
    }

    inline float Length(const Vector2& v) {
        const __m128 lenSqReg = _mm_dp_ps(v.reg, v.reg, 0x31);
        const __m128 lenReg   = _mm_sqrt_ss(lenSqReg);
        return _mm_cvtss_f32(lenReg);
    }

    inline Vector2 Normalized(const Vector2& v) {
        const __m128 lenSqReg = _mm_dp_ps(v.reg, v.reg, 0x31);
        const __m128 lenReg   = _mm_sqrt_ss(lenSqReg);

        if (_mm_cvtss_f32(lenReg) == 0.0f) return Vector2();

        const __m128 ones   = _mm_set1_ps(1.0f);
        __m128 invLen = _mm_div_ps(ones, lenReg);
        invLen = _mm_shuffle_ps(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_mul_ps(v.reg, invLen)};
    }

    inline void Normalize(Vector2& v) { v = Normalized(v); }

    inline float Cross(const Vector2& a, const Vector2& b) {
        const __m128 switchedB = _mm_shuffle_ps(b.reg, b.reg, _MM_SHUFFLE(3, 2, 0, 1));

        __m128 multiplied = _mm_mul_ps(a.reg, switchedB);
        const __m128 shifted = _mm_shuffle_ps(multiplied, multiplied, _MM_SHUFFLE(3, 2, 1, 1));
        const __m128 result = _mm_sub_ss(multiplied, shifted);

        return _mm_cvtss_f32(result);
    }

    inline float DistanceSquared(const Vector2& a, const Vector2& b) {
        __m128 delta = _mm_sub_ps(a.reg, b.reg);

        const __m128 distSqReg = _mm_dp_ps(delta, delta, 0x31);
        return _mm_cvtss_f32(distSqReg);
    }

    inline float Distance(const Vector2& a, const Vector2& b) {
        __m128 delta     = _mm_sub_ps(a.reg, b.reg);
        const __m128 distSqReg = _mm_dp_ps(delta, delta, 0x31);
        __m128 distReg   = _mm_sqrt_ss(distSqReg);
        return _mm_cvtss_f32(distReg);
    }
}

#endif //TILKY_ENGINE_VECTOR2MATH_HPP