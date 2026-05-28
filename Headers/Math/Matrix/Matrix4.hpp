module;

#include <cmath>

export module Matrix4;

import Vector3;

export struct Matrix4 {
    float m[4][4]{};

    Matrix4() = default;

    Matrix4(std::initializer_list<std::initializer_list<float>> rows) {
        int r = 0;
        for (const auto& row : rows) {
            if (r >= 4) break;
            int c = 0;
            for (float val : row) {
                if (c >= 4) break;
                m[r][c++] = val;
            }
            ++r;
        }
    }

    void SetValue(int row, int col, float v) {
        m[row][col] = v;
    }

    static Matrix4 Identity() {
        return {{
            {1, 0, 0, 0},
            {0, 1, 0, 0},
            {0, 0, 1, 0},
            {0, 0, 0, 1}
        }};
    }

    Matrix4 operator+(const Matrix4& other) const {
        Matrix4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result.m[i][j] = m[i][j] + other.m[i][j];
        return result;
    }

    Matrix4 operator-(const Matrix4& other) const {
        Matrix4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result.m[i][j] = m[i][j] - other.m[i][j];
        return result;
    }

    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    result.m[i][j] += m[i][k] * other.m[k][j];
                }
            }
        }
        return result;
    }

    Matrix4& operator+=(const Matrix4& other) {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] += other.m[i][j];
        return *this;
    }

    Matrix4& operator-=(const Matrix4& other) {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] -= other.m[i][j];
        return *this;
    }

    Matrix4& operator*=(const Matrix4& other) {
        Matrix4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    result.m[i][j] += m[i][k] * other.m[k][j];
                }
            }
        }
        *this = result;
        return *this;
    }

    [[nodiscard]] const float* Data() const {
        return &m[0][0];
    }

    float* Data() {
        return &m[0][0];
    }

    static Matrix4 Orthographic(float left, float right,
                                float bottom, float top,
                                float nearPlane, float farPlane) {
        Matrix4 result = Identity();

        result.m[0][0] =  2.0f / (right - left);
        result.m[1][1] =  2.0f / (top - bottom);
        result.m[2][2] = -2.0f / (farPlane - nearPlane);

        result.m[0][3] = -(right + left) / (right - left);
        result.m[1][3] = -(top + bottom) / (top - bottom);
        result.m[2][3] = -(farPlane + nearPlane) / (farPlane - nearPlane);

        return result;
    }

    static Matrix4 Perspective(
        const float fovDegrees,
        const float aspect,
        const float nearPlane,
        const float farPlane
    ) {
        Matrix4 result{};

        const float fovRadians = fovDegrees * 3.14159265359f / 180.0f;
        const float f = 1.0f / std::tan(fovRadians * 0.5f);

        result.m[0][0] = f / aspect;
        result.m[1][1] = f;
        result.m[2][2] = (farPlane + nearPlane) / (nearPlane - farPlane);
        result.m[2][3] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
        result.m[3][2] = -1.0f;

        return result;
    }

    static Matrix4 PerspectiveReverseZ(
        const float fovDegrees,
        const float aspect,
        const float nearPlane,
        const float farPlane
    ) {
        Matrix4 result{};

        const float fovRadians = fovDegrees * 3.14159265359f / 180.0f;
        const float f = 1.0f / std::tan(fovRadians * 0.5f);

        result.m[0][0] = f / aspect;
        result.m[1][1] = f;

        // Reverse-Z, finite far plane, GL_ZERO_TO_ONE
        result.m[2][2] = nearPlane / (farPlane - nearPlane);
        result.m[2][3] = (farPlane * nearPlane) / (farPlane - nearPlane);
        result.m[3][2] = -1.0f;

        return result;
    }

    static Matrix4 LookAt(
        const Vector3& eye,
        const Vector3& target,
        const Vector3& up
    ) {
        const Vector3 forward = Vector3Math::Normalized(target - eye);
        const Vector3 right = Vector3Math::Normalized(Vector3Math::Cross(forward, up));
        const Vector3 cameraUp = Vector3Math::Cross(right, forward);

        Matrix4 result = Identity();

        result.m[0][0] = right.x;
        result.m[0][1] = right.y;
        result.m[0][2] = right.z;
        result.m[0][3] = -Vector3Math::Dot(right, eye);

        result.m[1][0] = cameraUp.x;
        result.m[1][1] = cameraUp.y;
        result.m[1][2] = cameraUp.z;
        result.m[1][3] = -Vector3Math::Dot(cameraUp, eye);

        result.m[2][0] = -forward.x;
        result.m[2][1] = -forward.y;
        result.m[2][2] = -forward.z;
        result.m[2][3] = Vector3Math::Dot(forward, eye);

        result.m[3][0] = 0.0f;
        result.m[3][1] = 0.0f;
        result.m[3][2] = 0.0f;
        result.m[3][3] = 1.0f;

        return result;
    }
};