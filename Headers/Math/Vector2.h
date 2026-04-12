#ifndef WOLFY_ENGINE_VECTOR2_H
#define WOLFY_ENGINE_VECTOR2_H
#include <cmath>

struct Vector2{
    float x, y;
    Vector2(const float x = 0.0f, const float y = 0.0f) : x(x), y(y) {}

    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    template<typename T>
    Vector2 operator*(T value) const { return Vector2(x * value, y * value); }
    Vector2 operator+=(const Vector2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }
    Vector2 operator-=(const Vector2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    Vector2 operator*=(const Vector2& other) {
        x *= other.x;
        y *= other.y;
        return *this;
    }
    Vector2 operator*=(const float val) {
        x *= val;
        y *= val;
        return *this;
    }
    Vector2 operator/=(const Vector2& other) {
        x /= other.x;
        y /= other.y;
        return *this;
    }
    bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vector2& other) const { return !(x == other.x && y == other.y);}

    [[nodiscard]] float Length() const{ return std::sqrt(x*x + y*y); }
    [[nodiscard]] Vector2 Normalized() const {
        float len = Length();
        return len == 0 ? Vector2() : Vector2(x / len, y / len);
    }
    void Normalize(){
        float len = Length();
        if(len == 0) return;
        x/=len;
        y/=len;
    }
    [[nodiscard]] float Dot(const Vector2& other) const{
        return x * other.x + y * other.y;
    }
    [[nodiscard]] float Cross(const Vector2& other) const {
        return x * other.y - y * other.x;
    }
};

#endif //WOLFY_ENGINE_VECTOR2_H