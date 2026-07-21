//
// Created by berke on 5/29/2026.
//

#ifndef TILKY_ENGINE_VECTOR3MATH_HPP
#define TILKY_ENGINE_VECTOR3MATH_HPP

#include "Vector3.hpp"
#include "../SIMD/SSECompat.hpp"

#ifndef NOSIMD
namespace Vector3Math {
    // Helper: horizontal dot product of 3D vectors (x*x + y*y + z*z).
    // Assumes the w lane is 0 or irrelevant — only x, y, z are summed.
    inline float Dot3(const __m128 a, const __m128 b) {
        const __m128 mul  = _mm_mul_ps(a, b);               // [ax*bx, ay*by, az*bz, ?]

        // Shift left by one lane: [ay*by, az*bz, ?, ?]
        const __m128 shuf1 = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2, 1, 0, 1));
        const __m128 sum1  = _mm_add_ss(mul, shuf1);        // [ax*bx + ay*by, ...]

        // Bring az*bz into lane 0: [az*bz, ...]
        const __m128 shuf2 = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2, 2, 2, 2));
        const __m128 sum2  = _mm_add_ss(sum1, shuf2);       // [ax*bx + ay*by + az*bz, ...]

        return _mm_cvtss_f32(sum2);
    }

    inline float Dot(const Vector3& a, const Vector3& b) {
        return Dot3(a.reg, b.reg);
    }

    inline Vector3 Cross(const Vector3& a, const Vector3& b) {
        const __m128 leftA      = TILKY_MM_SHUFFLE_PS(a.reg, a.reg, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 leftB      = TILKY_MM_SHUFFLE_PS(b.reg, b.reg, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 leftResult = _mm_mul_ps(leftA, leftB);

        const __m128 rightA      = TILKY_MM_SHUFFLE_PS(a.reg, a.reg, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 rightB      = TILKY_MM_SHUFFLE_PS(b.reg, b.reg, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 rightResult = _mm_mul_ps(rightA, rightB);

        return {_mm_sub_ps(leftResult, rightResult)};
    }

    inline float LengthSquared(const Vector3& v) {
        return Dot3(v.reg, v.reg);
    }

    inline float Length(const Vector3& v) {
        const __m128 lenReg = _mm_sqrt_ss(_mm_set_ss(Dot3(v.reg, v.reg)));
        return _mm_cvtss_f32(lenReg);
    }

    inline Vector3 Normalized(const Vector3& v) {
        const __m128 len = _mm_sqrt_ss(_mm_set_ss(Dot3(v.reg, v.reg)));

        if (_mm_cvtss_f32(len) == 0.0f) return Vector3{};

        const __m128 ones = _mm_set1_ps(1.0f);
        __m128 invLen     = _mm_div_ss(ones, len);
        invLen            = TILKY_MM_SHUFFLE_PS(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_mul_ps(v.reg, invLen)};
    }

    inline void Normalize(Vector3& v) { v = Normalized(v); }

    inline float DistanceSquared(const Vector3& a, const Vector3& b) {
        // 1. Subtract all lanes in parallel to get the delta vector (a - b)
        const __m128 delta = _mm_sub_ps(a.reg, b.reg);

        // 2. Dot the delta vector with itself to get the squared distance
        // 3. Extract and return the float
        return Dot3(delta, delta);
    }

    inline float Distance(const Vector3& a, const Vector3& b) {
        const __m128 delta   = _mm_sub_ps(a.reg, b.reg);
        const __m128 distReg = _mm_sqrt_ss(_mm_set_ss(Dot3(delta, delta)));
        return _mm_cvtss_f32(distReg);
    }
}
#else
namespace Vector3Math {
    inline float Dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    inline float LengthSquared(const Vector3& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    inline float Length(const Vector3& v) {
        return std::sqrt(LengthSquared(v));
    }

    inline Vector3 Normalized(const Vector3& v) {
        const float lenSq = LengthSquared(v);

        if (lenSq == 0.0f) return Vector3{};

        const float invLen = 1.0f / std::sqrt(lenSq);

        return {
            v.x * invLen,
            v.y * invLen,
            v.z * invLen
        };
    }

    inline void Normalize(Vector3& v) {
        v = Normalized(v);
    }

    inline float DistanceSquared(const Vector3& a, const Vector3& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;

        return dx * dx + dy * dy + dz * dz;
    }

    inline float Distance(const Vector3& a, const Vector3& b) {
        return std::sqrt(DistanceSquared(a, b));
    }
}
#endif

#endif //TILKY_ENGINE_VECTOR3MATH_HPP