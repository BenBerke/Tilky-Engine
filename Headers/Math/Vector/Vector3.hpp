#pragma once

#include <cmath>

struct Vector3 {
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