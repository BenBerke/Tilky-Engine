//
// Created by berke on 8/1/2026.
//

#ifndef TILKY_ENGINE_QUATERNION_H
#define TILKY_ENGINE_QUATERNION_H

#include "../SIMD/SSECompat.hpp"
#include "../Constants.hpp"

#include <cmath>

#ifndef NOSIMD

struct alignas(16) Quaternion {
    union {
        __m128 reg;
        struct { float x, y, z, w; };
    };

    Quaternion(const float x = 0.0f, const float y = 0.0f, const float z = 0.0f, const float w = 1.0f)
        : x(x), y(y), z(z), w(w) {}

    explicit Quaternion(const __m128 primitiveReg) : reg(primitiveReg) {}

    [[nodiscard]] static Quaternion Identity() {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    [[nodiscard]] static Quaternion FromAxisAngle(float axisX, float axisY, float axisZ, const float angleRadians) {
        const float axisLengthSquared = axisX * axisX + axisY * axisY + axisZ * axisZ;

        if (axisLengthSquared <= Constants::Epsilon * Constants::Epsilon) return Identity();

        const float inverseAxisLength = 1.0f / std::sqrt(axisLengthSquared);

        axisX *= inverseAxisLength;
        axisY *= inverseAxisLength;
        axisZ *= inverseAxisLength;

        const float halfAngle = angleRadians * 0.5f;
        const float sinHalfAngle = std::sin(halfAngle);

        return {axisX * sinHalfAngle, axisY * sinHalfAngle, axisZ * sinHalfAngle, std::cos(halfAngle)};
    }

    [[nodiscard]] static Quaternion FromEulerRadians(const float xRadians, const float yRadians, const float zRadians) {
        const float halfX = xRadians * 0.5f;
        const float halfY = yRadians * 0.5f;
        const float halfZ = zRadians * 0.5f;

        const float sinX = std::sin(halfX);
        const float cosX = std::cos(halfX);
        const float sinY = std::sin(halfY);
        const float cosY = std::cos(halfY);
        const float sinZ = std::sin(halfZ);
        const float cosZ = std::cos(halfZ);

        return Quaternion{
            sinX * cosY * cosZ - cosX * sinY * sinZ,
            cosX * sinY * cosZ + sinX * cosY * sinZ,
            cosX * cosY * sinZ - sinX * sinY * cosZ,
            cosX * cosY * cosZ + sinX * sinY * sinZ
        }.Normalized();
    }

    [[nodiscard]] static Quaternion FromEulerDegrees(const float xDegrees, const float yDegrees, const float zDegrees) {
        return FromEulerRadians(xDegrees * Constants::DegToRad, yDegrees * Constants::DegToRad, zDegrees * Constants::DegToRad);
    }

    [[nodiscard]] Vector3 ToEulerRadians() const {
        const Quaternion q = Normalized();

        Vector3 result{};

        const float sinXCosY = 2.0f * (q.w * q.x + q.y * q.z);
        const float cosXCosY = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

        result.x = std::atan2(sinXCosY, cosXCosY);

        const float sinY = 2.0f * (q.w * q.y - q.z * q.x);

        if (std::abs(sinY) >= 1.0f) result.y = std::copysign(1.57079632679489661923f, sinY);
        else result.y = std::asin(sinY);

        const float sinZCosY = 2.0f * (q.w * q.z + q.x * q.y);
        const float cosZCosY = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);

        result.z = std::atan2(sinZCosY, cosZCosY);

        return result;
    }

    [[nodiscard]] Vector3 ToEulerDegrees() const {
        const Vector3 radians = ToEulerRadians();

        return {
            radians.x * Constants::RadToDeg,
            radians.y * Constants::RadToDeg,
            radians.z * Constants::RadToDeg
        };
    }

    [[nodiscard]] Quaternion operator*(const Quaternion& other) const {
        const __m128 wTerm = _mm_mul_ps(_mm_set1_ps(w), other.reg);
        const __m128 xTerm = _mm_mul_ps(_mm_set1_ps(x), _mm_set_ps(-other.x, other.y, -other.z, other.w));
        const __m128 yTerm = _mm_mul_ps(_mm_set1_ps(y), _mm_set_ps(-other.y, -other.x, other.w, other.z));
        const __m128 zTerm = _mm_mul_ps(_mm_set1_ps(z), _mm_set_ps(-other.z, other.w, other.x, -other.y));

        return Quaternion(_mm_add_ps(_mm_add_ps(wTerm, xTerm), _mm_add_ps(yTerm, zTerm)));
    }

    [[nodiscard]] Quaternion operator/(const Quaternion& other) const {
        return *this * other.Inversed();
    }

    [[nodiscard]] Quaternion operator+(const Quaternion& other) const {
        return Quaternion(_mm_add_ps(reg, other.reg));
    }

    [[nodiscard]] Quaternion operator-(const Quaternion& other) const {
        return Quaternion(_mm_sub_ps(reg, other.reg));
    }

    [[nodiscard]] Quaternion operator*(const float value) const {
        return Quaternion(_mm_mul_ps(reg, _mm_set1_ps(value)));
    }

    [[nodiscard]] Quaternion operator/(const float value) const {
        return Quaternion(_mm_div_ps(reg, _mm_set1_ps(value)));
    }

    [[nodiscard]] Quaternion operator-() const {
        return Quaternion(_mm_xor_ps(reg, _mm_set1_ps(-0.0f)));
    }

    Quaternion& operator*=(const Quaternion& other) {
        *this = *this * other;
        return *this;
    }

    Quaternion& operator/=(const Quaternion& other) {
        *this = *this / other;
        return *this;
    }

    Quaternion& operator+=(const Quaternion& other) {
        reg = _mm_add_ps(reg, other.reg);
        return *this;
    }

    Quaternion& operator-=(const Quaternion& other) {
        reg = _mm_sub_ps(reg, other.reg);
        return *this;
    }

    Quaternion& operator*=(const float value) {
        reg = _mm_mul_ps(reg, _mm_set1_ps(value));
        return *this;
    }

    Quaternion& operator/=(const float value) {
        reg = _mm_div_ps(reg, _mm_set1_ps(value));
        return *this;
    }

    [[nodiscard]] bool operator==(const Quaternion& other) const {
        const __m128 comparison = _mm_cmpeq_ps(reg, other.reg);
        return (_mm_movemask_ps(comparison) & 0b1111) == 0b1111;
    }

    [[nodiscard]] bool operator!=(const Quaternion& other) const {
        return !(*this == other);
    }

    [[nodiscard]] float LengthSquared() const {
        const __m128 squared = _mm_mul_ps(reg, reg);
        __m128 sum = _mm_add_ps(squared, _mm_movehl_ps(squared, squared));
        const __m128 secondLane = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(1, 1, 1, 1));

        sum = _mm_add_ss(sum, secondLane);

        return _mm_cvtss_f32(sum);
    }

    [[nodiscard]] float Length() const {
        return std::sqrt(LengthSquared());
    }

    Quaternion& Normalize() {
        const float lengthSquared = LengthSquared();

        if (lengthSquared <= Constants::Epsilon * Constants::Epsilon) {
            *this = Identity();
            return *this;
        }

        reg = _mm_mul_ps(reg, _mm_set1_ps(1.0f / std::sqrt(lengthSquared)));

        return *this;
    }

    [[nodiscard]] Quaternion Normalized() const {
        Quaternion result = *this;
        result.Normalize();
        return result;
    }

    [[nodiscard]] Quaternion Conjugated() const {
        const __m128 signMask = _mm_set_ps(0.0f, -0.0f, -0.0f, -0.0f);
        return Quaternion(_mm_xor_ps(reg, signMask));
    }

    [[nodiscard]] Quaternion Inversed() const {
        const float lengthSquared = LengthSquared();

        if (lengthSquared <= Constants::Epsilon * Constants::Epsilon) return Identity();

        return Conjugated() / lengthSquared;
    }

    [[nodiscard]] bool IsNormalized() const {
        return std::abs(LengthSquared() - 1.0f) < Constants::Epsilon;
    }

    [[nodiscard]] bool IsZero() const {
        const __m128 absoluteMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
        const __m128 absoluteValues = _mm_and_ps(reg, absoluteMask);
        const __m128 comparison = _mm_cmplt_ps(absoluteValues, _mm_set1_ps(Constants::Epsilon));

        return (_mm_movemask_ps(comparison) & 0b1111) == 0b1111;
    }
};

#else

struct Quaternion {
    float x, y, z, w;

    // Identity rotation by default.
    Quaternion(
        const float x = 0.0f,
        const float y = 0.0f,
        const float z = 0.0f,
        const float w = 1.0f
    ) : x(x), y(y), z(z), w(w) {}

    [[nodiscard]] static Quaternion Identity() {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    [[nodiscard]] static Quaternion FromAxisAngle(float axisX, float axisY, float axisZ, const float angleRadians) {
        const float axisLengthSquared = axisX * axisX + axisY * axisY + axisZ * axisZ;

        if (axisLengthSquared <= Constants::Epsilon * Constants::Epsilon) return Identity();

        const float inverseAxisLength = 1.0f / std::sqrt(axisLengthSquared);

        axisX *= inverseAxisLength;
        axisY *= inverseAxisLength;
        axisZ *= inverseAxisLength;

        const float halfAngle = angleRadians * 0.5f;
        const float sinHalfAngle = std::sin(halfAngle);

        return {axisX * sinHalfAngle, axisY * sinHalfAngle, axisZ * sinHalfAngle, std::cos(halfAngle)};
    }

    // Creates q = Z * Y * X.
    // The X rotation is applied first, followed by Y, then Z.
    [[nodiscard]] static Quaternion FromEulerRadians(const float xRadians, const float yRadians, const float zRadians) {
        const float halfX = xRadians * 0.5f;
        const float halfY = yRadians * 0.5f;
        const float halfZ = zRadians * 0.5f;

        const float sinX = std::sin(halfX);
        const float cosX = std::cos(halfX);

        const float sinY = std::sin(halfY);
        const float cosY = std::cos(halfY);

        const float sinZ = std::sin(halfZ);
        const float cosZ = std::cos(halfZ);

        return Quaternion{
            sinX * cosY * cosZ - cosX * sinY * sinZ,
            cosX * sinY * cosZ + sinX * cosY * sinZ,
            cosX * cosY * sinZ - sinX * sinY * cosZ,
            cosX * cosY * cosZ + sinX * sinY * sinZ
        }.Normalized();
    }

    [[nodiscard]] static Quaternion FromEulerDegrees(const float xDegrees, const float yDegrees,const float zDegrees) {
        return FromEulerRadians(
            xDegrees * Constants::DegToRad,
            yDegrees * Constants::DegToRad,
            zDegrees * Constants::DegToRad
        );
    }

    [[nodiscard]] Vector3 ToEulerRadians() const {
        const Quaternion q = Normalized();

        Vector3 result{};

        // X rotation
        const float sinXCosY = 2.0f * (q.w * q.x + q.y * q.z);

        const float cosXCosY = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

        result.x = std::atan2(sinXCosY, cosXCosY);

        // Y rotation
        const float sinY = 2.0f * (q.w * q.y - q.z * q.x);

        if (std::abs(sinY) >= 1.0f) result.y = std::copysign(1.57079632679489661923f,sinY);
        else result.y = std::asin(sinY);

        // Z rotation
        const float sinZCosY = 2.0f * (q.w * q.z + q.x * q.y);

        const float cosZCosY = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);

        result.z = std::atan2(sinZCosY, cosZCosY);

        return result;
    }

    [[nodiscard]] Vector3 ToEulerDegrees() const {
        const Vector3 radians = ToEulerRadians();

        return {
            radians.x * Constants::RadToDeg,
            radians.y * Constants::RadToDeg,
            radians.z * Constants::RadToDeg
        };
    }

    // Hamilton product. This composes rotations.
    [[nodiscard]] Quaternion operator*(const Quaternion& other) const {
        return {
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y - x * other.z + y * other.w + z * other.x,
            w * other.z + x * other.y - y * other.x + z * other.w,
            w * other.w - x * other.x - y * other.y - z * other.z
        };
    }

    [[nodiscard]] Quaternion operator/(const Quaternion& other) const {
        return *this * other.Inversed();
    }

    [[nodiscard]] Quaternion operator+(const Quaternion& other) const {
        return {x + other.x, y + other.y, z + other.z, w + other.w};
    }

    [[nodiscard]] Quaternion operator-(const Quaternion& other) const {
        return {x - other.x, y - other.y, z - other.z, w - other.w};
    }

    [[nodiscard]] Quaternion operator*(const float value) const {
        return {x * value, y * value, z * value, w * value};
    }

    [[nodiscard]] Quaternion operator/(const float value) const {
        return {x / value, y / value, z / value, w / value};
    }

    [[nodiscard]] Quaternion operator-() const {
        return {-x, -y, -z, -w};
    }

    Quaternion& operator*=(const Quaternion& other) {
        *this = *this * other;
        return *this;
    }

    Quaternion& operator/=(const Quaternion& other) {
        *this = *this / other;
        return *this;
    }

    Quaternion& operator+=(const Quaternion& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }

    Quaternion& operator-=(const Quaternion& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }

    Quaternion& operator*=(const float value) {
        x *= value;
        y *= value;
        z *= value;
        w *= value;
        return *this;
    }

    Quaternion& operator/=(const float value) {
        x /= value;
        y /= value;
        z /= value;
        w /= value;
        return *this;
    }

    [[nodiscard]] bool operator==(const Quaternion& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    [[nodiscard]] bool operator!=(const Quaternion& other) const {
        return !(*this == other);
    }

    [[nodiscard]] float LengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    [[nodiscard]] float Length() const {
        return std::sqrt(LengthSquared());
    }

    Quaternion& Normalize() {
        const float lengthSquared = LengthSquared();

        if (lengthSquared <= Constants::Epsilon * Constants::Epsilon) {
            *this = Identity();
            return *this;
        }

        const float inverseLength = 1.0f / std::sqrt(lengthSquared);

        x *= inverseLength;
        y *= inverseLength;
        z *= inverseLength;
        w *= inverseLength;

        return *this;
    }

    [[nodiscard]] Quaternion Normalized() const {
        Quaternion result = *this;
        result.Normalize();
        return result;
    }

    [[nodiscard]] Quaternion Conjugated() const {
        return {-x, -y, -z, w};
    }

    [[nodiscard]] Quaternion Inversed() const {
        const float lengthSquared = LengthSquared();

        if (lengthSquared <= Constants::Epsilon * Constants::Epsilon) return Identity();

        return Conjugated() / lengthSquared;
    }

    [[nodiscard]] bool IsNormalized() const {
        return std::abs(LengthSquared() - 1.0f) < Constants::Epsilon;
    }

    [[nodiscard]] bool IsZero() const {
        return
            std::abs(x) < Constants::Epsilon &&
            std::abs(y) < Constants::Epsilon &&
            std::abs(z) < Constants::Epsilon &&
            std::abs(w) < Constants::Epsilon;
    }
};

#endif

[[nodiscard]] inline Quaternion operator*(const float value,const Quaternion& quaternion) {
    return quaternion * value;
}

#endif // TILKY_ENGINE_QUATERNION_H