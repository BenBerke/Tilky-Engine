#pragma once


#include "../SIMD/SSECompat.hpp"
#include "../Constants.hpp"

struct alignas(16) Vector3 {
    union {
        __m128 reg;
        struct {float x, y, z, w;};
    };

    Vector3(const float x = 0.0f, const float y = 0.0f, const float z = 0.0f) : x(x), y(y), z(z), w(0.0f) {}
    Vector3(__m128 primitiveReg) : reg(primitiveReg) {}

    Vector3 operator=(const Vector3& other) {
        this->reg = other.reg;
        return *this;
    }

    // --- Basic Arithmetic Operators ---
    Vector3 operator+(const Vector3& other) const {
        return {_mm_add_ps(reg, other.reg)};;
    }
    Vector3 operator-(const Vector3& other) const {
        return {_mm_sub_ps(reg, other.reg)};;
    }

    Vector3 operator-() const {
        const __m128 reg = _mm_load_ps(&x);
        const __m128 signMask = _mm_set1_ps(-0.0f);

        const __m128 regResult = _mm_xor_ps(reg, signMask);

        Vector3 result;
        _mm_store_ps(&result.x, regResult);

        return result;
    }

    Vector3& operator+=(const Vector3& other) {
        this->reg = _mm_add_ps(this->reg, other.reg);
        return *this;
    }
    Vector3& operator-=(const Vector3& other) {
        this->reg = _mm_sub_ps(this->reg, other.reg);
        return *this;
    }
    Vector3& operator*=(const Vector3& other) {
        this->reg = _mm_mul_ps(this->reg, other.reg);
        return *this;
    }
    Vector3& operator/=(const Vector3& other) {
        this->reg = _mm_div_ps(this->reg, other.reg);
        return *this;
    }

    // --- Scalar Operators ---
    Vector3 operator+(const float value) const {
        const __m128 regScalar = _mm_set1_ps(value);

        const __m128 regResult = _mm_add_ps(reg, regScalar);

        return {regResult};
    }

    Vector3 operator-(const float value) const {
        const __m128 regScalar = _mm_set1_ps(value);

        const __m128 regResult = _mm_sub_ps(reg, regScalar);

        return {regResult};
    }

    Vector3 operator*(const float value) const {
        const __m128 regScalar = _mm_set1_ps(value);

        const __m128 regResult = _mm_mul_ps(reg, regScalar);

        return {regResult};
    }

    Vector3 operator/(const float value) const {
        const __m128 regScalar = _mm_set1_ps(value);

        const __m128 regResult = _mm_div_ps(reg, regScalar);

        return {regResult};
    }

    // --- Comparison Operators ---
    bool operator==(const Vector3& other) const {
        const __m128 cmpMask = _mm_cmpeq_ps(reg, other.reg);

        const int mask = _mm_movemask_ps(cmpMask);

        return (mask & 0b0111) == 0b0111;
    }
    bool operator!=(const Vector3& other) const { return !(*this == other); }

    // --- Utility Functions ---
    bool IsZero() const {
        const float fMask = std::bit_cast<float>(0x7FFFFFFF);
        const __m128 absMask = _mm_set1_ps(fMask);

        const __m128 absVec = _mm_and_ps(reg, absMask);
        const __m128 epsilon = _mm_set1_ps(Constants::Epsilon);
        const __m128 cmpMask = _mm_cmplt_ps(absVec, epsilon);

        const int mask = _mm_movemask_ps(cmpMask);

        return (mask & 0b0111) == 0b0111;
    }
};