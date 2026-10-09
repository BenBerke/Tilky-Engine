//
// Created by berke on 10/4/2026.
//

#ifndef TILKY_ENGINE_QUATERNIONMATH_HPP
#define TILKY_ENGINE_QUATERNIONMATH_HPP

#include "../Vector/Vector3.hpp"
#include "Quaternion.hpp"

#include <cmath>

namespace QuaternionMath {
    // The direction an identity rotation faces. Same as camera yaw 0.
    inline Vector3 LocalForward() { return {0.0f, 0.0f, 1.0f}; }

    // Rotates v by q (q is normalised first).
    inline Vector3 Rotate(const Quaternion& q, const Vector3& v) {
        const Quaternion n = q.Normalized();

        // t = 2 * cross(q.xyz, v)
        const float tx = 2.0f * (n.y * v.z - n.z * v.y);
        const float ty = 2.0f * (n.z * v.x - n.x * v.z);
        const float tz = 2.0f * (n.x * v.y - n.y * v.x);

        // v + w * t + cross(q.xyz, t)
        return {
            v.x + n.w * tx + (n.y * tz - n.z * ty),
            v.y + n.w * ty + (n.z * tx - n.x * tz),
            v.z + n.w * tz + (n.x * ty - n.y * tx)
        };
    }

    // Rotation that turns LocalForward() to face along direction, with no roll.
    // yawOnly ignores the vertical part of the direction.
    // Returns false (and leaves result untouched) for a direction with nothing to face.
    inline bool LookRotation(const Vector3& direction, const bool yawOnly, Quaternion& result) {
        const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);

        if (yawOnly) {
            if (horizontal <= Constants::Epsilon) return false;
        }
        else if (horizontal <= Constants::Epsilon && std::abs(direction.y) <= Constants::Epsilon) {
            return false;
        }

        // Straight up or down has no yaw of its own, so it keeps yaw 0.
        const float yaw = horizontal <= Constants::Epsilon ? 0.0f : std::atan2(direction.x, direction.z);
        const Quaternion yawRotation = Quaternion::FromAxisAngle(0.0f, 1.0f, 0.0f, yaw);

        if (yawOnly) {
            result = yawRotation;
            return true;
        }

        // Positive X rotation tips +Z downward, so looking up is a negative angle.
        const float pitch = -std::atan2(direction.y, horizontal);
        result = (yawRotation * Quaternion::FromAxisAngle(1.0f, 0.0f, 0.0f, pitch)).Normalized();

        return true;
    }
}

#endif //TILKY_ENGINE_QUATERNIONMATH_HPP
