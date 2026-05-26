#pragma once

#include <cmath>

#include "Headers/Math/Vector/Vector3.hpp"

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
    inline float Length(const Vector3& v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    inline float LengthSquared(const Vector3& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    inline Vector3 Normalized(const Vector3& v) {
        const float len = Length(v);
        if (len == 0.0f) return {};
        return {v.x / len, v.y / len, v.z / len};
    }

    inline void Normalize(Vector3& v) {
        const float len = Length(v);
        if (len == 0.0f) return;
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }

    inline float Distance(const Vector3& a, const Vector3& b) {
        const float lineA = a.x - b.x;
        const float lineB = a.y - b.y;
        const float lineD = a.z - b.z;
        const float result = lineA * lineA + lineB * lineB + lineD * lineD;
        return std::sqrt(result);
    }

    inline float DistanceSquared(const Vector3& a, const Vector3& b) {
        const float lineA = a.x - b.x;
        const float lineB = a.y - b.y;
        const float lineD = a.z - b.z;
        const float result = lineA * lineA + lineB * lineB + lineD * lineD;
        return result;
    }
}