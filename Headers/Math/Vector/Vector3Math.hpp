//
// Created by berke on 5/29/2026.
//

#ifndef TILKY_ENGINE_VECTOR3MATH_HPP
#define TILKY_ENGINE_VECTOR3MATH_HPP

#include "Vector3.hpp"

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
        const float len = Length(v);
        if (len == 0.0f) return {};
        return {v.x / len, v.y / len, v.z / len};
    }

    inline void Normalize(Vector3& v) {
        const float len = Length(v);
        if (len == 0.0f) return;
        v.x /= len; v.y /= len; v.z /= len;
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

#endif //TILKY_ENGINE_VECTOR3MATH_HPP