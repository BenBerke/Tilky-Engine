//
// Created by berke on 4/13/2026.
//
#ifndef TILKY_ENGINE_VECTOR4_H
#define TILKY_ENGINE_VECTOR4_H

#include "../SIMD/SSECompat.hpp"
#include "../Constants.hpp"

#ifndef NOSIMD
struct alignas(16) Vector4 {
    union {
        __m128 reg;
        struct { float x, y, z, w; };
    };

    // --- Constructors ---
    Vector4(const float x = 0.0f, const float y = 0.0f, const float z = 0.0f, const float w = 0.0f)
        : x(x), y(y), z(z), w(w) {}

    Vector4(__m128 primitiveReg) : reg(primitiveReg) {}

    Vector4& NormalizeColors() {
        const __m128 minBounds = _mm_set1_ps(0.0f);
        const __m128 maxBounds = _mm_set1_ps(255.0f);
        const __m128 divScale  = _mm_set1_ps(255.0f);

        __m128 clamped = _mm_max_ps(this->reg, minBounds);
        clamped = _mm_min_ps(clamped, maxBounds);

        this->reg = _mm_div_ps(clamped, divScale);

        return *this;
    }

    // --- Basic Arithmetic Operators ---
    Vector4 operator+(const Vector4& other) const { return {_mm_add_ps(reg, other.reg)}; }
    Vector4 operator-(const Vector4& other) const { return {_mm_sub_ps(reg, other.reg)}; }
    Vector4 operator*(const Vector4& other) const { return {_mm_mul_ps(reg, other.reg)}; }
    Vector4 operator/(const Vector4& other) const { return {_mm_div_ps(reg, other.reg)}; }

    Vector4 operator*(const float value) const { return {_mm_mul_ps(reg, _mm_set1_ps(value))}; }
    Vector4 operator/(const float value) const { return {_mm_div_ps(reg, _mm_set1_ps(value))}; }
    Vector4 operator-() const { return {_mm_xor_ps(reg, _mm_set1_ps(-0.0f))}; }

    // --- Assignment Operators (Vector4 overloads) ---
    Vector4& operator+=(const Vector4& other) { this->reg = _mm_add_ps(this->reg, other.reg); return *this; }
    Vector4& operator-=(const Vector4& other) { this->reg = _mm_sub_ps(this->reg, other.reg); return *this; }
    Vector4& operator*=(const Vector4& other) { this->reg = _mm_mul_ps(this->reg, other.reg); return *this; }
    Vector4& operator/=(const Vector4& other) { this->reg = _mm_div_ps(this->reg, other.reg); return *this; }

    // --- Assignment Operators (float overloads) ---
    Vector4& operator+=(const float value) { this->reg = _mm_add_ps(this->reg, _mm_set1_ps(value)); return *this; }
    Vector4& operator-=(const float value) { this->reg = _mm_sub_ps(this->reg, _mm_set1_ps(value)); return *this; }
    Vector4& operator*=(const float value) { this->reg = _mm_mul_ps(this->reg, _mm_set1_ps(value)); return *this; }
    Vector4& operator/=(const float value) { this->reg = _mm_div_ps(this->reg, _mm_set1_ps(value)); return *this; }

    // --- Comparison Operators ---
    bool operator==(const Vector4& other) const {
        const __m128 cmpMask = _mm_cmpeq_ps(reg, other.reg);
        const int mask = _mm_movemask_ps(cmpMask);

        return (mask & 0b1111) == 0b1111;
    }

    bool operator!=(const Vector4& other) const { return !(*this == other); }

    // --- Utility Functions ---
    bool IsZero() const {
        const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
        const __m128 absVec = _mm_and_ps(reg, absMask);
        const __m128 epsilon = _mm_set1_ps(Constants::Epsilon);

        const __m128 cmpMask = _mm_cmplt_ps(absVec, epsilon);
        const int mask = _mm_movemask_ps(cmpMask);

        return (mask & 0b1111) == 0b1111;
    }
};
#else

struct Vector4 {
    float x, y, z, w;

    // --- Constructors ---
    Vector4(const float x = 0.0f, const float y = 0.0f, const float z = 0.0f, const float w = 0.0f)
        : x(x), y(y), z(z), w(w) {}

    Vector4& NormalizeColors() {
        x /= 255.0f; y /= 255.0f; z /= 255.0f;
        return *this;
    }

    // --- Basic Arithmetic Operators ---
    Vector4 operator+(const Vector4& other) const { return (Vector4){x + other.x, y + other.y, z + other.z, w + other.w}; }
    Vector4 operator-(const Vector4& other) const { return (Vector4){x - other.x, y - other.y, z - other.z, w - other.w}; }
    Vector4 operator*(const Vector4& other) const { return (Vector4){x * other.x, y * other.y, z * other.z, w * other.w}; }
    Vector4 operator/(const Vector4& other) const { return (Vector4){x / other.x, y / other.y, z / other.z, w / other.w}; }

    Vector4 operator*(const float value) const { return (Vector4){x * value, y * value, z * value, w * value}; }
    Vector4 operator/(const float value) const { return (Vector4){x / value, y / value, z / value, w / value}; }
    Vector4 operator-() const { return (Vector4){-x, -y, -z, -w}; }

    // --- Assignment Operators (Vector4 overloads) ---
    Vector4& operator+=(const Vector4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    Vector4& operator-=(const Vector4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
    Vector4& operator*=(const Vector4& other) { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
    Vector4& operator/=(const Vector4& other) { x /= other.x; y /= other.y; z /= other.z; w /= other.w; return *this; }

    // --- Assignment Operators (float overloads) ---
    Vector4& operator+=(const float value) { x += value; y += value; z += value; w += value; return *this; }
    Vector4& operator-=(const float value) { x -= value; y -= value; z -= value; w -= value; return *this; }
    Vector4& operator*=(const float value) { x *= value; y *= value; z *= value; w *= value; return *this; }
    Vector4& operator/=(const float value) { x /= value; y /= value; z /= value; w /= value; return *this; }

    // --- Comparison Operators ---
    bool operator==(const Vector4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    bool operator!=(const Vector4& other) const { return !(*this == other); }

    // --- Utility Functions ---
    bool IsZero() const {
        return x < Constants::Epsilon && y < Constants::Epsilon && z < Constants::Epsilon && w < Constants::Epsilon;
    }
};

#endif

#endif //TILKY_ENGINE_VECTOR4_H