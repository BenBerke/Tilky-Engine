struct Matrix4 {
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
};