//
// Created by berke on 6/23/2026.
//

#ifndef TILKY_ENGINE_SCRIPTPUBLICTYPE_HPP
#define TILKY_ENGINE_SCRIPTPUBLICTYPE_HPP

#include <cstdint>
#include <string>
#include <variant>

#include "EntityTypes.hpp"

enum class ScriptValueType : std::uint8_t {
    Int,
    Float,
    Bool,
    String
};

using ScriptValue = std::variant<
    int,
    float,
    bool,
    std::string
>;

struct ScriptPublicField {
    std::string name;
    ScriptValueType type;
    ScriptValue defaultValue;
    std::string displayName;
};

struct ScriptComponentRef {
    ID ownerID = INVALID_ID;
    std::string scriptFile;
};

#endif //TILKY_ENGINE_SCRIPTPUBLICTYPE_HPP