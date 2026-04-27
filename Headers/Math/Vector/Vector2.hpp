#ifndef WOLFY_ENGINE_VECTOR2_H
#define WOLFY_ENGINE_VECTOR2_H

struct Vector2 {
    float x;
    float y;

    constexpr Vector2(float x = 0.0f, float y = 0.0f) : x(x), y(y) {}

    constexpr Vector2 operator+(const Vector2& other) const {
        return {x + other.x, y + other.y};
    }

    constexpr Vector2 operator-(const Vector2& other) const {
        return {x - other.x, y - other.y};
    }

    constexpr Vector2 operator*(float value) const {
        return {x * value, y * value};
    }

    constexpr Vector2 operator/(float value) const {
        return {x / value, y / value};
    }

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

    Vector2& operator*=(float value) {
        x *= value;
        y *= value;
        return *this;
    }

    Vector2& operator/=(float value) {
        x /= value;
        y /= value;
        return *this;
    }

    constexpr bool operator==(const Vector2& other) const {
        return x == other.x && y == other.y;
    }

    constexpr bool operator!=(const Vector2& other) const {
        return !(*this == other);
    }
};

constexpr Vector2 operator*(float value, const Vector2& v) {
    return {v.x * value, v.y * value};
}

#endif // WOLFY_ENGINE_VECTOR2_H