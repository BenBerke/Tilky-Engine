// ImGuiDrawFunctions.cpp  –  Revamped inspector UI
// Created by berke on 6/2/2026.  Revamped for cleaner tool-style inspector.
//
// Changes vs original
// ───────────────────
// • Local helpers: DrawInspectorHeader, BeginSection/EndSection,
//   DangerButton, SmallMetaText, Tooltip, FieldWidth, ResetDragFloat3
// • Sector  : Heights / Textures / Colors / Lighting groups + validation warning
// • Wall    : Sector Links / Appearance / Texture Offset groups
// • Entity  : Summary card, component rows as cards, compact Add-Component panel
// • Component: field groups per component type (Movement, Jumping, Attenuation…)
// • Danger (Delete) buttons are red; Close is plain; both live at the bottom
// • IDs shown as small disabled metadata text
// • Input widths are consistent (FieldWidth helper)
// • ImGui focus flags: inspector windows use ImGuiWindowFlags_NoNav so they
//   do not hijack keyboard during runtime – WASD/Space/CTRL stay functional
// • DrawRuntimeHUD: compact transparent overlay with keybind hints

#include "Headers/Editor/ImGuiDrawFunctions.hpp"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <array>
#include <string>
#include <algorithm>
#include <Headers/Runtime/LevelSystem.hpp>

#include "EditorInternal.hpp"
#include "Headers/Editor/AssetBrowser.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Internal helpers (anonymous namespace)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

    // ── Component label lookup ────────────────────────────────────────────────

    static const char *GetComponentLabelKey(const int componentType) {
        switch (componentType) {
    #define COMPONENT_LABEL_CASE(Type, Bit, Storage, LabelKey) \
    case Bit: return LabelKey;
            TILKY_COMPONENTS(COMPONENT_LABEL_CASE)
    #undef COMPONENT_LABEL_CASE
            default: return "bug.unknown";
        }
    }

    static std::string GetComponentDisplayName(const int componentType) {
        return Localisation::Get(GetComponentLabelKey(componentType));
    }

    // ── AddEditorComponent (unchanged logic, just moved inside anon ns) ───────

    template<typename T>
    static void AddEditorComponent(Entity &entity) {
        if constexpr (std::is_same_v<T, ComponentPlayerController>) {
            if (!entity.HasComponent<ComponentPlayerController>()) {
                auto *pc = entity.AddComponent<ComponentPlayerController>();
                if (!entity.HasComponent<ComponentRigidbody>())  entity.AddComponent<ComponentRigidbody>();
                if (!entity.HasComponent<ComponentCollider>())    entity.AddComponent<ComponentCollider>();
                if (!entity.HasComponent<ComponentCamera>())      entity.AddComponent<ComponentCamera>();
                pc->isActive = true;
            }
        } else if constexpr (std::is_same_v<T, ComponentCamera>) {
            if (!entity.HasComponent<ComponentCamera>()) {
                auto *cam = entity.AddComponent<ComponentCamera>();
                cam->isActive = true;
            }
        } else if constexpr (std::is_same_v<T, ComponentScript>) {
            if (!entity.HasComponent<ComponentScript>()) {
                auto *script = entity.AddComponent<ComponentScript>();
                script->enabled    = true;
                script->fileName.clear();
                script->publicValues.clear();
                script->schemaHash = 0;
            }
        } else {
            if (!entity.HasComponent<T>()) entity.AddComponent<T>();
        }
    }

    static void AddEditorComponentByType(Entity &entity, const int componentType) {
        switch (componentType) {
    #define ADD_EDITOR_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    case Bit: AddEditorComponent<Type>(entity); break;
            TILKY_NORMAL_COMPONENTS(ADD_EDITOR_COMPONENT_CASE)
    #undef ADD_EDITOR_COMPONENT_CASE
            default: break;
        }
    }

    // ── Input helpers (unchanged signatures) ─────────────────────────────────

    bool InputID(const char *label, ID &value) {
        int displayValue = (value == INVALID_ID) ? -1 : static_cast<int>(value);
        if (!ImGui::InputInt(label, &displayValue)) return false;
        value = (displayValue < 0) ? INVALID_ID : static_cast<ID>(displayValue);
        return true;
    }

    template<typename T>
    bool InputOrDrag(const char *label, T *value, bool draggable, float speed = 1.0f) {
        if constexpr (std::is_same_v<T, float>)
            return draggable ? ImGui::DragFloat(label, value, speed) : ImGui::InputFloat(label, value);
        else if constexpr (std::is_same_v<T, int>)
            return draggable ? ImGui::DragInt(label, value, static_cast<int>(speed)) : ImGui::InputInt(label, value);
        return false;
    }

    bool InputOrDrag2(const char *label, float *v, bool drag, float speed = 1.0f) {
        return drag ? ImGui::DragFloat2(label, v, speed) : ImGui::InputFloat2(label, v);
    }
    bool InputOrDrag3(const char *label, float *v, bool drag, float speed = 1.0f) {
        return drag ? ImGui::DragFloat3(label, v, speed) : ImGui::InputFloat3(label, v);
    }
    bool InputOrDrag4(const char *label, float *v, bool drag, float speed = 1.0f) {
        return drag ? ImGui::DragFloat4(label, v, speed) : ImGui::InputFloat4(label, v);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  NEW: UI primitive helpers
    // ─────────────────────────────────────────────────────────────────────────

    // Standard field input width – call before the next widget
    void FieldWidth(float width = 200.0f) { ImGui::SetNextItemWidth(width); }

    // Small grey metadata text (IDs, read-only info)
    void SmallMetaText(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        ImGui::TextDisabled("%s", buf);
    }

    // Tooltip on last item
    void Tooltip(const char *text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", text);
    }

    // Section header with SeparatorText (falls back gracefully)
    void BeginSection(const char *label) {
        ImGui::Spacing();
        ImGui::SeparatorText(label);
    }

    // (No-op closer kept for readability parity)
    void EndSection() { ImGui::Spacing(); }

    // Selection summary bar – compact coloured header line
    // e.g.  DrawInspectorHeader("Sector", "#3")
    void DrawInspectorHeader(const char *kind, const char *detail = nullptr) {
        ImGuiStyle &style = ImGui::GetStyle();
        // Slightly tinted background strip
        ImVec2 pos  = ImGui::GetCursorScreenPos();
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() + style.FramePadding.y * 2.0f);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(60, 80, 110, 160), 4.0f);
        ImGui::Indent(style.FramePadding.x);
        ImGui::Spacing();
        if (detail && detail[0] != '\0')
            ImGui::Text("Selected: %s  %s", kind, detail);
        else
            ImGui::Text("Selected: %s", kind);
        ImGui::Unindent(style.FramePadding.x);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // Red "danger" delete button
    bool DangerButton(const char *label) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.08f, 0.08f, 1.0f));
        bool pressed = ImGui::Button(label);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    // Reset a float3 to zero with a small inline button
    bool ResetFloat3Button(const char *id, float *v) {
        ImGui::PushID(id);
        ImGui::SameLine();
        bool pressed = ImGui::SmallButton("Reset");
        if (pressed) { v[0] = 0.0f; v[1] = 0.0f; v[2] = 0.0f; }
        ImGui::PopID();
        return pressed;
    }

    // Script public-field editor (unchanged logic)
    void DrawScriptValueEditor(const ScriptPublicField &field, ScriptValue &value) {
        switch (field.type) {
            case ScriptValueType::Int: {
                int *iv = std::get_if<int>(&value);
                if (!iv) return;
                ImGui::DragInt(field.displayName.c_str(), iv);
                break;
            }
            case ScriptValueType::Float: {
                float *fv = std::get_if<float>(&value);
                if (!fv) return;
                ImGui::DragFloat(field.displayName.c_str(), fv, 0.1f);
                break;
            }
            case ScriptValueType::Bool: {
                bool *bv = std::get_if<bool>(&value);
                if (!bv) return;
                ImGui::Checkbox(field.displayName.c_str(), bv);
                break;
            }
            case ScriptValueType::String: {
                std::string *sv = std::get_if<std::string>(&value);
                if (!sv) return;
                ImGui::InputText(field.displayName.c_str(), sv);
                break;
            }
        }
    }

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
//  ImGuiDrawFunctions implementation
// ─────────────────────────────────────────────────────────────────────────────
namespace ImGuiDrawFunctions {
    using namespace Localisation;

    // Runtime-safe inspector window flags:
    // NoNav prevents the window from stealing keyboard focus (WASD stays functional).
    static constexpr ImGuiWindowFlags kInspectorFlags =
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse;

    void PutSpace(const int n) {
        for (int i = 0; i < n; i++) ImGui::Spacing();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Sector Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawSectorEditor(Sector& sector, bool* open, const int sectorId, const bool draggable) {
    constexpr float MIN_ROOM_HEIGHT = 0.01f;

    bool deleteRequested = false;
    int floorToRemove = -1;

    if (sector.floors.empty()) {
        sector.floors.push_back({
            {0.0f, {255.0f, 255.0f, 255.0f}, {}},
            {40.0f, {255.0f, 255.0f, 255.0f}, {}}
        });
    }

    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(Get("sector.title").c_str(), open, kInspectorFlags)) {
        ImGui::End();
        return false;
    }

    char idBuf[32] = "";
    if (sectorId >= 0) snprintf(idBuf, sizeof(idBuf), "#%d", sectorId);
    DrawInspectorHeader("Sector", idBuf);

    for (size_t floorIndex = 0; floorIndex < sector.floors.size(); ++floorIndex) {
        ImGui::PushID(static_cast<int>(floorIndex));

        SectorFloor &sectorFloor = sector.floors[floorIndex];
        const std::string sectionTitle =
                Get("sector.floor") + " " + std::to_string(floorIndex + 1);

        BeginSection(sectionTitle.c_str());

        // ── Heights ──────────────────────────────────────────────────────────

        float floorHeight = sectorFloor.floor.height;

        FieldWidth(160.0f);

        if (InputOrDrag(Get("sector.floor_height").c_str(), &floorHeight, draggable)) {
            const float minimumHeight =
                    floorIndex > 0
                        ? sector.floors[floorIndex - 1].ceiling.height
                        : std::numeric_limits<float>::lowest();

            const float maximumHeight =
                    sectorFloor.ceiling.height - MIN_ROOM_HEIGHT;

            if (minimumHeight <= maximumHeight) {
                sectorFloor.floor.height =
                        std::clamp(floorHeight, minimumHeight, maximumHeight);
            }
        }

        Tooltip(Get("editor.tooltip.sector.floor_height").c_str());

        float ceilingHeight = sectorFloor.ceiling.height;

        FieldWidth(160.0f);

        if (InputOrDrag(Get("sector.ceil_height").c_str(), &ceilingHeight, draggable)) {
            const float minimumHeight =
                    sectorFloor.floor.height + MIN_ROOM_HEIGHT;

            const float maximumHeight =
                    floorIndex + 1 < sector.floors.size()
                        ? sector.floors[floorIndex + 1].floor.height
                        : std::numeric_limits<float>::max();

            if (minimumHeight <= maximumHeight) {
                sectorFloor.ceiling.height =
                        std::clamp(ceilingHeight, minimumHeight, maximumHeight);
            }
        }

        Tooltip(Get("editor.tooltip.sector.ceil_height").c_str());

        const bool invalidRoom =
                sectorFloor.floor.height >= sectorFloor.ceiling.height;

        const bool overlapsPrevious =
                floorIndex > 0 &&
                sectorFloor.floor.height <
                sector.floors[floorIndex - 1].ceiling.height;

        const bool overlapsNext =
                floorIndex + 1 < sector.floors.size() &&
                sectorFloor.ceiling.height >
                sector.floors[floorIndex + 1].floor.height;

        if (invalidRoom || overlapsPrevious || overlapsNext) {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(1.0f, 0.6f, 0.1f, 1.0f)
            );

            ImGui::TextWrapped(
                Get("editor.tooltip.sector.invalid_floor_heights").c_str()
            );

            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Textures ─────────────────────────────────────────────────────────

        MapEditorInternal::DrawAssetField(
            Get("sector.floor_texture").c_str(),
            sectorFloor.floor.texture,
            AssetKind::Texture,
            48.0f
        );

        Tooltip(Get("editor.tooltip.sector.floor_texture").c_str());

        MapEditorInternal::DrawAssetField(
            Get("sector.ceil_texture").c_str(),
            sectorFloor.ceiling.texture,
            AssetKind::Texture,
            48.0f
        );

        Tooltip(Get("editor.tooltip.sector.ceil_texture").c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Colors ───────────────────────────────────────────────────────────

        FieldWidth(220.0f);

        InputOrDrag3(
            Get("sector.floor_color").c_str(),
            &sectorFloor.floor.color.x,
            draggable
        );

        ResetFloat3Button(
            "reset_floor_color",
            &sectorFloor.floor.color.x
        );

        FieldWidth(220.0f);

        InputOrDrag3(
            Get("sector.ceil_color").c_str(),
            &sectorFloor.ceiling.color.x,
            draggable
        );

        ResetFloat3Button(
            "reset_ceil_color",
            &sectorFloor.ceiling.color.x
        );

        ImGui::Spacing();

        ImGui::BeginDisabled(sector.floors.size() <= 1);

        if (DangerButton(Get("sector.remove_floor").c_str())) {
            floorToRemove = static_cast<int>(floorIndex);
        }

        ImGui::EndDisabled();

        Tooltip(Get("editor.tooltip.sector.remove_floor").c_str());

        EndSection();
        ImGui::PopID();
    }

    if (floorToRemove >= 0) {
        sector.floors.erase(sector.floors.begin() + floorToRemove);
    }

    ImGui::Spacing();

    if (ImGui::Button(Get("sector.add_floor").c_str())) {
        const float floorHeight =
                sector.floors.empty()
                    ? 0.0f
                    : sector.floors.back().ceiling.height + 10.0f;

        sector.floors.push_back({
            {
                floorHeight,
                {255.0f, 255.0f, 255.0f},
                {}
            },
            {
                floorHeight + 40.0f,
                {255.0f, 255.0f, 255.0f},
                {}
            }
        });
    }

    Tooltip(Get("editor.tooltip.sector.add_floor").c_str());

    // ── Lighting ─────────────────────────────────────────────────────────────

    BeginSection(Get("sector.lighting").c_str());

    FieldWidth(160.0f);

    InputOrDrag(
        Get("sector.light_value").c_str(),
        &sector.lightValue,
        draggable
    );

    Tooltip(Get("editor.tooltip.sector.light_value").c_str());

    EndSection();

    // ── Meta ─────────────────────────────────────────────────────────────────

    if (sectorId >= 0) {
        ImGui::Spacing();
        SmallMetaText("ID: %d", sector.id);
    }

    // ── Actions ──────────────────────────────────────────────────────────────

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (DangerButton(Get("common.delete").c_str())) {
        deleteRequested = true;
        if (open != nullptr) *open = false;
    }

    Tooltip(Get("editor.tooltip.sector.delete").c_str());

    ImGui::SameLine();

    if (ImGui::Button(Get("common.close").c_str())) {
        if (open != nullptr) *open = false;
    }

    ImGui::End();
    return deleteRequested;
    }
    // ─────────────────────────────────────────────────────────────────────────
    //  Wall Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawWallEditor(Wall &wall, bool *open, const int wallId, const bool draggable) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Get("wall.title").c_str(), open, kInspectorFlags)) {
            ImGui::End();
            return false;
        }

        // ── Summary header ───────────────────────────────────────────────────
        char idBuf[32] = "";
        if (wallId >= 0) snprintf(idBuf, sizeof(idBuf), "#%d", wallId);
        DrawInspectorHeader("Wall", idBuf);

        // ── Sector Links ─────────────────────────────────────────────────────
        BeginSection("Sector Links");

        bool wallSectorChanged = false;

        FieldWidth(100.0f);
        wallSectorChanged |= InputID(Get("wall.front_sector").c_str(), wall.frontSector);
        Tooltip(Get("editor.tooltip.wall.front_sector").c_str());

        FieldWidth(100.0f);
        wallSectorChanged |= InputID(Get("wall.back_sector").c_str(), wall.backSector);
        Tooltip(Get("editor.tooltip.wall.back_sector").c_str());

        if (wallSectorChanged)
            MapQueries::RebuildSectorRuntimeLinks(LevelManager::CurrentLevel());

        SmallMetaText("Front: %s   Back: %s",
                      wall.frontSector == INVALID_ID
                          ? "none"
                          : std::to_string(static_cast<int>(wall.frontSector)).c_str(),
                      wall.backSector == INVALID_ID
                          ? "none"
                          : std::to_string(static_cast<int>(wall.backSector)).c_str());

        EndSection();

        // ── Appearance ───────────────────────────────────────────────────────
        BeginSection("Appearance");

        MapEditorInternal::DrawAssetField(
            Get("wall.texture_index").c_str(),
            wall.textureFileName,
            AssetKind::Texture,
            48.0f
        );
        Tooltip(Get("editor.tooltip.wall.texture").c_str());

        FieldWidth(220.0f);
        InputOrDrag4(Get("wall.color").c_str(), &wall.color.x, draggable);
        Tooltip(Get("editor.tooltip.wall.color").c_str());

        EndSection();

        // ── Texture Offset ───────────────────────────────────────────────────
        BeginSection("Texture Offset");

        FieldWidth(200.0f);
        InputOrDrag2(Get("wall.texture_offset").c_str(), &wall.textureOffset.x, draggable);
        Tooltip(Get("editor.tooltip.wall.texture_offset").c_str());

        EndSection();

        // ── Meta ─────────────────────────────────────────────────────────────
        if (wallId >= 0) {
            ImGui::Spacing();
            SmallMetaText("ID: %d   internal id: %d", wallId, wall.id);
        }

        // ── Actions ──────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (DangerButton(Get("common.delete").c_str())) {
            deleteRequested = true;
            if (open) *open = false;
        }
        Tooltip(Get("editor.tooltip.wall.delete").c_str());

        ImGui::SameLine();

        if (ImGui::Button(Get("common.close").c_str()))
            if (open) *open = false;

        ImGui::End();
        return deleteRequested;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Entity Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawEntityEditor(Entity &entity, EntityInspectorState &state, bool *open, const bool draggable) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Get("entity.title").c_str(), open, kInspectorFlags)) {
            ImGui::End();
            return false;
        }

        // ── Identity card ────────────────────────────────────────────────────
        {
            char hdr[128];
            snprintf(hdr, sizeof(hdr), "%s (#%u)",
                     entity.name.empty() ? "unnamed" : entity.name.c_str(),
                     entity.id);
            DrawInspectorHeader("Entity", hdr);
        }

        BeginSection("Identity");
        SmallMetaText("ID: %u", entity.id);
        FieldWidth(220.0f);
        ImGui::InputText(Get("entity.name").c_str(), &entity.name);
        EndSection();

        // ── Components list ──────────────────────────────────────────────────
        BeginSection("Components");

        // Each component rendered as a card-like row: [name]  [Edit ▶]
        auto DrawComponentCard = [&](const char *label, const int componentType) {
            ImGui::PushID(componentType);

            // Subtle background for the row
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            float  rowW   = ImGui::GetContentRegionAvail().x;
            float  rowH   = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.0f + 2.0f;

            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH),
                              IM_COL32(40, 50, 65, 130), 3.0f);

            ImGui::Spacing();
            ImGui::Indent(6.0f);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(rowW - 56.0f);

            if (ImGui::SmallButton("Edit")) {
                state.selectedComponent  = componentType;
                state.editingComponent   = true;
            }

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
            ImGui::PopID();
        };

#define DRAW_ENTITY_COMPONENT_ROW(Type, Bit, Storage, LabelKey) \
        if (entity.HasComponent<Type>()) \
            DrawComponentCard(Get(LabelKey).c_str(), Bit);

        TILKY_NORMAL_COMPONENTS(DRAW_ENTITY_COMPONENT_ROW)

#undef DRAW_ENTITY_COMPONENT_ROW

        EndSection();

        // ── Add Component panel ───────────────────────────────────────────────
        BeginSection("Add Component");

        if (ImGui::Button(state.addingComponent ? "Cancel" : Get("entity.add_component").c_str()))
            state.addingComponent = !state.addingComponent;

        if (state.addingComponent) {
            ImGui::PushID("add_component_combo");

            std::array<std::string, CMP_NORMAL_COUNT> componentNames{};

#define FILL_COMPONENT_NAME(Type, Bit, Storage, LabelKey) \
            componentNames[Bit] = Get(LabelKey);
            TILKY_NORMAL_COMPONENTS(FILL_COMPONENT_NAME)
#undef FILL_COMPONENT_NAME

            FieldWidth(200.0f);
            if (ImGui::BeginCombo(
                    Get("component.component").c_str(),
                    componentNames[state.componentToAdd].c_str()))
            {
                for (int i = 0; i < CMP_NORMAL_COUNT; i++) {
                    if (i == CMP_TRANSFORM) continue;
                    const bool isSel = (state.componentToAdd == i);
                    if (ImGui::Selectable(componentNames[i].c_str(), isSel))
                        state.componentToAdd = i;
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            //  ImGui::SameLine();
            if (ImGui::Button(Get("common.add").c_str())) {
                AddEditorComponentByType(entity, state.componentToAdd);
                state.addingComponent = false;
                state.componentToAdd  = CMP_SPRITE;
            }

            ImGui::PopID();
        }

        EndSection();

        // ── Actions ──────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushID("entity_buttons");

        if (DangerButton(Get("common.delete").c_str())
            || InputManager::GetKeyDown(SDL_SCANCODE_DELETE))
        {
            deleteRequested            = true;
            state.editingComponent     = false;
            state.selectedComponent    = -1;
            if (open) *open = false;
        }
        Tooltip(Get("editor.tooltip.entity.common.delete").c_str());

        ImGui::SameLine();
        if (ImGui::Button(Get("common.close").c_str())) {
            state.editingComponent  = false;
            state.selectedComponent = -1;
            if (open) *open = false;
        }

        ImGui::PopID();

        ImGui::End();
        return deleteRequested;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Component Editor
    // ─────────────────────────────────────────────────────────────────────────
    void DrawComponentEditor(Entity &entity, EntityInspectorState &state, bool *open, const bool draggable) {
        if (state.selectedComponent == -1) {
            state.editingComponent = false;
            if (open) *open = false;
            return;
        }

        const std::string componentName = GetComponentDisplayName(state.selectedComponent);
        const std::string windowTitle = componentName + "##component_editor";

        bool fallbackOpen = true;
        bool *windowOpen = open ? open : &fallbackOpen;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(windowTitle.c_str(), windowOpen, kInspectorFlags)) {
            ImGui::End();
            if (!*windowOpen) {
                state.editingComponent = false;
                state.selectedComponent = -1; }
            return;
        }

        ImGui::PushID(state.selectedComponent);

        bool closeRequested = false;
        auto CloseEditor    = [&]() { closeRequested = true; };

        // ── Summary header ────────────────────────────────────────────────────
        DrawInspectorHeader("Component", componentName.c_str());

        // ════════════════════════════════════════════════════════════════════
        //  Transform
        // ════════════════════════════════════════════════════════════════════
        if (state.selectedComponent == CMP_TRANSFORM) {
            auto *c = entity.GetComponent<ComponentTransform>();
            if (c) {
                BeginSection("Position");
                ImGui::TextDisabled("X                Y               Z(height)");
                FieldWidth(220.0f);
                InputOrDrag3("##pos", &c->position.x, draggable);
                ResetFloat3Button("rst_pos", &c->position.x);
                EndSection();

                BeginSection("Scale");
                ImGui::TextDisabled("X                Y               Z");
                FieldWidth(220.0f);
                InputOrDrag3("##scale", &c->scale.x, draggable);
                ResetFloat3Button("rst_scale", &c->scale.x);
                EndSection();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentTransform>();
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Transform component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Sprite
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_SPRITE) {
            auto *c = entity.GetComponent<ComponentSprite>();

            if (c) {
                constexpr float BOX_SIZE = 64.0f;
                constexpr float SLOT_HEIGHT = 64.0f + 18.0f + 24.0f + 22.0f + 8.0f;

                auto DrawSpriteSlot = [&](const int slotIndex, const char *label) {
                    ImGui::PushID(slotIndex);
                    MapEditorInternal::DrawAssetField(label, c->textureFileNames[slotIndex], AssetKind::Texture, BOX_SIZE);
                    ImGui::PopID();
                };

                auto DrawEmptySlot = [&]() {ImGui::Dummy(ImVec2(BOX_SIZE, SLOT_HEIGHT));};

                BeginSection("Rendering");
                FieldWidth(160.0f);

                int directionMode = 0;

                if (c->sideCount == SIDECOUNT_SINGLE) directionMode = 0;
                else if (c->sideCount == SIDECOUNT_90) directionMode = 1;
                else if (c->sideCount == SIDECOUNT_45) directionMode = 2;
                else {
                    c->sideCount = SIDECOUNT_SINGLE;
                    directionMode = 0;
                }

                const std::string singleText = Get("component.sprite.directions.single");
                const std::string sided90Text = Get("component.sprite.directions.90");
                const std::string sided45Text = Get("component.sprite.directions.45");

                const char *directionModes[] = {
                    singleText.c_str(),
                    sided90Text.c_str(),
                    sided45Text.c_str()
                };

                ImGui::Text("%s", Get("component.sprite.directional").c_str());
                ImGui::SameLine();

                ImGui::SetNextItemWidth(160.0f);

                if (ImGui::Combo("##sprite_direction_mode", &directionMode, directionModes, 3)) {
                    if (directionMode == 0) c->sideCount = SIDECOUNT_SINGLE;
                    else if (directionMode == 1) c->sideCount = SIDECOUNT_90;
                    else if (directionMode == 2) c->sideCount = SIDECOUNT_45;
                }

                ImGui::Spacing();

                if (ImGui::SmallButton("Clear All")) {
                    for (std::string& name : c->textureFileNames) name.clear();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                constexpr float CELL_PADDING = 8.0f;
                constexpr float COLUMN_WIDTH = BOX_SIZE + CELL_PADDING;

                if (c->sideCount == SIDECOUNT_SINGLE) {
                    ImGui::PushID("sprite_single");
                    DrawSpriteSlot(0, "Default");
                    ImGui::PopID();
                } else if (c->sideCount == SIDECOUNT_90) {
                    ImGui::PushID("sprite_90");

                    if (ImGui::BeginTable(
                        "##sprite_90_table",
                        3,
                        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit,
                        ImVec2(COLUMN_WIDTH * 3.0f, 0.0f)
                    )) {
                        ImGui::TableSetupColumn("C0", ImGuiTableColumnFlags_WidthFixed, COLUMN_WIDTH);
                        ImGui::TableSetupColumn("C1", ImGuiTableColumnFlags_WidthFixed, COLUMN_WIDTH);
                        ImGui::TableSetupColumn("C2", ImGuiTableColumnFlags_WidthFixed, COLUMN_WIDTH);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawEmptySlot();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(0, "N");
                        ImGui::TableNextColumn();
                        DrawEmptySlot();

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(6, "W");
                        ImGui::TableNextColumn();
                        DrawEmptySlot();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(2, "E");

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawEmptySlot();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(4, "S");
                        ImGui::TableNextColumn();
                        DrawEmptySlot();

                        ImGui::EndTable();
                    }

                    ImGui::PopID();
                }
                else if (c->sideCount == SIDECOUNT_45) {
                    ImGui::PushID("sprite_45");

                    if (ImGui::BeginTable(
                        "##sprite_45_table",
                        3,
                        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit,
                        ImVec2(COLUMN_WIDTH * 3.0f, 0.0f)
                    )) {
                        ImGui::TableSetupColumn("C0", ImGuiTableColumnFlags_WidthFixed, COLUMN_WIDTH);
                        ImGui::TableSetupColumn("C1", ImGuiTableColumnFlags_WidthFixed, COLUMN_WIDTH);
                        ImGui::TableSetupColumn("C2", ImGuiTableColumnFlags_WidthFixed, COLUMN_WIDTH);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(7, "NW");
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(0, "N");
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(1, "NE");

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(6, "W");
                        ImGui::TableNextColumn();
                        DrawEmptySlot();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(2, "E");

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(5, "SW");
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(4, "S");
                        ImGui::TableNextColumn();
                        DrawSpriteSlot(3, "SE");

                        ImGui::EndTable();
                    }

                    ImGui::PopID();
                }

                EndSection();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentSprite>();
                    CloseEditor();
                }
            }
            else ImGui::TextDisabled("Sprite component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Decal
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_DECAL) {
            auto *c = entity.GetComponent<ComponentDecal>();
            if (c) {
                BeginSection("Type");

                const std::array typeLabels = {
                    Get("component.decal.type.wall"),
                    Get("component.decal.type.floor")
                };
                const char *items[] = { typeLabels[0].c_str(), typeLabels[1].c_str() };
                static int selectedTypeIndex = 0;

                FieldWidth(140.0f);
                ImGui::Combo(Get("component.decal.type").c_str(),
                             &selectedTypeIndex, items, IM_ARRAYSIZE(items));

                c->type = static_cast<DecalType>(selectedTypeIndex);

                EndSection();

                BeginSection("Placement");

                FieldWidth(160.0f);
                InputOrDrag(Get("component.decal.attached_wall").c_str(), &c->wallIndex, draggable);
                InputOrDrag(Get("component.decal.wall_offset").c_str(), &c->horizontalPos, draggable);
                InputOrDrag(Get("component.decal.wall_normal_offset").c_str(), &c->wallNormalOffset, draggable);
                InputOrDrag("Wall T", &c->wallT, draggable);
                Tooltip(Get("editor.tooltip.component.decal.wall_t").c_str());
                EndSection();

                BeginSection("Vertical");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.decal.z_offset").c_str(), &c->verticalPos, draggable);
                InputOrDrag("Base Height",                             &c->baseHeight,  draggable);
                ImGui::Checkbox(Get("component.decal.abs_height").c_str(), &c->absHeight);
                Tooltip(Get("editor.tooltip.component.decal.abs_height").c_str());
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentDecal>();
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Decal component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Audio Source
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_AUDIO_SOURCE) {
            auto *c = entity.GetComponent<ComponentAudioSource>();
            if (c) {
                BeginSection("Sound");
                MapEditorInternal::DrawAssetField("Sound", c->soundFileName, AssetKind::Sound);
                Tooltip(Get("editor.tooltip.component.audio_source.sound").c_str());
                FieldWidth(120.0f);
                InputOrDrag(Get("component.audio_source.pitch").c_str(), &c->pitch, draggable, 0.01f);
                FieldWidth(120.0f);
                InputOrDrag(Get("component.audio_source.gain").c_str(),  &c->gain,  draggable, 0.01f);
                ImGui::Checkbox(Get("component.audio_source.looping").c_str(),       &c->looping);
                ImGui::Checkbox(Get("component.audio_source.play_on_start").c_str(), &c->playOnStart);
                EndSection();

                BeginSection("Attenuation");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.audio_source.ref_distance").c_str(), &c->referenceDistance, draggable, 0.1f);
                Tooltip(Get("editor.tooltip.component.audio_source.ref_distance").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.audio_source.max_distance").c_str(), &c->maxDistance,       draggable, 1.0f);
                Tooltip(Get("editor.tooltip.component.audio_source.max_distance").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.audio_source.rolloff").c_str(),       &c->rollOffFactor,     draggable, 0.01f);
                Tooltip(Get("editor.tooltip.component.audio_source.rolloff").c_str());
                EndSection();

                BeginSection("Cone");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.audio_source.inner_angle").c_str(), &c->innerConeAngle, draggable, 1.0f);
                Tooltip(Get("editor.tooltip.component.audio_source.inner_angle").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.audio_source.outer_angle").c_str(), &c->outerConeAngle, draggable, 1.0f);
                Tooltip(Get("editor.tooltip.component.audio_source.outer_angle").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.audio_source.outer_gain").c_str(),  &c->outerGain,      draggable, 0.01f);
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentAudioSource>();
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Audio component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Script
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_SCRIPT) {
            auto *c = entity.GetComponent<ComponentScript>();

            if (c) {
                BeginSection("Script File");

                if (MapEditorInternal::DrawAssetField(Get("component.script.file_name").c_str(), c->fileName, AssetKind::Script)) {
                    LevelSystem::ReconcileScriptPublicValues(*c);
                }
                Tooltip(Get("editor.tooltip.component.script.file").c_str());

                ImGui::Checkbox(Get("component.script.enabled").c_str(), &c->enabled);

                ImGui::SameLine();

                if (ImGui::SmallButton("Refresh Fields")) {
                    LevelSystem::ReconcileScriptPublicValues(*c);
                }

                EndSection();

                const std::vector<ScriptPublicField> *fields =
                        LevelSystem::GetPublicFieldsForScript(c->fileName);

                if (fields == nullptr) {
                    if (!c->fileName.empty()) {
                        ImGui::TextDisabled("Script not found or has no public fields.");
                    }
                } else {
                    BeginSection("Public Variables");

                    for (const ScriptPublicField &field: *fields) {
                        auto valueIt = c->publicValues.find(field.name);

                        if (valueIt == c->publicValues.end()) {
                            c->publicValues[field.name] = field.defaultValue;
                            valueIt = c->publicValues.find(field.name);
                        }

                        DrawScriptValueEditor(field, valueIt->second);
                    }

                    EndSection();

                    BeginSection("Orphaned Variables");

                    bool hasOrphans = false;

                    for (auto valueIt = c->publicValues.begin(); valueIt != c->publicValues.end();) {
                        const std::string &valueName = valueIt->first;

                        const bool existsInSchema = std::ranges::any_of(
                            *fields,
                            [&valueName](const ScriptPublicField &field) {
                                return field.name == valueName;
                            }
                        );

                        if (existsInSchema) {
                            ++valueIt;
                            continue;
                        }

                        hasOrphans = true;

                        SmallMetaText("%s", valueName.c_str());

                        ImGui::SameLine();

                        const std::string delLabel = "Remove##orphan_" + valueName;

                        if (ImGui::SmallButton(delLabel.c_str())) {
                            valueIt = c->publicValues.erase(valueIt);
                        } else {
                            ++valueIt;
                        }
                    }

                    if (!hasOrphans) {
                        ImGui::TextDisabled("None");
                    }

                    EndSection();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentScript>();
                    CloseEditor();
                }
            } else {
                ImGui::TextDisabled("Script component missing");
            }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Player Controller
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_PLAYER_CONTROLLER) {
            auto *c = entity.GetComponent<ComponentPlayerController>();
            if (c) {
                BeginSection("Movement");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.player_controller.speed").c_str(),         &c->speed,        draggable);
                FieldWidth(160.0f);
                InputOrDrag(Get("component.player_controller.running_speed").c_str(), &c->runningSpeed, draggable);
                FieldWidth(-1.0f); // full width slider
                ImGui::SliderFloat(Get("component.player_controller.friction").c_str(), &c->friction, 0.0f, 1.0f);
                Tooltip(Get("editor.tooltip.component.player_controller.friction").c_str());
                EndSection();

                BeginSection("Jumping");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.player_controller.jump_power").c_str(),      &c->jumpPower,     draggable);
                FieldWidth(160.0f);
                InputOrDrag(Get("component.player_controller.jump_buffer_ms").c_str(),  &c->jumpBufferMs,  draggable);
                Tooltip(Get("editor.tooltip.component.player_controller.jump_buffer").c_str());
                EndSection();

                BeginSection("Camera");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.player_controller.eye_height").c_str(), &c->eyeHeight, draggable);
                Tooltip(Get("editor.tooltip.component.player_controller.eye_height").c_str());
                FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.player_controller.sensitivity_x").c_str(),
                                   &c->sensitivityX, 0.001f, 2.0f);
                //FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.player_controller.sensitivity_y").c_str(),
                                   &c->sensitivityY, 0.001f, 2.0f);
                //FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.player_controller.max_pitch").c_str(),
                                   &c->maxPitch, 0.0f, 360.0f);
                //FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.player_controller.min_pitch").c_str(),
                                   &c->minPitch, 0.0f, 360.0f);
                //FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.player_controller.max_yaw").c_str(),
                                   &c->maxYaw, 0.0f, 360.0f);
                //FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.player_controller.min_yaw").c_str(),
                                   &c->minYaw, 0.0f, 360.0f);
                if (c->minYaw < .0f) c->minYaw = 0.0f;

                EndSection();

                BeginSection("State");
                ImGui::Checkbox(Get("component.player_controller.no_clip").c_str(),  &c->noClip);
                Tooltip(Get("editor.tooltip.component.player_controller.no_clip").c_str());
                ImGui::Checkbox(Get("component.player_controller.is_active").c_str(), &c->isActive);
                Tooltip(Get("editor.tooltip.component.player_controller.is_active").c_str());
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentPlayerController>();
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Player Controller component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Camera
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_CAMERA) {
            auto *c = entity.GetComponent<ComponentCamera>();
            if (c) {
                BeginSection("Projection");
                FieldWidth(-1.0f);
                ImGui::SliderFloat(Get("component.camera.fov").c_str(), &c->fov, 1.0f, 179.0f);
                Tooltip(Get("editor.tooltip.component.camera.fov").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.camera.aspect_ratio").c_str(), &c->aspectRatio, draggable);
                EndSection();

                BeginSection("Clipping Planes");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.camera.near_plane").c_str(), &c->nearPlane, draggable);
                Tooltip(Get("editor.tooltip.component.camera.near_plane").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.camera.far_plane").c_str(),  &c->farPlane,  draggable);
                Tooltip(Get("editor.tooltip.component.camera.far_plane").c_str());
                EndSection();

                BeginSection("State");
                ImGui::Checkbox(Get("component.camera.is_active").c_str(), &c->isActive);
                Tooltip(Get("editor.tooltip.component.camera.is_active").c_str());
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentCamera>();
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Camera component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Collider
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_COLLIDER) {
            auto *c = entity.GetComponent<ComponentCollider>();
            if (c) {
                BeginSection("Shape");

                const std::array<std::string, 2> typeLabels = {
                    Get("component.collider.type.sphere"),
                    Get("component.collider.type.box")
                };
                const char *items[] = { typeLabels[0].c_str(), typeLabels[1].c_str() };
                static int selectedColliderIndex = 0;

                FieldWidth(140.0f);
                ImGui::Combo(Get("component.collider.type").c_str(),
                             &selectedColliderIndex, items, IM_ARRAYSIZE(items));

                if (selectedColliderIndex == 0) {
                    FieldWidth(120.0f);
                    InputOrDrag(Get("component.collider.sphere.scale").c_str(), &c->scale.x, draggable);
                    Tooltip(Get("editor.tooltip.component.collider.sphere_radius").c_str());
                }
                else {
                    ImGui::TextDisabled("X                Y               Z");
                    FieldWidth(220.0f);
                    InputOrDrag3("##collider_scale", &c->scale.x, draggable);
                }

                EndSection();

                BeginSection("Behaviour");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.collider.step_size").c_str(), &c->stepSize, draggable);
                Tooltip(Get("editor.tooltip.component.collider.step_size").c_str());
                ImGui::Checkbox(Get("component.collider.is_trigger").c_str(), &c->isTrigger);
                Tooltip(Get("editor.tooltip.component.collider.is_trigger").c_str());
                ImGui::Checkbox(Get("component.collider.is_active").c_str(), &c->isActive);
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentCollider>();
                    CloseEditor();
                }
            } else ImGui::TextDisabled("Collider component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Rigidbody
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_RIGIDBODY) {
            auto *c = entity.GetComponent<ComponentRigidbody>();
            if (c) {
                BeginSection("Physics");
                FieldWidth(160.0f);
                InputOrDrag(Get("component.rigidbody.mass").c_str(),          &c->mass,         draggable);
                Tooltip(Get("editor.tooltip.component.rigidbody.mass").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.rigidbody.gravityScale").c_str(),  &c->gravityScale, draggable);
                Tooltip(Get("editor.tooltip.component.rigidbody.gravity_scale").c_str());
                FieldWidth(160.0f);
                InputOrDrag(Get("component.rigidbody.friction").c_str(),      &c->friction,     draggable);
                ImGui::Checkbox(Get("component.rigidbody.is_static").c_str(), &c->isStatic);
                Tooltip(Get("editor.tooltip.component.rigidbody.is_static").c_str());
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentRigidbody>();
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Rigidbody component missing"); }
        }

        // ── Close button (only when delete was not pressed) ───────────────────
        if (!closeRequested) {
            ImGui::SameLine();
            if (ImGui::Button(Get("common.close").c_str())) CloseEditor();
        }

        if (closeRequested || !*windowOpen) {
            state.editingComponent  = false;
            state.selectedComponent = -1;
            if (open) *open = false;
        }

        ImGui::PopID();
        ImGui::End();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Runtime HUD hint overlay
    //  Call every frame from the Runtime Editor tick (not the Map Editor).
    //  Place it after all other ImGui windows are drawn.
    // ─────────────────────────────────────────────────────────────────────────
    void DrawRuntimeHUD() {
        // Pin to bottom-left corner with a small margin
        const float PAD = 10.0f;
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImVec2 workPos  = vp->WorkPos;
        ImVec2 workSize = vp->WorkSize;

        ImVec2 windowPos = ImVec2(workPos.x + PAD, workPos.y + workSize.y - PAD);
        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::SetNextWindowSize(ImVec2(0, 0)); // auto-size

        constexpr ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoDecoration   |
            ImGuiWindowFlags_NoInputs        |
            ImGuiWindowFlags_NoNav           |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoSavedSettings |
            //ImGuiWindowFlags_NoBringToDisplayFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin("##RuntimeHUD", nullptr, hudFlags)) {
            ImGui::TextDisabled("WASD");
            ImGui::SameLine(60);
            ImGui::TextUnformatted("Move");
            ImGui::TextDisabled("Space");
            ImGui::SameLine(60);
            ImGui::TextUnformatted("Up");
            ImGui::TextDisabled("CTRL");
            ImGui::SameLine(60);
            ImGui::TextUnformatted("Down");
            ImGui::TextDisabled("RMB");
            ImGui::SameLine(60);
            ImGui::TextUnformatted("Pick wall / entity / sector");
            ImGui::TextDisabled("ESC");
            ImGui::SameLine(60);
            ImGui::TextUnformatted("Close");
        }
        ImGui::End();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Focus helper
    // ─────────────────────────────────────────────────────────────────────────
    void SetImGuiFocus(const bool focus) {
        ImGuiIO &io = ImGui::GetIO();
        if (focus) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        else       io.ConfigFlags |=  ImGuiConfigFlags_NoMouse;
    }

} // namespace ImGuiDrawFunctions