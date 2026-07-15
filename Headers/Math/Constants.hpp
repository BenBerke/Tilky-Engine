//
// Created by berke on 6/17/2026.
//

#ifndef TILKY_ENGINE_CONSTANTS_H
#define TILKY_ENGINE_CONSTANTS_H

namespace Constants {
    // --- Numerical Safeties ---
    constexpr float Epsilon      = 1e-6f;
    constexpr float LargeEpsilon = 1e-3f;        // Useful for coarse physics/collision clipping checks

    // --- Pi and Angles ---
    constexpr float Pi           = 3.14159265359f;
    constexpr float TwoPi        = 6.28318530718f; // One full rotation (Tau)
    constexpr float HalfPi       = 1.57079632679f; // 90 degrees
    constexpr float QuarterPi    = 0.78539816339f; // 45 degrees

    // --- Conversions ---
    constexpr float DegToRad     = Pi / 180.0f;    // Multiply by this to get Radians
    constexpr float RadToDeg     = 180.0f / Pi;    // Multiply by this to get Degrees

    // --- Common Algebraic Fractions ---
    constexpr float InvPi        = 0.31830988618f; // 1.0 / Pi
    constexpr float Sqrt2        = 1.41421356237f; // Diagonal of a 1x1 square
    constexpr float Sqrt3        = 1.73205080757f; // Diagonal of a 1x1x1 cube
    constexpr float InvSqrt2     = 0.70710678118f; // Useful for normalizing 45-degree 2D vectors
}
#endif //TILKY_ENGINE_CONSTANTS_H