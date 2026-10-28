//
// Created by berke on 10/27/2026.
//

#ifndef TILKY_ENGINE_LINEDEF_HPP
#define TILKY_ENGINE_LINEDEF_HPP

#include <cmath>
#include <string>
#include <vector>

#include "Types.hpp"
#include "Event.hpp"
#include "../Math/Constants.hpp"
#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector2Math.hpp"

// A line that carries events like a wall does, but is never drawn, never
// collides and never splits sectors.
struct Linedef {
    ID id = INVALID_ID;

    std::string name;

    Vector2 start, end;

    // A disabled linedef fires no events and typed rays pass it by.
    bool enabled = true;

    std::vector<Event> events;
    std::vector<Dispatch> dispatches;

    // Read only — do not change
    Vector2 dir, normal, vector;
    float lengthSq = 0.0f; // lengthSquared
    float length = 0.0f;
    float angle = 0.0f;

    Linedef() = default;

    Linedef(const Vector2& start, const Vector2& end)
    : start(start),
      end(end)
    {
        RefreshDerived();
    }

    bool operator==(const Linedef&) const = default;

    // Recomputes dir/normal/vector/lengthSq/length/angle from the current
    // start/end. Call it after changing start or end.
    void RefreshDerived() {
        vector = end - start;
        lengthSq = Vector2Math::Dot(vector, vector);
        length = std::sqrt(lengthSq);

        if (length > Constants::Epsilon) {
            dir = vector / length;
            normal = {-dir.y, dir.x};
        }
        else {
            dir = {0.0f, 0.0f};
            normal = {0.0f, 0.0f};
        }

        // Degrees in [0, 360), measured from +X towards +Y along start -> end.
        if (length > Constants::Epsilon) {
            angle = std::atan2(vector.y, vector.x) * Constants::RadToDeg;
            if (angle < 0.0f) angle += 360.0f;
        }
        else angle = 0.0f;
    }
};

#endif //TILKY_ENGINE_LINEDEF_HPP
