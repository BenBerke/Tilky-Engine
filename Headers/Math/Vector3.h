//
// Created by berke on 4/13/2026.
//

#ifndef WOLFY_ENGINE_VECTOR3_H
#define WOLFY_ENGINE_VECTOR3_H
#include <cmath>

struct Vector3{
    float x, y, z;
    Vector3(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z - other.z); }
    Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
    template<typename T>
    Vector3 operator*(T value) const { return Vector3(x * value, y * value, z * value); }
    Vector3 operator+=(const Vector3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    Vector3 operator-=(const Vector3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
    Vector3 operator*=(const Vector3& other) {
        x *= other.x;
        y *= other.y;
        z *= other.z;
        return *this;
    }
    Vector3 operator/=(const Vector3& other) {
        x /= other.x;
        y /= other.y;
        z /= other.z;
        return *this;
    }
    bool operator==(const Vector3& other) const { return x == other.x && y == other.y && z == other.z;}
    bool operator!=(const Vector3& other) const { return !(x == other.x && y == other.y && z == other.z);}

    [[nodiscard]] float Length() const{ return std::sqrt(x*x + y*y + z*z); }
    [[nodiscard]] Vector3 Normalized() const {
        float len = Length();
        return len == 0 ? Vector3() : Vector3(x / len, y / len, z / len);
    }
    void Normalize(){
        float len = Length();
        if(len == 0) return;
        x/=len;
        y/=len;
        z/=len;
    }
    [[nodiscard]] float Dot(const Vector3& other) const{
        return x * other.x + y * other.y + z * other.z;
    }
};

#endif //WOLFY_ENGINE_VECTOR3_H