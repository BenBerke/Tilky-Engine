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

// ImGuiDrawFunctions.hpp
// Shared inspector/menu drawing functions for Map Editor and Runtime Editor.

namespace ImGuiDrawFunctions {

    struct EntityInspectorState {
        bool addingComponent = false;
        bool editingComponent = false;

        int selectedComponent = -1;
        int componentToAdd = CMP_SPRITE;
    };

    // ── Spacing utility ───────────────────────────────────────────────────────
    void PutSpace(int n);

    // ── Inspector windows ─────────────────────────────────────────────────────
    // Returns true when the caller should delete the object.
    // `open`      – pointer to the bool controlling window visibility (may be null)
    // `id`        – ≥0 shows the ID badge; pass -1 to hide it
    // `draggable` – use DragFloat widgets instead of InputFloat

    bool DrawSectorEditor   (Sector &sector, bool *open, int sectorId,  bool draggable);
    bool DrawWallEditor     (Wall   &wall,   bool *open, int wallId,    bool draggable);
    bool DrawEntityEditor   (Entity &entity, EntityInspectorState &state, bool *open, bool draggable);
    void DrawComponentEditor(Entity &entity, EntityInspectorState &state, bool *open, bool draggable);

    // ── Runtime-only HUD overlay ──────────────────────────────────────────────
    // Compact keybind hint shown only in Runtime Editor.
    // Call once per frame after all other ImGui windows.
    void DrawRuntimeHUD();

    // ── Mouse/keyboard focus control ──────────────────────────────────────────
    // focus=true  → allow mouse input into ImGui
    // focus=false → disable mouse from ImGui (game has it)
    void SetImGuiFocus(bool focus);

} // namespace ImGuiDrawFunctions

#endif //TILKY_ENGINE_IMGUIDRAWFUNCTIONS_HPP