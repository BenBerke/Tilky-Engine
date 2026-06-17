#pragma once

#include <cmath>
#include <initializer_list>
#include <optional>
#include <immintrin.h>

#include "../Vector/Vector3Math.hpp"

#include "../Constants.hpp"

struct alignas(16) Matrix4 {
    union {
        float m[4][4];          // Array access for standard lookups
        float data[16];         // Flat array access
        __m128 rows[4];         // 4 SIMD registers representing each row
    };

    Matrix4() {
        rows[0] = _mm_setzero_ps();
        rows[1] = _mm_setzero_ps();
        rows[2] = _mm_setzero_ps();
        rows[3] = _mm_setzero_ps();
    }

    Matrix4(const std::initializer_list<std::initializer_list<float>> initialRows) {
        int r = 0;
        for (const auto &row : initialRows) {
            if (r >= 4) break;
            int c = 0;
            float temp[4]{0, 0, 0, 0};
            for (float val : row) {
                if (c >= 4) break;
                temp[c++] = val;
            }
            rows[r] = _mm_loadu_ps(temp);
            ++r;
        }
    }

    // Explicit SIMD constructor passing raw registers
    Matrix4(const __m128 r0, const __m128 r1, const __m128 r2, const __m128 r3) {
        rows[0] = r0;
        rows[1] = r1;
        rows[2] = r2;
        rows[3] = r3;
    }

    void SetValue(const int row, const int col, const float v) {
        m[row][col] = v;
    }

    static Matrix4 Identity() {
        return Matrix4(
            _mm_setr_ps(1, 0, 0, 0),
            _mm_setr_ps(0, 1, 0, 0),
            _mm_setr_ps(0, 0, 1, 0),
            _mm_setr_ps(0, 0, 0, 1)
        );
    }

    // ============================================================
    // SIMD Component-wise Arithmetic
    // ============================================================

    Matrix4 operator+(const Matrix4 &other) const {
        return Matrix4(
            _mm_add_ps(rows[0], other.rows[0]),
            _mm_add_ps(rows[1], other.rows[1]),
            _mm_add_ps(rows[2], other.rows[2]),
            _mm_add_ps(rows[3], other.rows[3])
        );
    }

    Matrix4 operator-(const Matrix4 &other) const {
        return Matrix4(
            _mm_sub_ps(rows[0], other.rows[0]),
            _mm_sub_ps(rows[1], other.rows[1]),
            _mm_sub_ps(rows[2], other.rows[2]),
            _mm_sub_ps(rows[3], other.rows[3])
        );
    }

    Matrix4 &operator+=(const Matrix4 &other) {
        rows[0] = _mm_add_ps(rows[0], other.rows[0]);
        rows[1] = _mm_add_ps(rows[1], other.rows[1]);
        rows[2] = _mm_add_ps(rows[2], other.rows[2]);
        rows[3] = _mm_add_ps(rows[3], other.rows[3]);
        return *this;
    }

    Matrix4 &operator-=(const Matrix4 &other) {
        rows[0] = _mm_sub_ps(rows[0], other.rows[0]);
        rows[1] = _mm_sub_ps(rows[1], other.rows[1]);
        rows[2] = _mm_sub_ps(rows[2], other.rows[2]);
        rows[3] = _mm_sub_ps(rows[3], other.rows[3]);
        return *this;
    }

    // ============================================================
    // High-Performance SIMD Matrix Multiplication
    // ============================================================
    Matrix4 operator*(const Matrix4 &other) const {
        Matrix4 result;

        for (int i = 0; i < 4; ++i) {
            // 1. Broadcast element 0 of current row across an entire register
            __m128 e0 = _mm_shuffle_ps(rows[i], rows[i], _MM_SHUFFLE(0, 0, 0, 0));
            // 2. Multiply that element by the entire row 0 of the other matrix
            __m128 r = _mm_mul_ps(e0, other.rows[0]);

            // 3. Repeat for element 1 and row 1, accumulating into the register using FMA or standard adds
            __m128 e1 = _mm_shuffle_ps(rows[i], rows[i], _MM_SHUFFLE(1, 1, 1, 1));
            r = _mm_add_ps(r, _mm_mul_ps(e1, other.rows[1]));

            __m128 e2 = _mm_shuffle_ps(rows[i], rows[i], _MM_SHUFFLE(2, 2, 2, 2));
            r = _mm_add_ps(r, _mm_mul_ps(e2, other.rows[2]));

            __m128 e3 = _mm_shuffle_ps(rows[i], rows[i], _MM_SHUFFLE(3, 3, 3, 3));
            result.rows[i] = _mm_add_ps(r, _mm_mul_ps(e3, other.rows[3]));
        }

        return result;
    }

    Matrix4 &operator*=(const Matrix4 &other) {
        *this = *this * other;
        return *this;
    }

    [[nodiscard]] const float *Data() const { return &m[0][0]; }
    float *Data() { return &m[0][0]; }

    // ============================================================
    // Projection Matrices (Kept unchanged but loaded efficiently)
    // ============================================================
    static Matrix4 Orthographic(
        const float left, const float right, const float bottom, const float top, const float nearPlane, const float farPlane) {
        Matrix4 result = Identity();
        result.m[0][0] = 2.0f / (right - left);
        result.m[1][1] = 2.0f / (top - bottom);
        result.m[2][2] = -2.0f / (farPlane - nearPlane);
        result.m[0][3] = -(right + left) / (right - left);
        result.m[1][3] = -(top + bottom) / (top - bottom);
        result.m[2][3] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        return result;
    }

    static Matrix4 Perspective(const float fovDegrees, const float aspect, const float nearPlane, const float farPlane) {
        Matrix4 result;
        const float fovRadians = fovDegrees * Constants::DegToRad;
        const float f = 1.0f / std::tan(fovRadians * 0.5f);

        result.m[0][0] = f / aspect;
        result.m[1][1] = f;
        result.m[2][2] = (farPlane + nearPlane) / (nearPlane - farPlane);
        result.m[2][3] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
        result.m[3][2] = -1.0f;

        return result;
    }

    static Matrix4 PerspectiveReverseZ(const float fovDegrees, const float aspect, const float nearPlane) {
        Matrix4 result;
        const float fovRadians = fovDegrees * Constants::DegToRad;
        const float f = 1.0f / std::tan(fovRadians * 0.5f);

        result.m[0][0] = f / aspect;
        result.m[1][1] = f;
        result.m[2][3] = nearPlane;
        result.m[3][2] = -1.0f;

        return result;
    }

    static Matrix4 PerspectiveReverseZ(const float fovDegrees, const float aspect, const float nearPlane, const float farPlane) {
        Matrix4 result;
        const float fovRadians = fovDegrees * Constants::DegToRad;
        const float f = 1.0f / std::tan(fovRadians * 0.5f);

        result.m[0][0] = f / aspect;
        result.m[1][1] = f;
        result.m[2][2] = nearPlane / (farPlane - nearPlane);
        result.m[2][3] = (farPlane * nearPlane) / (farPlane - nearPlane);
        result.m[3][2] = -1.0f;

        return result;
    }

    // ============================================================
    // LookAt & Inversion (Using updated Vector3 and Matrix layouts)
    // ============================================================

    static Matrix4 LookAt(const Vector3 &eye, const Vector3 &target, const Vector3 &up) {
        const Vector3 forward = Vector3Math::Normalized(target - eye);
        const Vector3 right = Vector3Math::Normalized(Vector3Math::Cross(forward, up));
        const Vector3 cameraUp = Vector3Math::Cross(right, forward);

        Matrix4 result = Identity();

        result.m[0][0] = right.x;     result.m[0][1] = right.y;     result.m[0][2] = right.z;     result.m[0][3] = -Vector3Math::Dot(right, eye);
        result.m[1][0] = cameraUp.x;  result.m[1][1] = cameraUp.y;  result.m[1][2] = cameraUp.z;  result.m[1][3] = -Vector3Math::Dot(cameraUp, eye);
        result.m[2][0] = -forward.x;  result.m[2][1] = -forward.y;  result.m[2][2] = -forward.z;  result.m[2][3] = Vector3Math::Dot(forward, eye);

        return result;
    }

    static std::optional<Matrix4> Invert(const Matrix4& mat) {
        float augmented[4][8];
        const float* src = mat.Data();

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) {
                augmented[row][col] = src[row * 4 + col];
                augmented[row][col + 4] = (row == col) ? 1.0f : 0.0f;
            }
        for (int i = 0; i < 4; ++i) {
            int pivotRow = i;
            for (int row = i + 1; row < 4; ++row)
                if (std::abs(augmented[row][i]) > std::abs(augmented[pivotRow][i]))
                    pivotRow = row;

            if (std::abs(augmented[pivotRow][i]) < Constants::Epsilon) return std::nullopt;

            if (pivotRow != i) for (int col = 0; col < 8; ++col)
                std::swap(augmented[i][col], augmented[pivotRow][col]);

            const float pivotValue = augmented[i][i];
            for (int col = 0; col < 8; ++col)  augmented[i][col] /= pivotValue;

            for (int row = 0; row < 4; ++row) {
                if (row == i) continue;
                const float factor = augmented[row][i];
                for (int col = 0; col < 8; ++col) {
                    augmented[row][col] -= factor * augmented[i][col];
                }
            }
        }

        Matrix4 result;
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
            result.SetValue(row, col, augmented[row][col + 4]);

        return result;
    }
};