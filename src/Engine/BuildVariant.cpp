#include "Headers/Engine/BuildVariant.hpp"

namespace BuildVariant {
    bool IsStandalone() {
#ifdef TILKY_STANDALONE
        return true;
#else
        return false;
#endif
    }
}
