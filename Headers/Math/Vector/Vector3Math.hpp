//
// Created by berke on 5/29/2026.
//

#ifndef TILKY_ENGINE_VECTOR3MATH_HPP
#define TILKY_ENGINE_VECTOR3MATH_HPP


#include "Vector3.hpp"

#include <immintrin.h>

#include "../Constants.hpp"

namespace Vector3Math {

    inline float Dot(const Vector3& a, const Vector3& b) {
        const __m128 dotReg = _mm_dp_ps(a.reg, b.reg, 0x71);
        return _mm_cvtss_f32(dotReg);
    }

    inline Vector3 Cross(const Vector3& a, const Vector3& b) {
        const __m128 leftA = _mm_shuffle_ps(a.reg, a. reg, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 leftB  = _mm_shuffle_ps(b.reg, b.reg, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 leftResult = _mm_mul_ps(leftA, leftB);

        const __m128 rightA = _mm_shuffle_ps(a.reg, a.reg, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 rightB = _mm_shuffle_ps(b.reg, b.reg, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 rightResult = _mm_mul_ps(rightA, rightB);

        return {_mm_sub_ps(leftResult, rightResult)};
    }

    inline float LengthSquared(const Vector3& v) {
        const __m128 lenSqReg = _mm_dp_ps(v.reg, v.reg, 0x71);
        return _mm_cvtss_f32(lenSqReg);
    }

    inline float Length(const Vector3& v) {
        const __m128 lenSqReg = _mm_dp_ps(v.reg, v.reg, 0x71);
        const __m128 lenReg = _mm_sqrt_ss(lenSqReg);
        return _mm_cvtss_f32(lenReg);
    }

    inline Vector3 Normalized(const Vector3& v) {
        const __m128 lenSq = _mm_dp_ps(v.reg, v.reg, 0x71);
        const __m128 len   = _mm_sqrt_ss(lenSq);

        if (_mm_cvtss_f32(len) == 0.0f) return Vector3{};

        const __m128 ones = _mm_set1_ps(1.0f);
        __m128 invLen = _mm_div_ps(ones, len);
        invLen = _mm_shuffle_ps(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_mul_ps(v.reg, invLen)};
    }

    inline void Normalize(Vector3& v) { v = Normalized(v); }

    inline float DistanceSquared(const Vector3& a, const Vector3& b) {
        // 1. Subtract all lanes in parallel to get the delta vector (a - b)
        __m128 delta = _mm_sub_ps(a.reg, b.reg);

        // 2. Dot the delta vector with itself to get the squared distance
        __m128 distSqReg = _mm_dp_ps(delta, delta, 0x71);

        // 3. Extract and return the float
        return _mm_cvtss_f32(distSqReg);
    }

    inline float Distance(const Vector3& a, const Vector3& b) {
        const __m128 delta = _mm_sub_ps(a.reg, b.reg);

        const __m128 distSqReg = _mm_dp_ps(delta, delta, 0x71);
        const __m128 distReg = _mm_sqrt_ss(distSqReg);

        return _mm_cvtss_f32(distReg);
    }
}

#endif //TILKY_ENGINE_VECTOR3MATH_HPP