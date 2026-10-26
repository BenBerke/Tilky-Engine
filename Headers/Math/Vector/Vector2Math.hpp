//
// Created by berke on 5/29/2026.
//

#ifndef TILKY_ENGINE_VECTOR2MATH_HPP
#define TILKY_ENGINE_VECTOR2MATH_HPP

#include "Vector2.hpp"

#ifndef NOSIMD
namespace Vector2Math {
    // Helper: horizontal dot product of 2D vector (x*x + y*y style)
    // Multiplies a * b elementwise, then sums x and y lanes into scalar.
    inline float Dot2(const __m128 a, const __m128 b) {
        const __m128 mul = _mm_mul_ps(a, b); // [ax*bx, ay*by, ?, ?]
        const __m128 shuf = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(1, 1, 1, 1)); // [ay*by, ...]
        const __m128 sum = _mm_add_ss(mul, shuf); // [ax*bx + ay*by, ...]
        return _mm_cvtss_f32(sum);
    }

    inline float Dot(const Vector2 &a, const Vector2 &b) {
        return Dot2(a.reg, b.reg);
    }

        inline float LengthSquared(const Vector2 &v) {
        return Dot2(v.reg, v.reg);
    }

    inline float Length(const Vector2 &v) {
        const __m128 lenSqReg = _mm_set_ss(Dot2(v.reg, v.reg));
        const __m128 lenReg = _mm_sqrt_ss(lenSqReg);
        return _mm_cvtss_f32(lenReg);
    }

    inline Vector2 Normalized(const Vector2 &v) {
        const float lenSq = Dot2(v.reg, v.reg);
        const __m128 lenReg = _mm_sqrt_ss(_mm_set_ss(lenSq));

        if (_mm_cvtss_f32(lenReg) == 0.0f) return Vector2();

        const __m128 ones = _mm_set1_ps(1.0f);
        __m128 invLen = _mm_div_ss(ones, lenReg);
        invLen = TILKY_MM_SHUFFLE_PS(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_mul_ps(v.reg, invLen)};
    }

    inline void Normalize(Vector2 &v) { v = Normalized(v); }

    inline float Cross(const Vector2 &a, const Vector2 &b) {
        const __m128 switchedB = TILKY_MM_SHUFFLE_PS(b.reg, b.reg, _MM_SHUFFLE(3, 2, 0, 1));

        __m128 multiplied = _mm_mul_ps(a.reg, switchedB);
        const __m128 shifted = TILKY_MM_SHUFFLE_PS(multiplied, multiplied, _MM_SHUFFLE(3, 2, 1, 1));
        const __m128 result = _mm_sub_ss(multiplied, shifted);

        return _mm_cvtss_f32(result);
    }

    inline float DistanceSquared(const Vector2 &a, const Vector2 &b) {
        const __m128 delta = _mm_sub_ps(a.reg, b.reg);
        return Dot2(delta, delta);
    }

    inline float Distance(const Vector2 &a, const Vector2 &b) {
        const __m128 delta = _mm_sub_ps(a.reg, b.reg);
        const float distSq = Dot2(delta, delta);
        const __m128 distReg = _mm_sqrt_ss(_mm_set_ss(distSq));
        return _mm_cvtss_f32(distReg);
    }

    // Moves current toward target by at most maxDistance, never overshooting.
    inline Vector2 MoveTowards(const Vector2 &current, const Vector2 &target, const float maxDistance) {
        const __m128 delta = _mm_sub_ps(target.reg, current.reg);
        const float distSq = Dot2(delta, delta);

        if (distSq == 0.0f || (maxDistance >= 0.0f && distSq <= maxDistance * maxDistance)) return target;

        const __m128 distReg = _mm_sqrt_ss(_mm_set_ss(distSq));
        __m128 step = _mm_div_ss(_mm_set_ss(maxDistance), distReg);
        step = TILKY_MM_SHUFFLE_PS(step, step, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_add_ps(current.reg, _mm_mul_ps(delta, step))};
    }

    // Projects v onto the line along onto; zero if onto is zero.
    inline Vector2 Project(const Vector2 &v, const Vector2 &onto) {
        const float lenSq = Dot2(onto.reg, onto.reg);

        if (lenSq == 0.0f) return Vector2();

        __m128 scale = _mm_div_ss(_mm_set_ss(Dot2(v.reg, onto.reg)), _mm_set_ss(lenSq));
        scale = TILKY_MM_SHUFFLE_PS(scale, scale, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_mul_ps(onto.reg, scale)};
    }

    // Shortens v to maxLength if it is longer; shorter vectors are returned unchanged.
    inline Vector2 ClampLength(const Vector2 &v, const float maxLength) {
        const float lenSq = Dot2(v.reg, v.reg);

        if (lenSq == 0.0f || (maxLength >= 0.0f && lenSq <= maxLength * maxLength)) return v;

        const __m128 lenReg = _mm_sqrt_ss(_mm_set_ss(lenSq));
        __m128 scale = _mm_div_ss(_mm_set_ss(maxLength), lenReg);
        scale = TILKY_MM_SHUFFLE_PS(scale, scale, _MM_SHUFFLE(0, 0, 0, 0));

        return {_mm_mul_ps(v.reg, scale)};
    }
}
#else
namespace Vector2Math {
    inline float Dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    inline float LengthSquared(const Vector2& v) {
        return v.x * v.x + v.y * v.y;
    }

    inline float Length(const Vector2& v) {
        return std::sqrt(LengthSquared(v));
    }

    inline Vector2 Normalized(const Vector2& v) {
        const float lenSq = LengthSquared(v);

        if (lenSq == 0.0f) return Vector2();

        const float invLen = 1.0f / std::sqrt(lenSq);
        return {v.x * invLen, v.y * invLen};
    }

    inline void Normalize(Vector2& v) {
        v = Normalized(v);
    }

    inline float Cross(const Vector2& a, const Vector2& b) {
        return a.x * b.y - a.y * b.x;
    }

    inline float DistanceSquared(const Vector2& a, const Vector2& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    inline float Distance(const Vector2& a, const Vector2& b) {
        return std::sqrt(DistanceSquared(a, b));
    }

    // Moves current toward target by at most maxDistance, never overshooting.
    inline Vector2 MoveTowards(const Vector2& current, const Vector2& target, const float maxDistance) {
        const float dx = target.x - current.x;
        const float dy = target.y - current.y;
        const float distSq = dx * dx + dy * dy;

        if (distSq == 0.0f || (maxDistance >= 0.0f && distSq <= maxDistance * maxDistance)) return target;

        const float step = maxDistance / std::sqrt(distSq);
        return {current.x + dx * step, current.y + dy * step};
    }

    // Projects v onto the line along onto; zero if onto is zero.
    inline Vector2 Project(const Vector2& v, const Vector2& onto) {
        const float lenSq = LengthSquared(onto);

        if (lenSq == 0.0f) return Vector2();

        const float scale = Dot(v, onto) / lenSq;
        return {onto.x * scale, onto.y * scale};
    }

    // Shortens v to maxLength if it is longer; shorter vectors are returned unchanged.
    inline Vector2 ClampLength(const Vector2& v, const float maxLength) {
        const float lenSq = LengthSquared(v);

        if (lenSq == 0.0f || (maxLength >= 0.0f && lenSq <= maxLength * maxLength)) return v;

        const float scale = maxLength / std::sqrt(lenSq);
        return {v.x * scale, v.y * scale};
    }
}
#endif

#endif //TILKY_ENGINE_VECTOR2MATH_HPP
