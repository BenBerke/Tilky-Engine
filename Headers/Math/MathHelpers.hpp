//
// Created by berke on 9/13/2026.
//

#ifndef TILKY_ENGINE_MATHHELPERS_HPP
#define TILKY_ENGINE_MATHHELPERS_HPP

#include <type_traits>

namespace MathHelpers {
    template <typename T, typename U, typename V>
    constexpr auto InverseLerp(T a, U b, V v) {
        using CommonType = std::common_type_t<T, U, V>;

        auto a_c = static_cast<CommonType>(a);
        auto b_c = static_cast<CommonType>(b);
        auto v_c = static_cast<CommonType>(v);

        if (a_c == b_c) return static_cast<CommonType>(0);

        // Cast to float to avoid integer division
        return static_cast<float>(v_c - a_c) / static_cast<float>(b_c - a_c);
    }
}


#endif //TILKY_ENGINE_MATHHELPERS_HPP