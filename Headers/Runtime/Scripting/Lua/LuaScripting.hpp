#ifndef TILKY_ENGINE_SCRIPTSYSTEM_HPP
#define TILKY_ENGINE_SCRIPTSYSTEM_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Runtime/Scripting/IScripting.hpp"

#include <string>
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

    static void RegisterVectorBindings(sol::state& lua);
    static void RegisterComponentBindings(sol::state& lua);
    void RegisterEntityBindings(sol::state& lua);
    void RegisterInputBindings(sol::state& lua);
    static void RegisterMathBindings(sol::state& lua);
    void RegisterEditorFunctionBindings(sol::state& lua);
    static void RegisterGameBindings(sol::state& lua);

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