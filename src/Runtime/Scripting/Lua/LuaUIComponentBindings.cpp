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

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/UILayouts.hpp"

namespace {
    // The UI layout `name` (its file name) that the current level shows.
    std::uint64_t LevelLayoutNamed(const Level& level, const std::string& name, const char* function) {
        const UILayouts::Info* info = UILayouts::FindByName(name);
        if (info == nullptr) throw sol::error(std::string(function) + ": there is no UI layout \"" + name + "\"");

        if (std::ranges::find(level.worldSettings.uiLayouts, info->id) == level.worldSettings.uiLayouts.end() &&
            std::ranges::none_of(level.entities, [&](const Entity& entity) { return entity.layoutID == info->id; }))
            throw sol::error(std::string(function) + ": this level doesn't use the UI layout \"" + name + "\" (World Settings > UI Layouts)");

        return info->id;
    }

    void RegisterUILayoutMetadata() {
        using namespace LuaBindingMetadata;

        RegisterType(GlobalTable("UI",
            "The level's UI layouts (World Settings > UI Layouts), named by their file name without .uilayout.",
            {},
            {
                Method("SetLayoutVisible", {Param("name", "string"), Param("visible", "boolean")}, {},
                       "Shows or hides every UI entity of the layout. A hidden layout isn't drawn and its buttons don't respond; "
                       "its scripts and events keep running. It stays hidden in the next level if that level uses it too."),
                Method("IsLayoutVisible", {Param("name", "string")}, "boolean",
                       "False while the layout is hidden with SetLayoutVisible."),
            }));
    }
}

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
        ),

        "drawOrder", sol::property(
            &ScriptUISprite::GetDrawOrder,
            &ScriptUISprite::SetDrawOrder
        ),

        "fillWhenEmpty", sol::property(
            &ScriptUISprite::GetFillWhenEmpty,
            &ScriptUISprite::SetFillWhenEmpty
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

    RegisterUILayoutMetadata();

    sol::table ui = lua.create_named_table("UI");

    ui.set_function("SetLayoutVisible", [](const std::string& name, const bool visible) {
        Level& level = LevelManager::CurrentLevel();
        const std::uint64_t id = LevelLayoutNamed(level, name, "UI.SetLayoutVisible");

        std::erase(level.hiddenUILayouts, id);
        if (!visible) level.hiddenUILayouts.push_back(id);
    });

    ui.set_function("IsLayoutVisible", [](const std::string& name) {
        const Level& level = LevelManager::CurrentLevel();
        const std::uint64_t id = LevelLayoutNamed(level, name, "UI.IsLayoutVisible");
        return std::ranges::find(level.hiddenUILayouts, id) == level.hiddenUILayouts.end();
    });
}
