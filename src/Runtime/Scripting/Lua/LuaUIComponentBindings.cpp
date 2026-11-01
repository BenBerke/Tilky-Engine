// Part of LuaScriptSystem::RegisterComponentBindings (LuaComponentBindings.cpp):
// the UI component usertypes, in their own file only so each part compiles
// faster.

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include "Headers/Runtime/Gameplay/UIButtonSystem.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaRayHit.hpp"

#include <optional>
#include <tuple>

void LuaScriptSystem::RegisterUIComponentBindings(sol::state& lua) {
    lua.new_usertype<ScriptUITransform>(
        "UITransform",

        "isValid", sol::property(&ScriptUITransform::IsValid),

        "anchorMin", sol::property(
            &ScriptUITransform::GetAnchorMin,
            &ScriptUITransform::SetAnchorMin
        ),

        "anchorMax", sol::property(
            &ScriptUITransform::GetAnchorMax,
            &ScriptUITransform::SetAnchorMax
        ),

        "pivot", sol::property(
            &ScriptUITransform::GetPivot,
            &ScriptUITransform::SetPivot
        ),

        "position", sol::property(
            &ScriptUITransform::GetPosition,
            &ScriptUITransform::SetPosition
        ),

        "scale", sol::property(
            &ScriptUITransform::GetScale,
            &ScriptUITransform::SetScale
        ),

        "rotation", sol::property(
            &ScriptUITransform::GetRotation,
            &ScriptUITransform::SetRotation
        ),

        "resolvedPosition", sol::property(&ScriptUITransform::GetResolvedPosition),
        "resolvedSize", sol::property(&ScriptUITransform::GetResolvedSize)
    );

    lua.new_usertype<ScriptUISprite>(
        "UISprite",

        "isValid", sol::property(&ScriptUISprite::IsValid),

        "isActive", sol::property(
            &ScriptUISprite::GetIsActive,
            &ScriptUISprite::SetIsActive
        ),

        "textureIndex", sol::property(
            &ScriptUISprite::GetTextureIndex,
            &ScriptUISprite::SetTextureIndex
        ),

        "color", sol::property(
            &ScriptUISprite::GetColor,
            &ScriptUISprite::SetColor
        )
    );

    {
        // A changed setting shows on the sprite straight away.
        const auto boolField = [](bool ComponentUIButton::* field, const bool fallback) {
            return sol::property(
                [field, fallback](const ScriptUIButton& self) {
                    const ComponentUIButton* button = self.GetComponent();
                    return button != nullptr ? button->*field : fallback;
                },
                [field](const ScriptUIButton& self, const bool value) {
                    ComponentUIButton* button = self.GetComponent();
                    if (button == nullptr) return;
                    button->*field = value;
                    UIButtonSystem::ApplyLook(*self.level, *button);
                });
        };

        const auto colorField = [](Vector4 ComponentUIButton::* field) {
            return sol::property(
                [field](const ScriptUIButton& self) {
                    const ComponentUIButton* button = self.GetComponent();
                    return button != nullptr ? button->*field : Vector4{1.0f, 1.0f, 1.0f, 1.0f};
                },
                [field](const ScriptUIButton& self, const Vector4& value) {
                    ComponentUIButton* button = self.GetComponent();
                    if (button == nullptr) return;
                    button->*field = value;
                    UIButtonSystem::ApplyLook(*self.level, *button);
                });
        };

        const auto textureField = [](std::string ComponentUIButton::* field) {
            return sol::property(
                [field](const ScriptUIButton& self) {
                    const ComponentUIButton* button = self.GetComponent();
                    return button != nullptr ? button->*field : std::string();
                },
                [field](const ScriptUIButton& self, const std::string& value) {
                    ComponentUIButton* button = self.GetComponent();
                    if (button == nullptr) return;
                    button->*field = value;
                    UIButtonSystem::ApplyLook(*self.level, *button);
                });
        };

        const auto readOnly = [](bool ComponentUIButton::* field) {
            return sol::readonly_property([field](const ScriptUIButton& self) {
                const ComponentUIButton* button = self.GetComponent();
                return button != nullptr && button->*field;
            });
        };

        lua.new_usertype<ScriptUIButton>(
            "UIButton",

            "isValid", sol::property(&ScriptUIButton::IsValid),
            "interactable", boolField(&ComponentUIButton::interactable, false),
            "isHovered", readOnly(&ComponentUIButton::hovered),
            "isPressed", readOnly(&ComponentUIButton::pressed),

            "normalColor", colorField(&ComponentUIButton::normalColor),
            "hoverColor", colorField(&ComponentUIButton::hoverColor),
            "pressedColor", colorField(&ComponentUIButton::pressedColor),
            "disabledColor", colorField(&ComponentUIButton::disabledColor),

            "normalTexture", textureField(&ComponentUIButton::normalTexture),
            "hoverTexture", textureField(&ComponentUIButton::hoverTexture),
            "pressedTexture", textureField(&ComponentUIButton::pressedTexture),
            "disabledTexture", textureField(&ComponentUIButton::disabledTexture)
        );
    }

    lua.new_usertype<ScriptUIText>(
        "UIText",

        "isValid", sol::property(&ScriptUIText::IsValid),

        "text", sol::property(
            &ScriptUIText::GetText,
            &ScriptUIText::SetText
        ),

        "prefix", sol::property(
            &ScriptUIText::GetPrefix,
            &ScriptUIText::SetPrefix
        ),

        "suffix", sol::property(
            &ScriptUIText::GetSuffix,
            &ScriptUIText::SetSuffix
        ),

        "font", sol::property(
            &ScriptUIText::GetFont,
            &ScriptUIText::SetFont
        ),

        "fontSize", sol::property(
            &ScriptUIText::GetFontSize,
            &ScriptUIText::SetFontSize
        ),

        "color", sol::property(
            &ScriptUIText::GetColor,
            &ScriptUIText::SetColor
        )
    );
}
