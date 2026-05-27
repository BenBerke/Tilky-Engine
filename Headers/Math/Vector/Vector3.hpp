//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_VECTOR3_H
#define TILKY_ENGINE_VECTOR3_H
#include <cmath>

#include "Vector2.hpp"

struct Vector3{
    float x, y, z;
    Vector3(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z - other.z); }
    Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
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

    bool IsZero() const {
        auto closeToZero = [](float n) -> bool {return -.0001 < n && n < .0001;};

        return closeToZero(x) && closeToZero(y) && closeToZero(z);
    }

    template<typename T>
    Vector3 operator*(T value) const { return Vector3(x * value, y * value, z * value); }
    template<typename T>
    Vector3 operator/(T value) const {return Vector3(x / value, y / value, z / value); }
    template<typename T>
    Vector3 operator+(T value) const { return Vector3(x + value, y + value, z + value); }
    template<typename T>
    Vector3 operator-(T value) const {return Vector3(x - value, y - value, z - value); }
};

#endif //TILKY_ENGINE_VECTOR3_H