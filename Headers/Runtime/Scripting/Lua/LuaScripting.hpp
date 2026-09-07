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

    void RegisterVectorBindings(sol::state& lua);
    static void RegisterComponentBindings(sol::state& lua);
    void RegisterEntityBindings(sol::state& lua);
    void RegisterInputBindings(sol::state& lua);
    static void RegisterMathBindings(sol::state& lua);
    void RegisterEditorFunctionBindings(sol::state& lua);
    void RegisterGameBindings(sol::state& lua);
    void RegisterWallBindings(sol::state& lua);
    void RegisterSectorBindings(sol::state& lua);

    const std::vector<ScriptPublicField>* GetPublicFieldsForScript(const std::string& fileName);
    bool ReconcileScriptPublicValues(ComponentScript& script);
    void RefreshScriptAssets(Level& level);

    // Actually removes every GameObject queued this frame via
    // GameObject:Destroy() (or whose ComponentScript disappeared out from
    // under a running instance), firing OnDestroy on their scripts first.
    // Deliberately NOT called from Update() itself - LevelSystem::Update()
    // calls this once, after physics/transform-sync has finished for the
    // frame, so pointers those systems cached earlier in the same frame
    // (e.g. the active player controller) can't be invalidated mid-frame by
    // a script destroying their owning entity. A destroyed GameObject
    // therefore keeps behaving normally for the rest of the frame it was
    // destroyed on, exactly like Unity's Destroy().
    void FlushPendingDestroys(Level& level);
};

#endif // TILKY_ENGINE_SCRIPTSYSTEM_HPP