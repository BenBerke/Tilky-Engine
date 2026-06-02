//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP
#define TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Components.hpp"

namespace ImGuiDrawFunctions {
    struct EntityInspectorState {
        bool addingComponent = false;
        bool editingComponent = false;

        int selectedComponent = -1;
        int componentToAdd = CMP_SPRITE;
    };

    void PutSpace(int n);

    // Returns true when caller should delete the sector.
    bool DrawSectorEditor(Sector& sector, bool* open, int sectorId = -1);

    // Returns true when caller should delete the wall.
    bool DrawWallEditor(Wall& wall, bool* open, int wallId = -1);

    // Returns true when caller should delete the entity.
    bool DrawEntityEditor(Entity& entity, EntityInspectorState& state, bool* open);

    void DrawComponentEditor(Entity& entity, EntityInspectorState& state, bool* open);
}

#endif //TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP