#pragma once

#include "../SIMD/SSECompat.hpp"
#include "../Constants.hpp"

#ifndef NOSIMD
struct alignas(16) Vector2 {
    union {
        __m128 reg;
        struct { float x, y, z, w; };
    };

    // --- Constructors ---
    Vector2(const float x = 0.0f, const float y = 0.0f)
        : x(x), y(y), z(0.0f), w(0.0f) {}

    Vector2(__m128 primitiveReg) : reg(primitiveReg) {}

    // --- Basic Arithmetic Operators ---
    Vector2 operator+(const Vector2& other) const { return {_mm_add_ps(reg, other.reg)}; }
    Vector2 operator-(const Vector2& other) const { return {_mm_sub_ps(reg, other.reg)}; }
    Vector2 operator*(const Vector2& other) const { return {_mm_mul_ps(reg, other.reg)}; }
    Vector2 operator/(const Vector2& other) const { return {_mm_div_ps(reg, other.reg)}; }

    Vector2 operator*(const float value) const { return {_mm_mul_ps(reg, _mm_set1_ps(value))}; }
    Vector2 operator/(const float value) const { return {_mm_div_ps(reg, _mm_set1_ps(value))}; }

    Vector2 operator-() const { return {_mm_xor_ps(reg, _mm_set1_ps(-0.0f))}; }

    // --- Assignment Operators ---
    Vector2& operator+=(const Vector2& other) {
        this->reg = _mm_add_ps(this->reg, other.reg);
        return *this;
    }

    Vector2& operator-=(const Vector2& other) {
        this->reg = _mm_sub_ps(this->reg, other.reg);
        return *this;
    }

    Vector2& operator*=(const float value) {
        this->reg = _mm_mul_ps(this->reg, _mm_set1_ps(value));
        return *this;
    }

    Vector2& operator/=(const float value) {
        this->reg = _mm_div_ps(this->reg, _mm_set1_ps(value));
        return *this;
    }

    // --- Comparison Operators ---
    bool operator==(const Vector2& other) const {
        const __m128 cmpMask = _mm_cmpeq_ps(reg, other.reg);
        const int mask = _mm_movemask_ps(cmpMask);

        return (mask & 0b0011) == 0b0011;
    }

    bool operator!=(const Vector2& other) const {return !(*this == other);}

    // --- Utility Functions ---
    bool IsZero() const {
        const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
        const __m128 absVec = _mm_and_ps(reg, absMask);
        const __m128 epsilon = _mm_set1_ps(Constants::Epsilon);

        const __m128 cmpMask = _mm_cmplt_ps(absVec, epsilon);
        const int mask = _mm_movemask_ps(cmpMask);

        return (mask & 0b0011) == 0b0011;
    }
};
#else
struct Vector2 {
    float x, y;

     // --- Constructors ---
    Vector2(const float x = 0.0f, const float y = 0.0f) : x(x), y(y) {}

    // --- Basic Arithmetic Operators ---
    Vector2 operator+(const Vector2 &other) const { return (Vector2){x + other.x, y + other.y}; }
    Vector2 operator-(const Vector2 &other) const { return (Vector2){x - other.x, y - other.y}; }
    Vector2 operator*(const Vector2 &other) const { return (Vector2){x * other.x, y * other.y}; }
    Vector2 operator/(const Vector2 &other) const { return (Vector2){x / other.x, y / other.y}; }

    Vector2 operator*(const float value) const { return Vector2{x * value, y * value}; }
    Vector2 operator/(const float value) const { return Vector2{x / value, y / value}; }

    Vector2 operator-() const { return Vector2{-x, -y}; }

    // --- Assignment Operators ---
    Vector2& operator+=(const Vector2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector2& operator*=(const float value) {
        x *= value;
        y *= value;
        return *this;
    }

    Vector2& operator/=(const float value) {
        x /= value;
        y /= value;
        return *this;
    }

    // --- Comparison Operators ---
    bool operator==(const Vector2& other) const {
       return x == other.x && y == other.y;
    }

    bool operator!=(const Vector2& other) const {return !(*this == other);}

    // --- Utility Functions ---
    bool IsZero() const {
        return x < Constants::Epsilon && y < Constants::Epsilon;
    }
};

#endif