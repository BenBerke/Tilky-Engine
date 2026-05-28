module; // Global module fragment for legacy headers

#include <cmath>

export module Vector3;

// Export the struct so it's usable when importing this module
export struct Vector3 {
    float x, y, z;

    Vector3(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}

    // --- Basic Arithmetic Operators ---
    // FIXED BUG: Changed 'z - other.z' to 'z + other.z'
    Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z + other.z); }
    Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }

    Vector3 operator-() const { return Vector3(-x, -y, -z); } // Useful addition: unary negation

    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;
    }
    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;
    }
    Vector3& operator*=(const Vector3& other) {
        x *= other.x; y *= other.y; z *= other.z;
        return *this;
    }
    Vector3& operator/=(const Vector3& other) {
        x /= other.x; y /= other.y; z /= other.z;
        return *this;
    }

    // --- Scalar Operators ---
    template<typename T> Vector3 operator*(T value) const { return Vector3(x * value, y * value, z * value); }
    template<typename T> Vector3 operator/(T value) const { return Vector3(x / value, y / value, z / value); }
    template<typename T> Vector3 operator+(T value) const { return Vector3(x + value, y + value, z + value); }
    template<typename T> Vector3 operator-(T value) const { return Vector3(x - value, y - value, z - value); }

    // --- Comparison Operators ---
    bool operator==(const Vector3& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const Vector3& other) const { return !(*this == other); }

    // --- Utility Functions ---
    bool IsZero() const {
        auto closeToZero = [](float n) -> bool { return -0.0001f < n && n < 0.0001f; };
        return closeToZero(x) && closeToZero(y) && closeToZero(z);
    }
};

// --- Left-Hand Side Scalar Multiplication ---
// Allows you to write '2.0f * vector' instead of just 'vector * 2.0f'
export template<typename T>
Vector3 operator*(T value, const Vector3& v) {
    return Vector3(v.x * value, v.y * value, v.z * value);
}

// --- Vector Math Namespace ---
export namespace Vector3Math {

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