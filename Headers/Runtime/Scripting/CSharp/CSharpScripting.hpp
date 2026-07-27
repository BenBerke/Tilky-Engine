//
// Created by berke on 7/27/2026.
//

#ifndef TILKY_ENGINE_CSHARPSCRIPTING_HPP
#define TILKY_ENGINE_CSHARPSCRIPTING_HPP

#include "Headers/Runtime/Scripting/IScripting.hpp"

class CSharpScriptSystem final : public IScripting {
public:
    CSharpScriptSystem() = default;
    ~CSharpScriptSystem() override = default;

    bool Initialize() override;

    void Start(Level& level) override;
    void Update(Level& level) override;
    void Stop(Level& level) override;
    void Shutdown() override;
};

#endif // TILKY_ENGINE_SCRIPTSYSTEM_HPP