module;

#include <cmath>

export module Vector2;

export struct Vector2 {
    float x;
    float y;

    constexpr Vector2(float x = 0.0f, float y = 0.0f) : x(x), y(y) {}

    // --- Basic Arithmetic Operators ---
    constexpr Vector2 operator+(const Vector2& other) const { return {x + other.x, y + other.y}; }
    constexpr Vector2 operator-(const Vector2& other) const { return {x - other.x, y - other.y}; }
    constexpr Vector2 operator*(const float value) const { return {x * value, y * value}; }
    constexpr Vector2 operator*(const Vector2& other) const { return {x * other.x, y * other.y}; }
    constexpr Vector2 operator/(const float value) const { return {x / value, y / value}; }

    constexpr Vector2 operator-() const { return {-x, -y}; }

    Vector2& operator+=(const Vector2& other) {
        x += other.x; y += other.y;
        return *this;
    }
    Vector2& operator-=(const Vector2& other) {
        x -= other.x; y -= other.y;
        return *this;
    }
    Vector2& operator*=(float value) {
        x *= value; y *= value;
        return *this;
    }
    Vector2& operator/=(float value) {
        x /= value; y /= value;
        return *this;
    }

    // --- Comparison Operators ---
    constexpr bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }
    constexpr bool operator!=(const Vector2& other) const { return !(*this == other); }
};

// --- Left-Hand Side Scalar Multiplication ---
export constexpr Vector2 operator*(float value, const Vector2& v) {
    return {v.x * value, v.y * value};
}

// --- Vector Math Namespace ---
export namespace Vector2Math {

    inline float LengthSquared(const Vector2& v) {
        return v.x * v.x + v.y * v.y;
    }

    inline float Length(const Vector2& v) {
        return std::sqrt(LengthSquared(v));
    }

    inline Vector2
    Normalized(const Vector2& v) {
        const float len = Length(v);
        if (len == 0.0f) return {};
        return {v.x / len, v.y / len};
    }

    inline void Normalize(Vector2& v) {
        const float len = Length(v);
        if (len == 0.0f) return;
        v.x /= len; v.y /= len;
    }

    inline float Dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    inline float Cross(const Vector2& a, const Vector2& b) {
        return a.x * b.y - a.y * b.x;
    }

    inline float DistanceSquared(const Vector2& a, const Vector2& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    inline float Distance(const Vector2& a, const Vector2& b) {
        return std::sqrt(DistanceSquared(a, b));
    }
}