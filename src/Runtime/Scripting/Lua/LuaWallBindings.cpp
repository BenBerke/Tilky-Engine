//
// Created by berke on 7/6/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaSurfaceTextureBindings.hpp"
#include "sol/sol.hpp"

namespace {
    using namespace LuaBindingMetadata;

    // One wall slot's frames, for LuaSurfaceTextureBindings::Bind.
    template<WallSurfaceSlot SLOT>
    struct WallSlotAccess {
        static const SurfaceTexture& Read(const ScriptWall& wall) { return wall.SlotTexture(SLOT); }

        template<typename Fn>
        static void Write(const ScriptWall& wall, Fn&& write) { write(wall.SlotTexture(SLOT)); }
    };

    void RegisterWallMetadata() {
        std::vector<PropertyDoc> properties = {
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
            Prop("middleTexture", "string"),
            Prop("middleTextureOffset", "Vector2"),
            Prop("middleTextureScale", "Vector2"),
            Prop("middleAnchor", "WallAnchor"),
            Prop("middleHasCollision", "boolean"),
            Prop("middleIgnoredByRaycast", "boolean"),
            Prop("blocksPathfinders", "boolean", false, "Pathfinders won't step across this wall (Doom's block-monsters line)."),
            Prop("blocksSound", "boolean", false, "A Noise Alert crosses one of these walls and stops at the second (Doom's sound-block line)."),
            Prop("tagCount", "integer", true),
        };

        std::vector<MethodDoc> methods = {
            Method("ClearTopTexture"),
            Method("ClearBottomTexture"),
            Method("ClearMiddleTexture"),
            Method("HasTag", {Param("tag", "string")}, "boolean", "True if this wall has the given tag."),
            Method("GetTag", {Param("index", "integer")}, "string", "1-based. Tags are assigned in the editor - there is no SetTag."),
        };

        LuaSurfaceTextureBindings::AppendDocs(properties, methods, "top", "Top", "Top texture");
        LuaSurfaceTextureBindings::AppendDocs(properties, methods, "bottom", "Bottom", "Bottom texture");
        LuaSurfaceTextureBindings::AppendDocs(properties, methods, "middle", "Middle", "Middle texture");

        RegisterType(Type("Wall", "A safe reference to one map wall.", std::move(properties), std::move(methods)));

        std::vector<EnumValueDoc> animationModes;
        for (const TextureAnimationModeInfo& mode : TEXTURE_ANIMATION_MODES) animationModes.push_back({mode.name, static_cast<int>(mode.mode), {}});
        RegisterType(Enum("TextureAnimation",
                          "How a wall, floor or ceiling plays its texture frames. Loop: 1 2 3 1 2 3. PingPong: 1 2 3 2 1. Once: 1 2 3, then stays on the last frame.",
                          std::move(animationModes)));

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

    // Shared with SectorFloor's floor/ceiling animations.
    sol::table textureAnimation = lua.create_named_table("TextureAnimation");
    for (const TextureAnimationModeInfo& mode : TEXTURE_ANIMATION_MODES) textureAnimation[mode.name] = static_cast<int>(mode.mode);

    sol::usertype<ScriptWall> wallType = lua.new_usertype<ScriptWall>(
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

        // Fills the wall's openings; no texture means no middle at all.
        "middleTexture", sol::property(
            &ScriptWall::GetMiddleTexture,
            &ScriptWall::SetMiddleTexture
        ),

        "ClearMiddleTexture",
        &ScriptWall::ClearMiddleTexture,

        "middleTextureOffset", sol::property(
            &ScriptWall::GetMiddleTextureOffset,
            &ScriptWall::SetMiddleTextureOffset
        ),

        "middleTextureScale", sol::property(
            &ScriptWall::GetMiddleTextureScale,
            &ScriptWall::SetMiddleTextureScale
        ),

        "middleAnchor", sol::property(
            &ScriptWall::GetMiddleAnchor,
            &ScriptWall::SetMiddleAnchor
        ),

        "blocksPathfinders", sol::property(
            &ScriptWall::GetBlocksPathfinders,
            &ScriptWall::SetBlocksPathfinders
        ),

        "blocksSound", sol::property(
            &ScriptWall::GetBlocksSound,
            &ScriptWall::SetBlocksSound
        ),

        "middleHasCollision", sol::property(
            &ScriptWall::GetMiddleHasCollision,
            &ScriptWall::SetMiddleHasCollision
        ),

        "middleIgnoredByRaycast", sol::property(
            &ScriptWall::GetMiddleIgnoredByRaycast,
            &ScriptWall::SetMiddleIgnoredByRaycast
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

    LuaSurfaceTextureBindings::Bind<ScriptWall, WallSlotAccess<WallSurfaceSlot::Top>>(wallType, "top", "Top");
    LuaSurfaceTextureBindings::Bind<ScriptWall, WallSlotAccess<WallSurfaceSlot::Bottom>>(wallType, "bottom", "Bottom");
    LuaSurfaceTextureBindings::Bind<ScriptWall, WallSlotAccess<WallSurfaceSlot::Middle>>(wallType, "middle", "Middle");
}
