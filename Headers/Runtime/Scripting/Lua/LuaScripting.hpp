#ifndef TILKY_ENGINE_SCRIPTSYSTEM_HPP
#define TILKY_ENGINE_SCRIPTSYSTEM_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Runtime/Scripting/IScripting.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/Runtime/EventSystem.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace sol {
    class state;
}

class LuaScriptSystem final : public IScripting {
public:
    LuaScriptSystem() = default;
    ~LuaScriptSystem() override = default;

    bool Initialize() override;

    void Start(Level& level) override;
    void Update(Level& level) override;
    void Stop(Level& level) override;
    void Shutdown() override;

    // A level switch: OnDestroy on every script except those of the
    // entities that go on into the next level (see LevelTransfer), which
    // keep running with their Lua state and Invokes.
    void StopExcept(Level& level, const std::unordered_set<ID>& keepEntities);

    // After a level switch, instead of Start: the carried scripts carry on
    // (those listed in `restart` took their level copy's values and start
    // over), every other script gets an instance and OnEnable/Start, then
    // the carried ones get OnLevelLoaded.
    void ContinueIntoLevel(Level& level, const std::vector<ScriptInstanceID>& restart);

    static void RegisterVectorBindings(sol::state& lua);
    static void RegisterComponentBindings(sol::state& lua);
    // Parts of RegisterComponentBindings, in their own files only so each
    // compiles faster: LuaGameplayComponentBindings.cpp, LuaUIComponentBindings.cpp.
    static void RegisterGameplayComponentBindings(sol::state& lua);
    static void RegisterUIComponentBindings(sol::state& lua);
    void RegisterEntityBindings(sol::state& lua);
    void RegisterInputBindings(sol::state& lua);

    // The Lua Key table as enum options (name, scancode), for public fields
    // typed `Key`. Defined in LuaInputBindings.cpp.
    static std::vector<ScriptEnumOption> KeyEnumOptions();
    static void RegisterMathBindings(sol::state& lua);

    // A float in [0, 1] from the same generator as mathT.RandomF, so
    // mathT.RandomSeed also makes Sector:RandomPointInside repeatable.
    static float SharedRandomUnitFloat();

    // Fires OnEntityEnter/OnEntityExit on sector scripts for every entity
    // that moved into or out of their sector since the last call. Called
    // once per frame by LevelSystem::Update, after positions and sector
    // membership have been updated for the frame.
    void DispatchSectorOccupancyEvents(Level& level);

    // Fires OnSectorChange(sector) on entity scripts whose entity is now in
    // a different sector than at the last call (sector is nil once the
    // entity leaves the map). Called by LevelSystem::Update right after
    // DispatchSectorOccupancyEvents.
    void DispatchSectorChangeEvents(Level& level);

    // Fires OnCollisionEnter/OnCollision/OnCollisionExit(other) and
    // OnTriggerEnter/OnTrigger/OnTriggerExit(other) on entity scripts, from
    // this frame's contacts diffed against last frame's. `contacts` may hold
    // duplicates. Called once per frame by LevelSystem::Update, after physics.
    void DispatchContactEvents(Level& level, const PhysicsSystem::Contacts& contacts);

    // Calls the global function `functionName(argument)` on every enabled
    // script of entity `entityID` that defines one, e.g. a flipbook frame
    // event. Looked up by name at call time, so it works for any function and
    // after a script reloads. Returns false if none of the entity's scripts
    // (enabled or not) defines it.
    bool DispatchEntityEvent(Level& level, ID entityID, const std::string& functionName, const std::string& argument);

    // Same, with an Entity as the argument (nil if `argumentEntityID` doesn't exist).
    bool DispatchEntityEvent(Level& level, ID entityID, const std::string& functionName, ID argumentEntityID);

    // Calls OnClick / OnRelease / OnHoverEnter / OnHoverExit (no arguments)
    // on every enabled script of the UI Button's entity. From UIButtonSystem.
    void DispatchUIButtonCallback(Level& level, ID entityID, EventSystem::UIButtonEvent kind);

    // Set Field dispatches on script fields (EventSystem::Hooks): the public
    // field `field` of the entity's `scriptIndex`-th script, read from / written
    // to its running Lua instance. Int, Float, Bool, String, Vector2/3/4, Enum
    // and Texture fields; nullopt for anything else or a missing instance.
    std::optional<std::pair<FieldValueKind, FieldValue>> GetScriptField(Level& level, ID entityID, int scriptIndex,
                                                                       const std::string& field);
    void SetScriptField(Level& level, ID entityID, int scriptIndex, const std::string& field,
                        FieldValueKind kind, const FieldValue& value);

    void RegisterEditorFunctionBindings(sol::state& lua);
    static void RegisterGameBindings(sol::state& lua);
    static void RegisterSavingBindings(sol::state& lua);
    static void RegisterMusicBindings(sol::state& lua);

    static void RegisterWallBindings(sol::state& lua);
    void RegisterSectorBindings(sol::state& lua);

    const std::vector<ScriptPublicField>* GetPublicFieldsForScript(const std::string& fileName);
    // Entity scripts: the owner label for log messages is derived from the
    // ComponentScript itself.
    bool ReconcileScriptPublicValues(ComponentScript& script);
    // Any script owner (e.g. a sector script). ownerLabel only appears in
    // log messages, e.g. "sector 3".
    bool ReconcileScriptPublicValues(ScriptAttachmentData& script, const std::string& ownerLabel);

    // Compile error of the script file, or nullptr if it compiles / doesn't
    // exist / has no file name. Compiles without running it; cached per
    // on-disk revision. For the inspector - runtime load errors are reported
    // through the console when the instance is created.
    const std::string* GetScriptLoadError(const std::string& fileName);

    // Re-reconciles every entity AND sector script's public values.
    void RefreshScriptAssets(Level& level);

    // Actually removes every Entity queued this frame via
    // Entity:Destroy() (or whose ComponentScript disappeared out from
    // under a running instance), firing OnDestroy on their scripts first.
    // Deliberately NOT called from Update() itself - LevelSystem::Update()
    // calls this once, after physics/transform-sync has finished for the
    // frame, so pointers those systems cached earlier in the same frame
    // (e.g. the active player controller) can't be invalidated mid-frame by
    // a script destroying their owning entity. A destroyed Entity
    // therefore keeps behaving normally for the rest of the frame it was
    // destroyed on, exactly like Unity's Destroy().
    void FlushPendingDestroys(Level& level);
};

#endif // TILKY_ENGINE_SCRIPTSYSTEM_HPP