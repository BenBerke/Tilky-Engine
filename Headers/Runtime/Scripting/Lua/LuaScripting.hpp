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
    void Shutdown() override;

    void RegisterVectorBindings(sol::state& lua);
    static void RegisterComponentBindings(sol::state& lua);
    void RegisterEntityBindings(sol::state& lua);
    void RegisterInputBindings(sol::state& lua);
    void RegisterMathBindings(sol::state& lua);
    void RegisterEditorFunctionBindings(sol::state& lua);
    void RegisterGameBindings(sol::state& lua);
    void RegisterWallBindings(sol::state& lua);
    void RegisterSectorBindings(sol::state& lua);

    const std::vector<ScriptPublicField>* GetPublicFieldsForScript(const std::string& fileName);
    bool ReconcileScriptPublicValues(ComponentScript& script);
    void RefreshScriptAssets(Level& level);
};

#endif // TILKY_ENGINE_SCRIPTSYSTEM_HPP