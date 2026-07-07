#ifndef TILKY_ENGINE_ISCRIPTING_HPP
#define TILKY_ENGINE_ISCRIPTING_HPP

#include "Headers/Objects/Level.hpp"

class IScripting {
public:
    virtual ~IScripting() = default;

    virtual bool Initialize() = 0;

    virtual void Start(Level& level) = 0;
    virtual void Update(Level& level) = 0;
    virtual void Shutdown() = 0;
};

#endif // TILKY_ENGINE_ISCRIPTING_HPP