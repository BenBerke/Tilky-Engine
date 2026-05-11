//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_VECTOR4_H
#define TILKY_ENGINE_VECTOR4_H
struct Vector4 {
    float x, y, z, w;
    Vector4(const float x = 0.0f, const float y = 0.0f, const float z = 0.0f, const float w = 0.0f) : x(x), y(y), z(z), w(w) {}
};
#endif //TILKY_ENGINE_VECTOR4_H