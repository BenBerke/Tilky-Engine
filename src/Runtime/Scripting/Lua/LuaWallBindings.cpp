//
// Created by berke on 7/6/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

namespace {
    using namespace LuaBindingMetadata;

    void RegisterWallMetadata() {
        RegisterType(Type("Wall", "A safe reference to one map wall.", {
            Prop("id", "integer", true),
            Prop("isValid", "boolean", true),
            Prop("start", "Vector2", true),
            Prop("end", "Vector2", true),
            Prop("frontSector", "integer", true),
            Prop("backSector", "integer", true),
            Prop("dir", "Vector2", true),
            Prop("normal", "Vector2", true),
            Prop("length", "number", true),
            Prop("isPortal", "boolean", true),
            Prop("color", "Vector4"),
            Prop("topTexture", "string"),
            Prop("bottomTexture", "string"),
            Prop("topTextureOffset", "Vector2"),
            Prop("bottomTextureOffset", "Vector2"),
            Prop("topTextureScale", "Vector2"),
            Prop("bottomTextureScale", "Vector2"),
            Prop("topAnchor", "WallAnchor"),
            Prop("bottomAnchor", "WallAnchor"),
            Prop("tagCount", "integer", true),
        }, {
            Method("ClearTopTexture"),
            Method("ClearBottomTexture"),
            Method("HasTag", {Param("tag", "string")}, "boolean", "True if this wall has the given tag."),
            Method("GetTag", {Param("index", "integer")}, "string", "1-based. Tags are assigned in the editor - there is no SetTag."),
        }));

        std::vector<EnumValueDoc> anchors;
        for (const WallTextureAnchorInfo& anchor : WALL_TEXTURE_ANCHORS) anchors.push_back({anchor.name, static_cast<int>(anchor.anchor), {}});
        RegisterType(Enum("WallAnchor",
                          "Where a wall texture is pinned. Auto: top edge, except the wall above a neighbour's ceiling uses its bottom edge.",
                          std::move(anchors)));
    }
}

void LuaScriptSystem::RegisterWallBindings(sol::state& lua) {
    RegisterWallMetadata();

    sol::table wallAnchor = lua.create_named_table("WallAnchor");
    for (const WallTextureAnchorInfo& anchor : WALL_TEXTURE_ANCHORS) wallAnchor[anchor.name] = static_cast<int>(anchor.anchor);

    lua.new_usertype<ScriptWall>(
        "Wall",

        "id", sol::readonly_property(
            &ScriptWall::GetID
        ),

        "isValid", sol::readonly_property(
            &ScriptWall::IsValid
        ),

        "start", sol::readonly_property(
            &ScriptWall::GetStart
        ),

        "end", sol::readonly_property(
            &ScriptWall::GetEnd
        ),

        "frontSector", sol::readonly_property(
            &ScriptWall::GetFrontSector
        ),

        "backSector", sol::readonly_property(
            &ScriptWall::GetBackSector
        ),

        "dir", sol::readonly_property(
            &ScriptWall::GetDir
        ),

        "normal", sol::readonly_property(
            &ScriptWall::GetNormal
        ),

        "length", sol::readonly_property(
            &ScriptWall::GetLength
        ),

        "color", sol::property(
            &ScriptWall::GetColor,
            &ScriptWall::SetColor
        ),

        "isPortal", sol::readonly_property(
            &ScriptWall::IsPortal
        ),

        "topTexture", sol::property(
            &ScriptWall::GetTopTexture,
            &ScriptWall::SetTopTexture
        ),

        "bottomTexture", sol::property(
            &ScriptWall::GetBottomTexture,
            &ScriptWall::SetBottomTexture
        ),

        "ClearTopTexture",
        &ScriptWall::ClearTopTexture,

        "ClearBottomTexture",
        &ScriptWall::ClearBottomTexture,

        "topTextureOffset", sol::property(
            &ScriptWall::GetTopTextureOffset,
            &ScriptWall::SetTopTextureOffset
        ),

        "bottomTextureOffset", sol::property(
            &ScriptWall::GetBottomTextureOffset,
            &ScriptWall::SetBottomTextureOffset
        ),

        "topTextureScale", sol::property(
            &ScriptWall::GetTopTextureScale,
            &ScriptWall::SetTopTextureScale
        ),

        "bottomTextureScale", sol::property(
            &ScriptWall::GetBottomTextureScale,
            &ScriptWall::SetBottomTextureScale
        ),

        "topAnchor", sol::property(
            &ScriptWall::GetTopAnchor,
            &ScriptWall::SetTopAnchor
        ),

        "bottomAnchor", sol::property(
            &ScriptWall::GetBottomAnchor,
            &ScriptWall::SetBottomAnchor
        ),

        // Read-only: tags are assigned from the editor only - no SetTag.
        "tagCount", sol::readonly_property(
            &ScriptWall::GetTagCount
        ),

        "HasTag",
        &ScriptWall::HasTag,

        "GetTag",
        &ScriptWall::GetTag
    );
}
