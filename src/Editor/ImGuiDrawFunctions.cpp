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
#include <cfloat>
#include <cstring>
#include <string>
#include <algorithm>
#include <filesystem>
#include <Headers/Runtime/LevelSystem.hpp>

#include "EditorInternal.hpp"
#include "Headers/Editor/AssetBrowser.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/TagRegistry.hpp"
#include "Headers/GlobalEventVariables.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"
#include "Headers/Objects/FlipbookAsset.hpp"
#include "Headers/Objects/Prefab.hpp"
// #include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

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

    // Every component except Transform can be added again; each add makes a
    // new instance after the existing ones.
    template<typename T>
    static void AddEditorComponent(Entity &entity) {
        Level &level = LevelManager::CurrentLevel();

        if constexpr (std::is_same_v<T, ComponentPlayerController>) {
            const ComponentInstanceID controllerID = entity.AddComponent<ComponentPlayerController>()->instanceID;
            if (!entity.HasComponent<ComponentRigidbody>())  entity.AddComponent<ComponentRigidbody>();
            if (!entity.HasComponent<ComponentCollider>())    entity.AddComponent<ComponentCollider>();
            if (!entity.HasComponent<ComponentCamera>())      entity.AddComponent<ComponentCamera>();
            // A player added in the editor takes over, camera and all.
            level.ActivatePlayerController(*level.playerControllers.GetInstance(controllerID));
            level.ActivateCamera(*entity.GetComponent<ComponentCamera>());
        }
        else if constexpr (std::is_same_v<T, ComponentCamera>) {
            // A camera added in the editor takes over.
            level.ActivateCamera(*entity.AddComponent<ComponentCamera>());
        }
        else if constexpr (std::is_same_v<T, ComponentScript>) {
            // Deliberately unconditional: ScriptComponentStorage already
            // supports several scripts per Entity (each AddScript() call
            // gets its own fresh instance ID), so "Add Component -> Script"
            // always appends a new instance rather than being a no-op after
            // the first one - see the per-instance row list in
            // DrawEntityEditor.
            ComponentScript &script = entity.AddScript();
            script.enabled = true;
            script.fileName.clear();
            script.publicValues.clear();
            script.schemaHash = 0;
        }
        else if (!IsSingleComponent<T> || !entity.HasComponent<T>()) entity.AddComponent<T>();
    }

    // Moves a component to `newIndex` among its entity's components of the
    // same type (the inspector's drag-to-reorder). Index 0 is the "first" one
    // that entity.<component> returns in Lua.
    template<typename Storage>
    static bool MoveInStorage(Storage &storage, const std::uint64_t instanceID, const size_t newIndex) {
        return storage.MoveOnOwner(instanceID, newIndex);
    }

    static void MoveComponentInstance(const int componentType, const std::uint64_t instanceID, const size_t newIndex) {
        Level &level = LevelManager::CurrentLevel();

        switch (componentType) {
    #define MOVE_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    case Bit: MoveInStorage(level.Storage, instanceID, newIndex); break;
            TILKY_COMPONENTS(MOVE_COMPONENT_CASE)
    #undef MOVE_COMPONENT_CASE
            default: break;
        }
    }

    // The instance IDs of the entity's components of one type, in order.
    // Scripts use their ScriptInstanceIDs.
    template<typename T>
    static std::vector<std::uint64_t> ComponentInstancesOf(Entity &entity) {
        std::vector<std::uint64_t> result;
        for (const T *component : entity.GetComponents<T>()) result.push_back(component->instanceID);
        return result;
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
    // UI primitive helpers
    // ─────────────────────────────────────────────────────────────────────────

    // Reset a float3 to zero with a small inline button
    bool ResetFloat3Button(const char *id, float *v) {
        ImGui::PushID(id);
        ImGui::SameLine();
        bool pressed = ImGui::SmallButton("Reset");
        if (pressed) { v[0] = 0.0f; v[1] = 0.0f; v[2] = 0.0f; }
        ImGui::PopID();
        return pressed;
    }

    // ── Reference-field pickers (Entity / Wall / Sector / Component / Behaviour)
    //
    // Entity, Wall and Sector fields accept a drop of the matching row from
    // the Map Editor's hierarchy, and Entity fields also accept an entity
    // dragged straight off the canvas (see SubmitCanvasEntityDragSource).
    // Each keeps its combo box for picking by hand. Component and Behaviour
    // fields are combo-only. Asset fields (textures, etc.) get drag-and-drop
    // by reusing MapEditorInternal::DrawAssetField below, same as every other
    // texture/sound/script field in the inspector.

    std::string DescribeEntity(const Entity &entity) {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s (#%u)", entity.name.empty() ? "unnamed" : entity.name.c_str(), entity.id);
        return buf;
    }

    std::string DescribeWall(const Wall &wall) {
        char buf[160];
        if (wall.name.empty()) snprintf(buf, sizeof(buf), "Wall #%u", wall.id);
        else snprintf(buf, sizeof(buf), "%s (#%u)", wall.name.c_str(), wall.id);
        return buf;
    }

    std::string DescribeLinedef(const Linedef &linedef) {
        char buf[160];
        if (linedef.name.empty()) snprintf(buf, sizeof(buf), "Linedef #%u", linedef.id);
        else snprintf(buf, sizeof(buf), "%s (#%u)", linedef.name.c_str(), linedef.id);
        return buf;
    }

    std::string DescribeSector(const Sector &sector) {
        char buf[160];
        if (sector.name.empty()) snprintf(buf, sizeof(buf), "Sector #%u", sector.id);
        else snprintf(buf, sizeof(buf), "%s (#%u)", sector.name.c_str(), sector.id);
        return buf;
    }

    // Drop target for the item just drawn. A drop from the canvas also
    // undoes the move the drag made on the way here - the user was
    // assigning the entity, not relocating it.
    bool AcceptLevelObjectDrop(const char *payloadType, ID &id) {
        if (!ImGui::BeginDragDropTarget()) return false;

        bool changed = false;

        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(payloadType)) {
            MapEditorInternal::LevelObjectDragPayload dropped;
            std::memcpy(&dropped, payload->Data, sizeof(dropped));

            id = dropped.id;
            changed = true;

            if (dropped.fromCanvas) MapEditorInternal::RevertCanvasEntityDrag();
        }

        ImGui::EndDragDropTarget();
        return changed;
    }

    // Shared body of the Entity/Wall/Sector fields: label, a combo with
    // "(None)" plus whatever drawOptions lists, and the combo itself doubling
    // as the drop target for `payloadType`.
    template<typename DrawOptions>
    bool DrawLevelObjectRefField(const char *label, ID &id, const std::string &preview,
                                 const char *payloadType, const char *tooltipKey,
                                 DrawOptions &&drawOptions) {
        bool changed = false;

        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);

        if (ImGui::BeginCombo("##refPicker", preview.c_str())) {
            if (ImGui::Selectable("(None)", id == INVALID_ID)) {
                id = INVALID_ID;
                changed = true;
            }

            drawOptions(id, changed);

            ImGui::EndCombo();
        }

        changed |= AcceptLevelObjectDrop(payloadType, id);

        if (ImGui::GetDragDropPayload() == nullptr)
            ImGuiDrawFunctions::Tooltip(Localisation::Get(tooltipKey).c_str());

        ImGui::PopID();
        return changed;
    }

    bool DrawEntityField(const char *label, EntityRefValue &ref) {
        Level &level = LevelManager::CurrentLevel();

        const Entity *current = ref.entityId == INVALID_ID ? nullptr : level.GetEntity(ref.entityId);
        const std::string preview = !ref.prefab.empty() ? "Prefab: " + std::filesystem::path(ref.prefab).stem().string()
                                  : current != nullptr ? DescribeEntity(*current) : "(None)";

        // Picking a level entity (or None) replaces a prefab.
        bool changed = DrawLevelObjectRefField(
            label, ref.entityId, preview,
            MapEditorInternal::ENTITY_REF_PAYLOAD, "editor.ref_field.tooltip.entity",
            [&level](ID &id, bool &changed) {
                for (const Entity &candidate : level.entities) {
                    const bool isSelected = candidate.id == id;

                    if (ImGui::Selectable(DescribeEntity(candidate).c_str(), isSelected)) {
                        id = candidate.id;
                        changed = true;
                    }

                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }
        );
        if (changed) ref.prefab.clear();

        // A .prefab dragged from the Asset Browser onto the same combo.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(AssetBrowser::DragDropPayloadTypeFor(AssetKind::Prefab))) {
                ref.prefab = AssetBrowser::ToAssetReference(static_cast<const char *>(payload->Data), AssetKind::Prefab);
                ref.entityId = INVALID_ID;
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }

        return changed;
    }

    bool DrawWallField(const char *label, WallRefValue &ref) {
        Level &level = LevelManager::CurrentLevel();

        const auto currentIt = level.wallIDToIndex.find(ref.wallId);
        const std::string preview = currentIt != level.wallIDToIndex.end()
                                        ? DescribeWall(level.walls[currentIt->second])
                                        : "(None)";

        return DrawLevelObjectRefField(
            label, ref.wallId, preview,
            MapEditorInternal::WALL_REF_PAYLOAD, "editor.ref_field.tooltip.wall",
            [&level](ID &id, bool &changed) {
                for (const Wall &candidate : level.walls) {
                    const bool isSelected = candidate.id == id;

                    if (ImGui::Selectable(DescribeWall(candidate).c_str(), isSelected)) {
                        id = candidate.id;
                        changed = true;
                    }

                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }
        );
    }

    bool DrawSectorField(const char *label, SectorRefValue &ref) {
        Level &level = LevelManager::CurrentLevel();

        const auto currentIt = level.sectorIDToIndex.find(ref.sectorId);
        const std::string preview = currentIt != level.sectorIDToIndex.end()
                                        ? DescribeSector(level.sectors[currentIt->second])
                                        : "(None)";

        return DrawLevelObjectRefField(
            label, ref.sectorId, preview,
            MapEditorInternal::SECTOR_REF_PAYLOAD, "editor.ref_field.tooltip.sector",
            [&level](ID &id, bool &changed) {
                for (const Sector &candidate : level.sectors) {
                    const bool isSelected = candidate.id == id;

                    if (ImGui::Selectable(DescribeSector(candidate).c_str(), isSelected)) {
                        id = candidate.id;
                        changed = true;
                    }

                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }
        );
    }

    // The entity's components of one type, in order.
    std::vector<std::uint64_t> ComponentInstancesByType(Entity &entity, const int componentType) {
        switch (componentType) {
#define INSTANCES_CASE(Type, Bit, Storage, LabelKey) case Bit: return ComponentInstancesOf<Type>(entity);
            TILKY_NORMAL_COMPONENTS(INSTANCES_CASE)
#undef INSTANCES_CASE
            default: return {};
        }
    }

    // "Door (#4)", or "Door (#4) / Audio Source 2" when the entity has several.
    std::string DescribeComponent(Entity &entity, const int componentType, const std::uint64_t instanceID) {
        const std::vector<std::uint64_t> instances = ComponentInstancesByType(entity, componentType);
        if (instances.size() < 2) return DescribeEntity(entity);

        const auto it = std::ranges::find(instances, instanceID);
        if (it == instances.end()) return DescribeEntity(entity);

        return DescribeEntity(entity) + " / " + GetComponentDisplayName(componentType) + " " +
               std::to_string(it - instances.begin() + 1);
    }

    // Picks one component (entity + instance): an entity can have several of a type.
    bool DrawComponentField(const char *label, ComponentRefValue &ref, const int componentType) {
        Level &level = LevelManager::CurrentLevel();
        ref.componentType = componentType;

        Entity *current = ref.entityId == INVALID_ID ? nullptr : level.GetEntity(ref.entityId);
        const std::string preview = current != nullptr ? DescribeComponent(*current, componentType, ref.instanceId) : "(None)";

        bool changed = false;

        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);

        if (ImGui::BeginCombo("##componentPicker", preview.c_str())) {
            if (ImGui::Selectable("(None)", ref.entityId == INVALID_ID)) {
                ref.entityId = INVALID_ID;
                ref.instanceId = INVALID_COMPONENT_INSTANCE_ID;
                changed = true;
            }

            for (Entity &candidate : level.entities) {
                for (const std::uint64_t instanceID : ComponentInstancesByType(candidate, componentType)) {
                    const bool isSelected = candidate.id == ref.entityId && instanceID == ref.instanceId;

                    ImGui::PushID(std::to_string(instanceID).c_str());
                    if (ImGui::Selectable(DescribeComponent(candidate, componentType, instanceID).c_str(), isSelected)) {
                        ref.entityId = candidate.id;
                        ref.instanceId = instanceID;
                        changed = true;
                    }
                    ImGui::PopID();

                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        // An entity dragged from the hierarchy or the level picks its first
        // component of this type. Entities without one aren't accepted, so
        // the field doesn't light up for them.
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload *active = ImGui::GetDragDropPayload();

            if (active != nullptr && active->IsDataType(MapEditorInternal::ENTITY_REF_PAYLOAD)) {
                MapEditorInternal::LevelObjectDragPayload dragged;
                std::memcpy(&dragged, active->Data, sizeof(dragged));

                Entity *candidate = level.GetEntity(dragged.id);
                const std::vector<std::uint64_t> instances = candidate != nullptr
                    ? ComponentInstancesByType(*candidate, componentType)
                    : std::vector<std::uint64_t>{};

                if (!instances.empty() && ImGui::AcceptDragDropPayload(MapEditorInternal::ENTITY_REF_PAYLOAD)) {
                    ref.entityId = dragged.id;
                    ref.instanceId = instances.front();
                    changed = true;

                    if (dragged.fromCanvas) MapEditorInternal::RevertCanvasEntityDrag();
                }
            }

            ImGui::EndDragDropTarget();
        }

        if (ImGui::GetDragDropPayload() == nullptr)
            ImGuiDrawFunctions::Tooltip(Localisation::Get("editor.ref_field.tooltip.component").c_str());

        ImGui::PopID();
        return changed;
    }

    bool DrawBehaviourField(const char *label, BehaviourRefValue &ref) {
        Level &level = LevelManager::CurrentLevel();

        const Entity *currentEntity = ref.entityId == INVALID_ID ? nullptr : level.GetEntity(ref.entityId);
        const ComponentScript *currentScript = currentEntity == nullptr ? nullptr : level.scripts.GetByID(ref.instanceId);

        std::string preview = "(None)";
        if (currentEntity != nullptr && currentScript != nullptr) {
            const std::string scriptName = currentScript->fileName.empty()
                ? std::string("(unassigned)")
                : std::filesystem::path(currentScript->fileName).filename().string();
            preview = DescribeEntity(*currentEntity) + " / " + scriptName;
        }

        bool changed = false;

        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(240.0f);

        if (ImGui::BeginCombo("##behaviourPicker", preview.c_str())) {
            if (ImGui::Selectable("(None)", ref.entityId == INVALID_ID)) {
                ref = BehaviourRefValue{};
                changed = true;
            }

            for (Entity &candidate : level.entities) {
                for (ComponentScript *script : candidate.GetScripts()) {
                    const std::string scriptName = script->fileName.empty()
                        ? std::string("(unassigned)")
                        : std::filesystem::path(script->fileName).filename().string();
                    const std::string entry = DescribeEntity(candidate) + " / " + scriptName;

                    const bool isSelected = candidate.id == ref.entityId && script->instanceID == ref.instanceId;

                    if (ImGui::Selectable(entry.c_str(), isSelected)) {
                        ref.entityId = candidate.id;
                        ref.instanceId = script->instanceID;
                        changed = true;
                    }

                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::PopID();
        return changed;
    }

    // Script public-field editor. Dispatches on the field's schema type
    // (from the script's `public` declarations - see LuaScriptCompiler) to
    // the matching typed control.
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
            case ScriptValueType::Vector2: {
                Vector2 *vv = std::get_if<Vector2>(&value);
                if (!vv) return;
                ImGui::DragFloat2(field.displayName.c_str(), &vv->x, 0.1f);
                break;
            }
            case ScriptValueType::Vector3: {
                Vector3 *vv = std::get_if<Vector3>(&value);
                if (!vv) return;
                ImGui::DragFloat3(field.displayName.c_str(), &vv->x, 0.1f);
                break;
            }
            case ScriptValueType::Vector4: {
                Vector4 *vv = std::get_if<Vector4>(&value);
                if (!vv) return;
                ImGui::DragFloat4(field.displayName.c_str(), &vv->x, 0.1f);
                break;
            }
            case ScriptValueType::Enum: {
                int *iv = std::get_if<int>(&value);
                if (!iv || field.enumOptions.empty()) return;

                std::size_t currentIndex = 0;
                for (std::size_t i = 0; i < field.enumOptions.size(); ++i)
                    if (field.enumOptions[i].value == *iv) { currentIndex = i; break; }

                if (ImGui::BeginCombo(field.displayName.c_str(), field.enumOptions[currentIndex].name.c_str())) {
                    for (std::size_t i = 0; i < field.enumOptions.size(); ++i) {
                        const bool isSelected = i == currentIndex;

                        if (ImGui::Selectable(field.enumOptions[i].name.c_str(), isSelected))
                            *iv = field.enumOptions[i].value;

                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
                }

                break;
            }
            case ScriptValueType::Entity: {
                EntityRefValue *ev = std::get_if<EntityRefValue>(&value);
                if (!ev) return;
                DrawEntityField(field.displayName.c_str(), *ev);
                break;
            }
            case ScriptValueType::Component: {
                ComponentRefValue *cv = std::get_if<ComponentRefValue>(&value);
                if (!cv) return;
                DrawComponentField(field.displayName.c_str(), *cv, field.componentType);
                break;
            }
            case ScriptValueType::Behaviour: {
                BehaviourRefValue *bv = std::get_if<BehaviourRefValue>(&value);
                if (!bv) return;
                DrawBehaviourField(field.displayName.c_str(), *bv);
                break;
            }
            case ScriptValueType::Asset: {
                AssetRefValue *av = std::get_if<AssetRefValue>(&value);
                if (!av) return;
                if (field.assetKind == ScriptAssetKind::Flipbook)
                    MapEditorInternal::DrawAssetField(field.displayName.c_str(), av->path, AssetKind::Flipbook);
                else
                    MapEditorInternal::DrawAssetField(field.displayName.c_str(), av->path, AssetKind::Texture, 48.0f);
                break;
            }
            case ScriptValueType::Wall: {
                WallRefValue *wv = std::get_if<WallRefValue>(&value);
                if (!wv) return;
                DrawWallField(field.displayName.c_str(), *wv);
                break;
            }
            case ScriptValueType::Sector: {
                SectorRefValue *sv = std::get_if<SectorRefValue>(&value);
                if (!sv) return;
                DrawSectorField(field.displayName.c_str(), *sv);
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

    // Selection summary bar – compact coloured header line
    // e.g.  DrawInspectorHeader("Sector", "#3")
    void DrawInspectorHeader(const char *kind, const char *detail) {
        ImGuiStyle &style = ImGui::GetStyle();
        // Slightly tinted background strip
        ImVec2 pos  = ImGui::GetCursorScreenPos();
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() + style.FramePadding.y * 2.0f);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          ImGui::GetColorU32(ImGuiCol_Header), 4.0f);
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
    // The 8 directional texture slots, laid out as a compass for 90/45-degree
    // sprites. Shared by the Sprite inspector and the flipbook editor.
    void DrawDirectionalTextureSlots(std::array<std::string, 8>& textures, const SideCount sideCount) {
        constexpr float BOX_SIZE = 48.0f;
        // A filled slot: box, side label, Clear button. Empty cells reserve
        // the same so rows don't jump when a slot is cleared.
        const float SLOT_HEIGHT = BOX_SIZE + ImGui::GetStyle().ItemSpacing.y + 2.0f * ImGui::GetTextLineHeightWithSpacing();

        auto DrawSpriteSlot = [&](const int slotIndex, const char *label) {
            ImGui::PushID(slotIndex);
            MapEditorInternal::DrawAssetField(label, textures[slotIndex], AssetKind::Texture, BOX_SIZE);
            ImGui::PopID();
        };

        auto DrawEmptySlot = [&]() {ImGui::Dummy(ImVec2(BOX_SIZE, SLOT_HEIGHT));};

        constexpr float CELL_PADDING = 4.0f;
        constexpr float COLUMN_WIDTH = BOX_SIZE + CELL_PADDING;

        if (sideCount == SIDECOUNT_SINGLE) {
            ImGui::PushID("sprite_single");
            DrawSpriteSlot(0, "Default");
            ImGui::PopID();
        } else if (sideCount == SIDECOUNT_90) {
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
        else if (sideCount == SIDECOUNT_45) {
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
    }

    bool DangerButton(const char *label) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.08f, 0.08f, 1.0f));
        bool pressed = ImGui::Button(label);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    // (No-op closer kept for readability parity)
    void EndSection() { ImGui::Spacing(); }

    // Standard field input width – call before the next widget
    void FieldWidth(const float width) { ImGui::SetNextItemWidth(width); }

    // Section header with SeparatorText (falls back gracefully)
    void BeginSection(const char *label) {
        ImGui::Spacing();
        ImGui::SeparatorText(label);
    }

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

    // Runtime-safe inspector window flags:
    // NoNav prevents the window from stealing keyboard focus (WASD stays functional).
    static constexpr ImGuiWindowFlags kInspectorFlags =
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse;

    void PutSpace(const int n) {
        for (int i = 0; i < n; i++) ImGui::Spacing();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Inspector layout — shared by the Sector / Wall / Entity inspectors
    // ─────────────────────────────────────────────────────────────────────────
    // Fields sit in a two-column property table: labels on the left in one
    // column, every field filling the rest. Groups are collapsible sections.

    // Starts a property table; call EndProperties only when this returns true.
    static bool BeginProperties(const char *id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;

        const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.38f, 80.0f, 150.0f);
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    static void EndProperties() { ImGui::EndTable(); }

    // A new row: the label, then the value column with the next item filling it.
    static void Property(const char *label) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        // Long labels are clipped by the column; the full text on hover.
        if (ImGui::CalcTextSize(label).x > ImGui::GetColumnWidth()) Tooltip(label);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Color fields too narrow for their number inputs show the swatch only;
    // clicking it opens the picker with the numbers.
    static ImGuiColorEditFlags CompactColorFlags(const float width, const int channels) {
        return width < 52.0f * static_cast<float>(channels) ? ImGuiColorEditFlags_NoInputs : 0;
    }

    // The width left for a field followed by `buttons` small buttons on the same line.
    static float WidthBeforeButtons(std::initializer_list<const char *> buttons) {
        const ImGuiStyle &style = ImGui::GetStyle();
        float width = ImGui::GetContentRegionAvail().x;
        for (const char *button: buttons)
            width -= ImGui::CalcTextSize(button, nullptr, true).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
        return std::max(40.0f, width);
    }

    // A collapsible section. `count` >= 0 is shown after the title
    // ("Events (2)"); `id` keeps the open state while the title changes.
    static bool InspectorSection(const std::string &title, const char *id, const int count = -1,
                                 const bool defaultOpen = true) {
        std::string label = title;
        if (count >= 0) label += " (" + std::to_string(count) + ")";
        label += "###";
        label += id;

        ImGui::Spacing();
        return ImGui::CollapsingHeader(label.c_str(), defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    }

    // Orange wrapped text for problems the user should fix.
    static void WarningText(const char *text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    // The top row of an inspector: the name field filling the row, the
    // object's ID, then room for `buttonsWidth` of buttons on the right.
    static void InspectorNameRow(std::string &name, const ID id, const float buttonsWidth, const char *tooltipKey) {
        char idText[24];
        snprintf(idText, sizeof(idText), "#%u", id);

        const ImGuiStyle &style = ImGui::GetStyle();
        const float idWidth = ImGui::CalcTextSize(idText).x + style.ItemSpacing.x;

        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - idWidth - buttonsWidth));
        ImGui::InputTextWithHint("##Name", Get("inspector.name_hint").c_str(), &name);
        Tooltip(Get(tooltipKey).c_str());

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", idText);
        Tooltip(Get("editor.tooltip.inspector.id").c_str());
    }

    // The width of small header buttons with these labels, spacing included.
    static float HeaderButtonsWidth(std::initializer_list<const char *> labels) {
        const ImGuiStyle &style = ImGui::GetStyle();
        float width = 0.0f;
        for (const char *label: labels)
            width += ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
        return width;
    }

    // A small red button, for the header row and card corners.
    static bool SmallDangerButton(const char *label) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.08f, 0.08f, 1.0f));
        const bool pressed = ImGui::SmallButton(label);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    // A property table whose first row is the texture (`drawSettings` adds
    // the rest), so the texture's settings line up with every other field.
    // The thumbnail is the Texture Field Size user setting, shrunk to fit the
    // value column.
    template<typename DrawSettings>
    static void TextureBlock(const char *id, std::string &texture, const char *tooltipKey, DrawSettings &&drawSettings) {
        ImGui::PushID(id);

        if (BeginProperties("##TextureSettings")) {
            Property(Get("inspector.texture").c_str());

            const float thumbnail = std::min(MapEditorInternal::textureFieldSize, ImGui::GetContentRegionAvail().x);
            MapEditorInternal::DrawAssetField("", texture, AssetKind::Texture, thumbnail);
            Tooltip(Get(tooltipKey).c_str());

            drawSettings();
            EndProperties();
        }

        ImGui::PopID();
    }

    // Offset, scale and flips - the settings every floor, ceiling and wall
    // surface has. `prefix` picks the sector or wall translation keys.
    static void TextureTransformRows(const char *prefix, Vector2 &offset, Vector2 &scale, bool &flipX, bool &flipY,
                                     const bool draggable) {
        const std::string p = prefix;

        Property(Get("inspector.texture_offset").c_str());
        InputOrDrag2("##Offset", &offset.x, draggable);
        Tooltip(Get("editor.tooltip." + p + ".texture_offset").c_str());

        Property(Get("inspector.texture_scale").c_str());
        InputOrDrag2("##Scale", &scale.x, draggable);
        Tooltip(Get("editor.tooltip." + p + ".texture_scale").c_str());

        Property(Get("inspector.texture_flip").c_str());
        ImGui::Checkbox("X##FlipX", &flipX);
        ImGui::SameLine();
        ImGui::Checkbox("Y##FlipY", &flipY);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Tags — shared assignment editor for Sector / Wall / Entity inspectors
    // ─────────────────────────────────────────────────────────────────────────
    // Assignment only: tags are picked from TagRegistry and never created
    // here - creating, renaming or deleting a project tag happens in the
    // Project Settings Tags panel, not from an inspector.
    // Draws every currently-assigned tag (name looked up from its ID) with a
    // compact red per-row Delete (removes only this object's assignment, not
    // the tag itself) plus a dropdown at the bottom listing the project tags
    // this object doesn't have yet.
    // A removable tag chip ("Player  x"), wrapping onto the next line when the
    // row is full. `rowRight` is the screen x where the row ends.
    static bool TagChip(const std::string &name, const bool first, const float rowRight, const char *tooltipKey) {
        const std::string chip = name + "  x";

        if (!first) {
            const float chipWidth = ImGui::CalcTextSize(chip.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + chipWidth <= rowRight) ImGui::SameLine();
        }

        const bool pressed = ImGui::SmallButton(chip.c_str());
        Tooltip(Get(tooltipKey).c_str());
        return pressed;
    }

    static void DrawTagsEditor(std::vector<uint16_t> &tagIds) {
        int tagIndexToRemove = -1;
        const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

        for (size_t tagIndex = 0; tagIndex < tagIds.size(); ++tagIndex) {
            ImGui::PushID(static_cast<int>(tagIndex));

            const auto name = TagRegistry::GetName(tagIds[tagIndex]);
            if (TagChip(name ? *name : Get("tags.missing"), tagIndex == 0, rowRight, "editor.tooltip.tag_delete"))
                tagIndexToRemove = static_cast<int>(tagIndex);

            ImGui::PopID();
        }

        if (tagIndexToRemove >= 0) tagIds.erase(tagIds.begin() + tagIndexToRemove);

        // Project tags not assigned yet, in ID (creation) order like the
        // Project Settings list.
        std::vector<std::pair<std::string, TagId> > availableTags;
        for (const auto &[name, id]: TagRegistry::GetAll())
            if (std::ranges::find(tagIds, id) == tagIds.end()) availableTags.emplace_back(name, id);
        std::ranges::sort(availableTags, {}, [](const auto &entry) { return entry.second; });

        // Always-available dropdown for assigning another tag.
        FieldWidth(-FLT_MIN);
        ImGui::BeginDisabled(availableTags.empty());

        const std::string preview = Get(availableTags.empty() ? "tags.none_available" : "tags.add_placeholder");
        if (ImGui::BeginCombo("##NewTag", preview.c_str())) {
            for (const auto &[name, id]: availableTags) {
                if (ImGui::Selectable(name.c_str())) tagIds.push_back(id);
            }

            ImGui::EndCombo();
        }

        ImGui::EndDisabled();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Events / Dispatches — shared by the Sector / Wall / Entity inspectors
    // ─────────────────────────────────────────────────────────────────────────
    // An event reaches every dispatch with the same tag. Both tags and the
    // payloads' tag filters are picked from the project tags.

    // A dropdown of the project tags. A tag that no longer exists shows as
    // deleted (or as "no tags" while the project has none).
    static bool DrawTagCombo(const char *label, TagId &tag) {
        std::vector<std::pair<std::string, TagId> > tags(TagRegistry::GetAll().begin(), TagRegistry::GetAll().end());
        std::ranges::sort(tags, {}, [](const auto &entry) { return entry.second; });

        const auto name = TagRegistry::GetName(tag);
        const std::string preview = name ? *name : Get(tags.empty() ? "events.no_tags" : "tags.missing");

        bool changed = false;
        if (ImGui::BeginCombo(label, preview.c_str())) {
            for (const auto &[tagName, id]: tags) {
                const bool selected = id == tag;
                if (ImGui::Selectable(tagName.c_str(), selected)) {
                    tag = id;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }

            if (tags.empty()) ImGui::TextDisabled("%s", Get("tags.none_available").c_str());
            ImGui::EndCombo();
        }

        return changed;
    }

    // A dropdown over {value, translation key} options.
    template<typename Enum, size_t N>
    static bool DrawEnumCombo(const char *label, Enum &value, const std::pair<Enum, const char *> (&options)[N]) {
        const char *previewKey = options[0].second;
        for (const auto &[optionValue, key]: options)
            if (optionValue == value) previewKey = key;

        bool changed = false;
        if (ImGui::BeginCombo(label, Get(previewKey).c_str())) {
            for (const auto &[optionValue, key]: options) {
                const bool selected = optionValue == value;
                if (ImGui::Selectable(Get(key).c_str(), selected)) {
                    value = optionValue;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    constexpr std::pair<TagType, const char *> kTagTypeOptions[] = {
        {TagType::INCLUDE, "event.payload.tag_type.include"},
        {TagType::EXCLUDE, "event.payload.tag_type.exclude"},
        {TagType::ALL, "event.payload.tag_type.all"}
    };

    constexpr std::pair<PayloadLineEntityCross::EntityCrossDirection, const char *> kCrossDirectionOptions[] = {
        {PayloadLineEntityCross::EITHER, "events.direction.either"},
        {PayloadLineEntityCross::FORWARD, "events.direction.forward"},
        {PayloadLineEntityCross::BACKWARD, "events.direction.backward"}
    };

    constexpr std::pair<SectorCollision, const char *> kSectorCollisionOptions[] = {
        {SECTOR_FORCE_MOVE, "events.collision.force_move"},
        {SECTOR_STOP, "events.collision.stop"},
        {SECTOR_RETRACT, "events.collision.retract"},
        {SECTOR_DONT_MOVE, "events.collision.dont_move"}
    };

    constexpr std::pair<Condition::ConditionType, const char *> kConditionTypeOptions[] = {
        {Condition::NONE, "event.condition.none"},
        {Condition::EQUAL, "event.condition.equal"},
        {Condition::LESS_THAN, "event.condition.less_than"},
        {Condition::GREATER_THAN, "event.condition.greater_than"},
        {Condition::LESS_THAN_OR_EQUAL, "event.condition.less_than_or_equal"},
        {Condition::GREATER_THAN_OR_EQUAL, "event.condition.greater_than_or_equal"}
    };

    constexpr std::pair<PayloadDispatchChangeVariable::Operation, const char *> kVariableOperationOptions[] = {
        {PayloadDispatchChangeVariable::SET, "events.operation.set"},
        {PayloadDispatchChangeVariable::ADD, "events.operation.add"},
        {PayloadDispatchChangeVariable::SUBTRACT, "events.operation.subtract"}
    };

    constexpr std::pair<LineSide, const char *> kLineSideOptions[] = {
        {LineSide::EITHER, "events.side.either"},
        {LineSide::FRONT, "events.side.front"},
        {LineSide::BACK, "events.side.back"}
    };

    constexpr std::pair<RayType, const char *> kRayTypeOptions[] = {
        {RayType::INTERACT, "events.ray_type.interact"},
        {RayType::LINE_OF_SIGHT, "events.ray_type.line_of_sight"},
        {RayType::BULLET, "events.ray_type.bullet"},
        {RayType::CUSTOM, "events.ray_type.custom"}
    };

    // The PlayerController can also cast General rays (which fire nothing).
    constexpr std::pair<RayType, const char *> kRayTypeOptionsWithGeneral[] = {
        {RayType::GENERAL, "events.ray_type.general"},
        {RayType::INTERACT, "events.ray_type.interact"},
        {RayType::LINE_OF_SIGHT, "events.ray_type.line_of_sight"},
        {RayType::BULLET, "events.ray_type.bullet"},
        {RayType::CUSTOM, "events.ray_type.custom"}
    };

    constexpr std::pair<SectorSurfacePick, const char *> kSurfaceOptions[] = {
        {SectorSurfacePick::FLOOR, "events.surface.floor"},
        {SectorSurfacePick::CEILING, "events.surface.ceiling"},
        {SectorSurfacePick::BOTH, "events.surface.both"}
    };

    // Sector Move Finished: BOTH means either surface.
    constexpr std::pair<SectorSurfacePick, const char *> kSurfaceFilterOptions[] = {
        {SectorSurfacePick::BOTH, "events.surface.either"},
        {SectorSurfacePick::FLOOR, "events.surface.floor"},
        {SectorSurfacePick::CEILING, "events.surface.ceiling"}
    };

    constexpr std::pair<PayloadSectorMoveFinished::Reason, const char *> kMoveEndReasonOptions[] = {
        {PayloadSectorMoveFinished::ANY, "events.move_end.any"},
        {PayloadSectorMoveFinished::REACHED, "events.move_end.reached"},
        {PayloadSectorMoveFinished::STOPPED, "events.move_end.stopped"},
        {PayloadSectorMoveFinished::RETRACTED, "events.move_end.retracted"}
    };

    constexpr std::pair<ToggleMode, const char *> kToggleModeOptions[] = {
        {ToggleMode::ON, "events.toggle.on"},
        {ToggleMode::OFF, "events.toggle.off"},
        {ToggleMode::TOGGLE, "events.toggle.toggle"}
    };

    constexpr std::pair<WallPiece, const char *> kWallPieceOptions[] = {
        {WallPiece::TOP, "events.wall_piece.top"},
        {WallPiece::MIDDLE, "events.wall_piece.middle"},
        {WallPiece::BOTTOM, "events.wall_piece.bottom"}
    };

    constexpr std::pair<SectorHeightMode, const char *> kHeightModeOptions[] = {
        {SectorHeightMode::ABSOLUTE_HEIGHT, "events.height_mode.absolute"},
        {SectorHeightMode::RELATIVE_OFFSET, "events.height_mode.relative"},
        {SectorHeightMode::LOWEST_NEIGHBOR_FLOOR, "events.height_mode.lowest_neighbor_floor"},
        {SectorHeightMode::HIGHEST_NEIGHBOR_FLOOR, "events.height_mode.highest_neighbor_floor"},
        {SectorHeightMode::LOWEST_NEIGHBOR_CEILING, "events.height_mode.lowest_neighbor_ceiling"},
        {SectorHeightMode::HIGHEST_NEIGHBOR_CEILING, "events.height_mode.highest_neighbor_ceiling"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::OpenMode, const char *> kDoorOpenModeOptions[] = {
        {PayloadDispatchSectorDoor::LOWEST_NEIGHBOR_CEILING, "events.door.open_mode.lowest_neighbor_ceiling"},
        {PayloadDispatchSectorDoor::FIXED_HEIGHT, "events.door.open_mode.fixed_height"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::Retrigger, const char *> kDoorRetriggerOptions[] = {
        {PayloadDispatchSectorDoor::RESTART, "events.door.retrigger.restart"},
        {PayloadDispatchSectorDoor::TOGGLE, "events.door.retrigger.toggle"},
        {PayloadDispatchSectorDoor::IGNORE_WHILE_OPEN, "events.door.retrigger.ignore"}
    };

    constexpr std::pair<ToggleableComponent, const char *> kToggleableComponentOptions[] = {
        {ToggleableComponent::COLLIDER, "component.collider"},
        {ToggleableComponent::SPRITE, "component.sprite"},
        {ToggleableComponent::SCRIPT, "component.script"},
        {ToggleableComponent::CAMERA, "component.camera"},
        {ToggleableComponent::PLAYER_CONTROLLER, "component.player_controller"},
        {ToggleableComponent::UI_SPRITE, "component.ui_sprite"}
    };

    constexpr std::pair<bool, const char *> kTagOperationOptions[] = {
        {false, "events.tag_operation.add"},
        {true, "events.tag_operation.remove"}
    };

    constexpr std::pair<bool, const char *> kVelocityModeOptions[] = {
        {false, "events.velocity_mode.add"},
        {true, "events.velocity_mode.set"}
    };

    constexpr std::pair<bool, const char *> kMoveTimingOptions[] = {
        {false, "events.move_timing.speed"},
        {true, "events.move_timing.duration"}
    };

    constexpr std::pair<bool, const char *> kSurfaceSingleOptions[] = {
        {false, "events.surface.floor"},
        {true, "events.surface.ceiling"}
    };

    // What a variable reference shows: its name (global ones marked), or
    // none / missing.
    static std::string EventVariableLabel(const Level &level, const EventVariableID id) {
        if (id == 0) return Get("events.variable.none");

        const auto name = level.GetEventVariableName(id);
        if (!name) return Get("events.variable.missing");

        return IsGlobalEventVariable(id) ? *name + "  (" + Get("events.variable.global") + ")" : *name;
    }

    // A dropdown of the current level's variables, then the project's global
    // ones, each in ID order.
    static bool DrawEventVariableCombo(const char *label, EventVariableID &id, const float width) {
        const Level &level = LevelManager::CurrentLevel();
        bool changed = false;

        FieldWidth(width);
        if (!ImGui::BeginCombo(label, EventVariableLabel(level, id).c_str())) return false;

        std::vector<std::pair<EventVariableID, std::string> > levelVariables(
            level.worldSettings.eventVariableNames.begin(), level.worldSettings.eventVariableNames.end());
        std::ranges::sort(levelVariables, {}, [](const auto &entry) { return entry.first; });

        std::vector<std::pair<EventVariableID, std::string> > globalVariables;
        for (const auto &[globalID, definition]: GlobalEventVariables::GetAll())
            globalVariables.emplace_back(globalID, definition.name);
        std::ranges::sort(globalVariables, {}, [](const auto &entry) { return entry.first; });

        const auto drawGroup = [&](const char *headerKey, const auto &variables) {
            if (variables.empty()) return;

            ImGui::SeparatorText(Get(headerKey).c_str());
            for (const auto &[variableID, name]: variables) {
                ImGui::PushID(static_cast<int>(variableID));
                const bool selected = variableID == id;
                if (ImGui::Selectable(name.c_str(), selected)) {
                    id = variableID;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        };

        drawGroup("events.variable.level_header", levelVariables);
        drawGroup("events.variable.global_header", globalVariables);

        if (levelVariables.empty() && globalVariables.empty())
            ImGui::TextDisabled("%s", Get("events.variable.none_available").c_str());

        ImGui::EndCombo();
        return changed;
    }

    // Property rows: the comparison, then (unless None) the variable on the
    // left and what it is compared to - a number, or another variable.
    static void DrawConditionFields(Condition &condition) {
        Property(Get("events.condition").c_str());
        DrawEnumCombo("##ConditionType", condition.type, kConditionTypeOptions);
        Tooltip(Get("editor.tooltip.events.condition").c_str());

        if (condition.type == Condition::NONE) return;

        ImGui::PushID("Condition");

        // The left side is always a variable.
        if (!std::holds_alternative<EventVariableID>(condition.left)) condition.left = EventVariableID{0};

        Property(Get("events.condition.left").c_str());
        DrawEventVariableCombo("##Left", std::get<EventVariableID>(condition.left), -FLT_MIN);

        const bool rightIsNumber = std::holds_alternative<float>(condition.right);
        const std::string toggleLabel = Get(rightIsNumber ? "events.condition.to_variable" : "events.condition.to_number");

        Property(Get("events.condition.right").c_str());
        const float rightWidth = WidthBeforeButtons({toggleLabel.c_str()});

        if (rightIsNumber) {
            FieldWidth(rightWidth);
            ImGui::InputFloat("##Right", &std::get<float>(condition.right), 0.0f, 0.0f, "%.2f");
        }
        else DrawEventVariableCombo("##Right", std::get<EventVariableID>(condition.right), rightWidth);

        ImGui::SameLine();
        if (ImGui::Button(toggleLabel.c_str())) {
            if (rightIsNumber) condition.right = EventVariableID{0};
            else condition.right = 0.0f;
        }
        Tooltip(Get(rightIsNumber ? "editor.tooltip.events.condition_to_variable"
                                  : "editor.tooltip.events.condition_to_number").c_str());

        ImGui::PopID();
    }

    // Calls f(std::type_identity<Payload>{}) for every alternative of the variant.
    template<typename Variant, typename F>
    static void ForEachPayloadType(F &&f) {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (f(std::type_identity<std::variant_alternative_t<I, Variant> >{}), ...);
        }(std::make_index_sequence<std::variant_size_v<Variant> >{});
    }

    template<typename Payload>
    static const TagFilter *FilterOf(const Payload &payload) {
        if constexpr (requires { payload.filter; }) return &payload.filter;
        else return nullptr;
    }

    // The types an object of `kind` can have. Picking another type resets the
    // fields, except a tag filter both types have.
    template<typename Variant>
    static void DrawPayloadTypeCombo(Variant &payload, const EventOwnerKind kind) {
        if (!ImGui::BeginCombo("##Type", Get(GetTranslationKey(payload)).c_str())) return;

        ForEachPayloadType<Variant>([&]<typename Payload>(std::type_identity<Payload>) {
            if ((Payload::owners & OwnerBit(kind)) == 0) return;

            const bool selected = std::holds_alternative<Payload>(payload);
            if (ImGui::Selectable(Get(Payload::translationKey).c_str(), selected) && !selected) {
                const TagFilter *oldFilter = std::visit([](const auto &p) { return FilterOf(p); }, payload);
                const std::optional<TagFilter> keptFilter = oldFilter ? std::optional(*oldFilter) : std::nullopt;

                Payload replacement{};
                if constexpr (requires { replacement.filter; })
                    if (keptFilter) replacement.filter = *keptFilter;

                payload = std::move(replacement);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        });

        ImGui::EndCombo();
    }

    // "Add event..." / "Add dispatch..." listing the types valid for `kind`.
    template<typename Entry>
    static void DrawAddEntryCombo(const char *placeholderKey, std::vector<Entry> &entries, const EventOwnerKind kind) {
        FieldWidth(ImGui::GetContentRegionAvail().x);
        if (!ImGui::BeginCombo("##Add", Get(placeholderKey).c_str())) return;

        using Variant = decltype(Entry::payload);
        ForEachPayloadType<Variant>([&]<typename Payload>(std::type_identity<Payload>) {
            if ((Payload::owners & OwnerBit(kind)) == 0) return;
            if (!ImGui::Selectable(Get(Payload::translationKey).c_str())) return;

            Entry entry;
            entry.payload = Payload{};

            // The first project tag, so a new entry starts on a real tag.
            std::optional<TagId> firstTag;
            for (const auto &[name, id]: TagRegistry::GetAll())
                if (!firstTag || id < *firstTag) firstTag = id;
            if (firstTag) entry.tagId = *firstTag;

            entries.push_back(std::move(entry));
        });

        ImGui::EndCombo();
    }

    // Property rows: the filter type, then the filter's tags as removable
    // chips with a dropdown of the project tags it doesn't have yet.
    static void DrawTagFilterFields(TagFilter &filter) {
        Property(Get("event.payload.tag_type").c_str());
        DrawEnumCombo("##TagType", filter.tagType, kTagTypeOptions);
        Tooltip(Get("editor.tooltip.events.filter").c_str());

        Property(Get("events.filter_tags").c_str());
        ImGui::BeginDisabled(filter.tagType == TagType::ALL);
        ImGui::PushID("FilterTags");

        int tagToRemove = -1;
        const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

        for (size_t i = 0; i < filter.tags.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));

            const auto name = TagRegistry::GetName(filter.tags[i]);
            if (TagChip(name ? *name : Get("tags.missing"), i == 0, rowRight, "editor.tooltip.events.filter_tag_remove"))
                tagToRemove = static_cast<int>(i);

            ImGui::PopID();
        }

        if (tagToRemove >= 0) filter.tags.erase(filter.tags.begin() + tagToRemove);

        std::vector<std::pair<std::string, TagId> > availableTags;
        for (const auto &[name, id]: TagRegistry::GetAll())
            if (std::ranges::find(filter.tags, id) == filter.tags.end()) availableTags.emplace_back(name, id);
        std::ranges::sort(availableTags, {}, [](const auto &entry) { return entry.second; });

        FieldWidth(-FLT_MIN);
        const char *previewKey = availableTags.empty() ? "tags.none_available" : "events.filter_tag_add";
        if (ImGui::BeginCombo("##AddFilterTag", Get(previewKey).c_str())) {
            for (const auto &[name, id]: availableTags)
                if (ImGui::Selectable(name.c_str())) filter.tags.push_back(id);

            if (availableTags.empty()) ImGui::TextDisabled("%s", Get("tags.none_available").c_str());
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.events.filter_tags").c_str());

        ImGui::PopID();
        ImGui::EndDisabled();
    }

    // The gap between the fields every entry has and its type's own fields.
    static void EntrySettingsGap() {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }

    // Starts an entry's card: a framed child that grows to fit its contents.
    static bool BeginEntryCard(const char *id) {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));

        const bool visible = ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                                               ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return visible;
    }

    static void EndEntryCard() {
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }

    // The card's collapsible header with a small Delete at its right end,
    // usable while collapsed too. Returns whether the card is open.
    static bool EntryCardHeader(const std::string &header, bool &deleteClicked) {
        const ImGuiStyle &style = ImGui::GetStyle();
        const std::string deleteLabel = Get("common.delete");
        const float deleteWidth = ImGui::CalcTextSize(deleteLabel.c_str()).x + style.FramePadding.x * 2.0f;
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        const bool open = ImGui::CollapsingHeader(header.c_str(),
                                                  ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine(right - deleteWidth - style.FramePadding.x);
        if (SmallDangerButton(deleteLabel.c_str())) deleteClicked = true;

        return open;
    }

    static std::string EntryHeader(const char *translationKey, const TagId tag, const char *idSuffix) {
        const auto tagName = TagRegistry::GetName(tag);
        return Get(translationKey) + "  ->  " + (tagName ? *tagName : Get("tags.missing")) + idSuffix;
    }

    // Property row: which of the sector's floors, numbered from 1 as in the
    // sector inspector. `allowAny` adds "Any floor" for -1.
    static void DrawFloorIndexRow(int &floorIndex, const ID sectorID, const bool allowAny) {
        const Sector *sector = LevelManager::CurrentLevel().GetSector(sectorID);
        const int floorCount = sector ? static_cast<int>(sector->floors.size()) : 0;

        const auto floorLabel = [](const int index) {
            return index < 0 ? Get("events.floor_any") : Get("events.floor") + " " + std::to_string(index + 1);
        };

        Property(Get("events.floor").c_str());
        if (ImGui::BeginCombo("##Floor", floorLabel(floorIndex).c_str())) {
            if (allowAny && ImGui::Selectable(floorLabel(-1).c_str(), floorIndex < 0)) floorIndex = -1;

            for (int floor = 0; floor < floorCount; ++floor)
                if (ImGui::Selectable(floorLabel(floor).c_str(), floor == floorIndex)) floorIndex = floor;
            ImGui::EndCombo();
        }
    }

    // Property row: an asset reference picked or dropped from the asset browser.
    static void DrawAssetRow(const char *labelKey, std::string &value, const AssetKind kind, const char *tooltipKey) {
        Property(Get(labelKey).c_str());
        ImGui::PushID(labelKey);
        MapEditorInternal::DrawAssetField("", value, kind);
        ImGui::PopID();
        Tooltip(Get(tooltipKey).c_str());
    }

    // Property row: one of the project's levels, by name.
    static void DrawLevelNameRow(std::string &levelName) {
        Property(Get("events.level").c_str());

        const std::string preview = levelName.empty() ? Get("events.level_none") : levelName;
        if (ImGui::BeginCombo("##Level", preview.c_str())) {
            std::vector<std::string> names;
            for (const std::filesystem::path &file: LevelSerialization::ListLevelFiles())
                names.push_back(file.stem().string());
            std::ranges::sort(names);
            names.erase(std::unique(names.begin(), names.end()), names.end());

            for (const std::string &name: names)
                if (ImGui::Selectable(name.c_str(), name == levelName)) levelName = name;

            if (names.empty()) ImGui::TextDisabled("%s", Get("events.level_none_available").c_str());
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.dispatches.level").c_str());
    }

    static void DrawEventsEditor(std::vector<Event> &events, const EventOwnerKind kind, const ID ownerID) {
        int eventToRemove = -1;

        for (size_t i = 0; i < events.size(); ++i) {
            Event &event = events[i];
            ImGui::PushID(static_cast<int>(i));

            // "###event" keeps the header's open state while its text changes.
            const std::string header = EntryHeader(GetTranslationKey(event.payload), event.tagId, "###event");

            bool deleteClicked = false;
            if (BeginEntryCard("##EventCard") && EntryCardHeader(header, deleteClicked)) {
                // What every event has...
                if (BeginProperties("##Common")) {
                    Property(Get("events.type").c_str());
                    DrawPayloadTypeCombo(event.payload, kind);

                    Property(Get("events.link_tag").c_str());
                    DrawTagCombo("##LinkTag", event.tagId);
                    Tooltip(Get("editor.tooltip.events.link_tag").c_str());

                    int maxTriggers = static_cast<int>(event.maxTriggers);
                    Property(Get("events.max_triggers").c_str());
                    if (ImGui::InputInt("##MaxTriggers", &maxTriggers))
                        event.maxTriggers = static_cast<uint32_t>(std::max(0, maxTriggers));
                    Tooltip(Get("editor.tooltip.events.max_triggers").c_str());

                    Property(Get("events.chance").c_str());
                    ImGui::SliderFloat("##Chance", &event.chance, 0.0f, 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
                    Tooltip(Get("editor.tooltip.events.chance").c_str());

                    DrawConditionFields(event.condition);

                    EndProperties();
                }

                // ...then what only this type has.
                const bool hasSettings = std::visit([]<typename Payload>(const Payload &) {
                    return !std::is_same_v<Payload, PayloadEntityEnable> &&
                           !std::is_same_v<Payload, PayloadEntityDisable> &&
                           !std::is_same_v<Payload, PayloadEntityDestroyed> &&
                           !std::is_same_v<Payload, PayloadConditionMet> &&
                           !std::is_same_v<Payload, PayloadLevelStart>;
                }, event.payload);

                if (hasSettings) {
                    EntrySettingsGap();

                    if (BeginProperties("##Settings")) {
                        std::visit([&]<typename Payload>(Payload &payload) {
                            if constexpr (requires { payload.filter; }) DrawTagFilterFields(payload.filter);

                            if constexpr (std::is_same_v<Payload, PayloadLineEntityCross>) {
                                Property(Get("events.direction").c_str());
                                DrawEnumCombo("##Direction", payload.direction, kCrossDirectionOptions);
                                Tooltip(Get("editor.tooltip.events.direction").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadLineEntityCollide>) {
                                Property(Get("events.side").c_str());
                                DrawEnumCombo("##Side", payload.side, kLineSideOptions);
                                Tooltip(Get("editor.tooltip.events.collide_side").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadSectorEntityStay>) {
                                Property(Get("events.interval").c_str());
                                if (ImGui::InputFloat("##Interval", &payload.interval, 0.0f, 0.0f, "%.2f s"))
                                    payload.interval = std::max(0.01f, payload.interval);
                                Tooltip(Get("editor.tooltip.events.stay_interval").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadSectorMoveFinished>) {
                                Property(Get("events.surface").c_str());
                                DrawEnumCombo("##Surface", payload.surface, kSurfaceFilterOptions);

                                DrawFloorIndexRow(payload.floorIndex, ownerID, true);

                                Property(Get("events.move_end").c_str());
                                DrawEnumCombo("##Reason", payload.reason, kMoveEndReasonOptions);
                                Tooltip(Get("editor.tooltip.events.move_end").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadReceiveRay>) {
                                Property(Get("events.ray_type").c_str());
                                DrawEnumCombo("##RayType", payload.rayType, kRayTypeOptions);
                                Tooltip(Get("editor.tooltip.events.ray_type").c_str());

                                Property(Get("events.first_hit_only").c_str());
                                ImGui::Checkbox("##FirstHitOnly", &payload.firstHitOnly);
                                Tooltip(Get("editor.tooltip.events.first_hit_only").c_str());

                                if (kind == EventOwnerKind::LINE || kind == EventOwnerKind::LINEDEF) {
                                    Property(Get("events.side").c_str());
                                    DrawEnumCombo("##Side", payload.side, kLineSideOptions);
                                    Tooltip(Get("editor.tooltip.events.ray_side").c_str());
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadTimer>) {
                                Property(Get("events.interval").c_str());
                                if (ImGui::InputFloat("##Interval", &payload.interval, 0.0f, 0.0f, "%.2f s"))
                                    payload.interval = std::max(0.01f, payload.interval);
                                Tooltip(Get("editor.tooltip.events.timer_interval").c_str());

                                Property(Get("events.first_delay").c_str());
                                if (ImGui::InputFloat("##FirstDelay", &payload.firstDelay, 0.0f, 0.0f, "%.2f s"))
                                    payload.firstDelay = std::max(0.0f, payload.firstDelay);
                                Tooltip(Get("editor.tooltip.events.first_delay").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadLuaSignal>) {
                                Property(Get("events.signal_name").c_str());
                                ImGui::InputText("##SignalName", &payload.name);
                                Tooltip(Get("editor.tooltip.events.signal_name").c_str());
                            }
                        }, event.payload);

                        EndProperties();
                    }
                }
            }
            EndEntryCard();

            if (deleteClicked) eventToRemove = static_cast<int>(i);

            ImGui::PopID();
        }

        if (eventToRemove >= 0) events.erase(events.begin() + eventToRemove);

        DrawAddEntryCombo("events.add_placeholder", events, kind);
    }

    static void DrawDispatchesEditor(std::vector<Dispatch> &dispatches, const EventOwnerKind kind, const ID ownerID,
                                     const bool draggable) {
        Level &level = LevelManager::CurrentLevel();
        int dispatchToRemove = -1;

        for (size_t i = 0; i < dispatches.size(); ++i) {
            Dispatch &dispatch = dispatches[i];
            ImGui::PushID(static_cast<int>(i));

            const std::string header = EntryHeader(GetTranslationKey(dispatch.payload), dispatch.tagId, "###dispatch");

            bool deleteClicked = false;
            if (BeginEntryCard("##DispatchCard") && EntryCardHeader(header, deleteClicked)) {
                // What every dispatch has...
                if (BeginProperties("##Common")) {
                    Property(Get("events.type").c_str());
                    DrawPayloadTypeCombo(dispatch.payload, kind);

                    Property(Get("events.link_tag").c_str());
                    DrawTagCombo("##LinkTag", dispatch.tagId);
                    Tooltip(Get("editor.tooltip.events.link_tag").c_str());

                    Property(Get("events.delay").c_str());
                    if (ImGui::InputFloat("##Delay", &dispatch.delay, 0.0f, 0.0f, "%.2f s"))
                        dispatch.delay = std::max(0.0f, dispatch.delay);
                    Tooltip(Get("editor.tooltip.dispatches.delay").c_str());

                    Property(Get("events.cooldown").c_str());
                    if (ImGui::InputFloat("##Cooldown", &dispatch.cooldown, 0.0f, 0.0f, "%.2f s"))
                        dispatch.cooldown = std::max(0.0f, dispatch.cooldown);
                    Tooltip(Get("editor.tooltip.dispatches.cooldown").c_str());

                    Property(Get("events.cooldown_triggers").c_str());
                    ImGui::BeginDisabled(dispatch.cooldown <= 0.0f);
                    if (ImGui::InputInt("##CooldownTriggers", &dispatch.cooldownTriggers))
                        dispatch.cooldownTriggers = std::max(1, dispatch.cooldownTriggers);
                    ImGui::EndDisabled();
                    Tooltip(Get("editor.tooltip.dispatches.cooldown_triggers").c_str());

                    EndProperties();
                }

                // ...then what only this type has.
                const bool hasSettings = std::visit([]<typename Payload>(const Payload &) {
                    return !std::is_same_v<Payload, PayloadDispatchEntityDestroy> &&
                           !std::is_same_v<Payload, PayloadDispatchEntityDisable> &&
                           !std::is_same_v<Payload, PayloadDispatchEntityEnable>;
                }, dispatch.payload);

                if (hasSettings) {
                    EntrySettingsGap();

                    if (BeginProperties("##Settings")) {
                        std::visit([&]<typename Payload>(Payload &payload) {
                            if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetColor>) {
                                Property(Get("events.color").c_str());
                                ImGui::ColorEdit3("##Color", &payload.color.x,
                                                  CompactColorFlags(ImGui::GetContentRegionAvail().x, 3));

                                Property(Get("events.speed").c_str());
                                if (InputOrDrag("##Speed", &payload.speed, draggable, 0.05f))
                                    payload.speed = std::max(0.0f, payload.speed);
                                Tooltip(Get("editor.tooltip.dispatches.color_speed").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorMoveFloorToCeiling> ||
                                               std::is_same_v<Payload, PayloadDispatchSectorMoveCeilingToFloor>) {
                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                Property(Get("events.gap").c_str());
                                if (InputOrDrag("##Gap", &payload.gap, draggable, 0.1f))
                                    payload.gap = std::max(0.0f, payload.gap);
                                Tooltip(Get("editor.tooltip.dispatches.gap").c_str());

                                Property(Get("events.speed").c_str());
                                if (InputOrDrag("##Speed", &payload.speed, draggable, 0.1f))
                                    payload.speed = std::max(0.01f, payload.speed);
                                Tooltip(Get("editor.tooltip.dispatches.move_speed").c_str());

                                Property(Get("events.collision").c_str());
                                DrawEnumCombo("##Collision", payload.collisionType, kSectorCollisionOptions);
                                Tooltip(Get("editor.tooltip.dispatches.collision").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityTeleport>) {
                                Property(Get("events.position").c_str());
                                InputOrDrag3("##Position", &payload.position.x, draggable);

                                // Euler angles kept between frames, re-read when the
                                // quaternion was changed elsewhere (as in the
                                // Transform inspector), so dragging doesn't jump.
                                static std::pair<ID, size_t> eulerOwner = {INVALID_ID, 0};
                                static Vector3 euler = {0.0f, 0.0f, 0.0f};

                                const Quaternion shown = Quaternion::FromEulerDegrees(euler.x, euler.y, euler.z);
                                const Quaternion actual = payload.rotation.Normalized();
                                const float dot = shown.x * actual.x + shown.y * actual.y + shown.z * actual.z + shown.w * actual.w;

                                if (eulerOwner != std::pair{ownerID, i} || std::abs(dot) < 0.99999f) {
                                    eulerOwner = {ownerID, i};
                                    euler = actual.ToEulerDegrees();
                                }

                                Property(Get("events.rotation").c_str());
                                if (InputOrDrag3("##Rotation", &euler.x, draggable))
                                    payload.rotation = Quaternion::FromEulerDegrees(euler.x, euler.y, euler.z);

                                const ComponentTransform *transform = level.transforms.Get(ownerID);
                                Property("");
                                ImGui::BeginDisabled(transform == nullptr);
                                if (ImGui::Button(Get("events.use_entity").c_str(), ImVec2(-FLT_MIN, 0.0f)) && transform != nullptr) {
                                    payload.position = transform->position;
                                    payload.rotation = transform->rotation.Normalized();
                                }
                                ImGui::EndDisabled();
                                Tooltip(Get("editor.tooltip.dispatches.use_entity").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityCallFunction>) {
                                Property(Get("events.function_name").c_str());
                                ImGui::InputText("##Function", &payload.functionName);
                                Tooltip(Get("editor.tooltip.dispatches.function_name").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchChangeVariable>) {
                                Property(Get("events.variable").c_str());
                                DrawEventVariableCombo("##Variable", payload.variable, -FLT_MIN);
                                Tooltip(Get("editor.tooltip.dispatches.variable").c_str());

                                Property(Get("events.operation").c_str());
                                DrawEnumCombo("##Operation", payload.operation, kVariableOperationOptions);
                                Tooltip(Get("editor.tooltip.dispatches.operation").c_str());

                                Property(Get("events.value").c_str());
                                InputOrDrag("##Value", &payload.value, draggable, 0.1f);
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetTexture>) {
                                Property(Get("events.wall_piece").c_str());
                                DrawEnumCombo("##Piece", payload.piece, kWallPieceOptions);

                                DrawAssetRow("events.texture", payload.texture, AssetKind::Texture, "editor.tooltip.dispatches.line_texture");
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchLineSetCollision>) {
                                Property(Get("events.toggle").c_str());
                                DrawEnumCombo("##Mode", payload.mode, kToggleModeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.line_collision").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchLinedefSetEnabled>) {
                                Property(Get("events.toggle").c_str());
                                DrawEnumCombo("##Mode", payload.mode, kToggleModeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.linedef_enabled").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorMoveToHeight>) {
                                Property(Get("events.surface").c_str());
                                DrawEnumCombo("##Surface", payload.ceiling, kSurfaceSingleOptions);

                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                Property(Get("events.height_mode").c_str());
                                DrawEnumCombo("##Mode", payload.mode, kHeightModeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.height_mode").c_str());

                                Property(Get(payload.mode == SectorHeightMode::ABSOLUTE_HEIGHT ? "events.height" : "events.offset").c_str());
                                InputOrDrag("##Height", &payload.height, draggable, 0.1f);

                                Property(Get("events.speed").c_str());
                                if (InputOrDrag("##Speed", &payload.speed, draggable, 0.1f))
                                    payload.speed = std::max(0.01f, payload.speed);
                                Tooltip(Get("editor.tooltip.dispatches.move_speed").c_str());

                                Property(Get("events.collision").c_str());
                                DrawEnumCombo("##Collision", payload.collisionType, kSectorCollisionOptions);
                                Tooltip(Get("editor.tooltip.dispatches.collision").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorDoor>) {
                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                Property(Get("events.door.open_mode").c_str());
                                DrawEnumCombo("##OpenMode", payload.openMode, kDoorOpenModeOptions);

                                if (payload.openMode == PayloadDispatchSectorDoor::FIXED_HEIGHT) {
                                    Property(Get("events.door.open_height").c_str());
                                    InputOrDrag("##OpenHeight", &payload.openHeight, draggable, 0.1f);
                                }
                                else {
                                    Property(Get("events.door.lip").c_str());
                                    InputOrDrag("##Lip", &payload.lip, draggable, 0.1f);
                                    Tooltip(Get("editor.tooltip.dispatches.door_lip").c_str());
                                }

                                Property(Get("events.door.open_speed").c_str());
                                if (InputOrDrag("##OpenSpeed", &payload.openSpeed, draggable, 0.1f))
                                    payload.openSpeed = std::max(0.01f, payload.openSpeed);

                                Property(Get("events.door.close_speed").c_str());
                                if (InputOrDrag("##CloseSpeed", &payload.closeSpeed, draggable, 0.1f))
                                    payload.closeSpeed = std::max(0.01f, payload.closeSpeed);

                                Property(Get("events.door.wait").c_str());
                                if (InputOrDrag("##Wait", &payload.wait, draggable, 0.1f))
                                    payload.wait = std::max(0.0f, payload.wait);
                                Tooltip(Get("editor.tooltip.dispatches.door_wait").c_str());

                                Property(Get("events.door.close_collision").c_str());
                                DrawEnumCombo("##Collision", payload.closeCollision, kSectorCollisionOptions);
                                Tooltip(Get("editor.tooltip.dispatches.door_collision").c_str());

                                Property(Get("events.door.retrigger").c_str());
                                DrawEnumCombo("##Retrigger", payload.retrigger, kDoorRetriggerOptions);
                                Tooltip(Get("editor.tooltip.dispatches.door_retrigger").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorStopMovement>) {
                                Property(Get("events.surface").c_str());
                                DrawEnumCombo("##Surface", payload.surface, kSurfaceOptions);

                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorSetLight>) {
                                // 0-255 like the sector inspector's light.
                                float color[3] = {payload.light.x / 255.0f, payload.light.y / 255.0f, payload.light.z / 255.0f};
                                Property(Get("events.light").c_str());
                                if (ImGui::ColorEdit3("##Light", color, CompactColorFlags(ImGui::GetContentRegionAvail().x, 3)))
                                    payload.light = {color[0] * 255.0f, color[1] * 255.0f, color[2] * 255.0f};

                                Property(Get("events.speed").c_str());
                                if (InputOrDrag("##Speed", &payload.speed, draggable, 1.0f))
                                    payload.speed = std::max(0.0f, payload.speed);
                                Tooltip(Get("editor.tooltip.dispatches.light_speed").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorSetTexture>) {
                                Property(Get("events.surface").c_str());
                                DrawEnumCombo("##Surface", payload.surface, kSurfaceOptions);

                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                DrawAssetRow("events.texture", payload.texture, AssetKind::Texture, "editor.tooltip.dispatches.sector_texture");
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchChangeTag>) {
                                Property(Get("events.operation").c_str());
                                DrawEnumCombo("##Operation", payload.remove, kTagOperationOptions);

                                Property(Get("events.tag").c_str());
                                DrawTagCombo("##Tag", payload.tag);
                                Tooltip(Get("editor.tooltip.dispatches.change_tag").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityChangeVelocity>) {
                                Property(Get("events.velocity_mode").c_str());
                                DrawEnumCombo("##Mode", payload.set, kVelocityModeOptions);

                                Property(Get("events.velocity").c_str());
                                InputOrDrag3("##Velocity", &payload.velocity.x, draggable);
                                Tooltip(Get("editor.tooltip.dispatches.velocity").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityMoveTo>) {
                                Property(Get("events.position").c_str());
                                InputOrDrag3("##Position", &payload.position.x, draggable);

                                Property(Get("events.relative").c_str());
                                ImGui::Checkbox("##Relative", &payload.relative);
                                Tooltip(Get("editor.tooltip.dispatches.move_relative").c_str());

                                Property(Get("events.move_timing").c_str());
                                DrawEnumCombo("##Timing", payload.useDuration, kMoveTimingOptions);

                                if (payload.useDuration) {
                                    Property(Get("events.duration").c_str());
                                    if (InputOrDrag("##Duration", &payload.duration, draggable, 0.05f))
                                        payload.duration = std::max(0.0f, payload.duration);
                                }
                                else {
                                    Property(Get("events.speed").c_str());
                                    if (InputOrDrag("##Speed", &payload.speed, draggable, 0.1f))
                                        payload.speed = std::max(0.01f, payload.speed);
                                }

                                Property(Get("events.rotate").c_str());
                                ImGui::Checkbox("##Rotate", &payload.rotate);

                                if (payload.rotate) {
                                    Property(Get("events.rotation").c_str());
                                    InputOrDrag3("##Rotation", &payload.rotation.x, draggable);
                                }

                                const ComponentTransform *transform = level.transforms.Get(ownerID);
                                Property("");
                                ImGui::BeginDisabled(transform == nullptr);
                                if (ImGui::Button(Get("events.use_entity").c_str(), ImVec2(-FLT_MIN, 0.0f)) && transform != nullptr) {
                                    payload.position = transform->position;
                                    payload.relative = false;
                                    payload.rotation = transform->rotation.Normalized().ToEulerDegrees();
                                }
                                ImGui::EndDisabled();
                                Tooltip(Get("editor.tooltip.dispatches.use_entity").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntitySetComponent>) {
                                Property(Get("events.component").c_str());
                                DrawEnumCombo("##Component", payload.component, kToggleableComponentOptions);
                                Tooltip(Get("editor.tooltip.dispatches.component").c_str());

                                Property(Get("events.toggle").c_str());
                                DrawEnumCombo("##Mode", payload.mode, kToggleModeOptions);
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSpawnPrefab>) {
                                DrawAssetRow("events.prefab", payload.prefab, AssetKind::Prefab, "editor.tooltip.dispatches.prefab");

                                Property(Get("events.world_position").c_str());
                                ImGui::Checkbox("##WorldPosition", &payload.worldPosition);
                                Tooltip(Get("editor.tooltip.dispatches.world_position").c_str());

                                Property(Get(payload.worldPosition ? "events.position" : "events.offset").c_str());
                                InputOrDrag3("##Offset", &payload.offset.x, draggable);

                                Property(Get("events.rotation").c_str());
                                InputOrDrag3("##Rotation", &payload.rotation.x, draggable);
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchPlaySound>) {
                                DrawAssetRow("events.sound", payload.sound, AssetKind::Sound, "editor.tooltip.dispatches.sound");

                                Property(Get("events.gain").c_str());
                                if (InputOrDrag("##Gain", &payload.gain, draggable, 0.01f))
                                    payload.gain = std::max(0.0f, payload.gain);

                                Property(Get("events.pitch").c_str());
                                if (InputOrDrag("##Pitch", &payload.pitch, draggable, 0.01f))
                                    payload.pitch = std::max(0.01f, payload.pitch);
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchShowMessage>) {
                                Property(Get("events.text").c_str());
                                ImGui::InputText("##Text", &payload.text);

                                Property(Get("events.duration").c_str());
                                if (InputOrDrag("##Duration", &payload.duration, draggable, 0.05f))
                                    payload.duration = std::max(0.0f, payload.duration);

                                Property(Get("events.color").c_str());
                                ImGui::ColorEdit3("##Color", &payload.color.x,
                                                  CompactColorFlags(ImGui::GetContentRegionAvail().x, 3));
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchRelay> ||
                                               std::is_same_v<Payload, PayloadDispatchResetEvent>) {
                                Property(Get("events.target_tag").c_str());
                                DrawTagCombo("##Target", payload.target);
                                Tooltip(Get(std::is_same_v<Payload, PayloadDispatchRelay>
                                                ? "editor.tooltip.dispatches.relay"
                                                : "editor.tooltip.dispatches.reset_event").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchLoadLevel>) {
                                DrawLevelNameRow(payload.levelName);
                            }
                        }, dispatch.payload);

                        EndProperties();
                    }
                }
            }
            EndEntryCard();

            if (deleteClicked) dispatchToRemove = static_cast<int>(i);

            ImGui::PopID();
        }

        if (dispatchToRemove >= 0) dispatches.erase(dispatches.begin() + dispatchToRemove);

        DrawAddEntryCombo("dispatches.add_placeholder", dispatches, kind);
    }

    // The Events and Dispatches sections, placed right under Tags. An entity
    // wraps each in its prefab field instead (see the entity inspector).
    static void DrawEventSections(std::vector<Event> &events, std::vector<Dispatch> &dispatches,
                                  const EventOwnerKind kind, const ID ownerID, const bool draggable) {
        if (InspectorSection(Get("events.title"), "Events", static_cast<int>(events.size()), false)) {
            ImGui::PushID("Events");
            DrawEventsEditor(events, kind, ownerID);
            ImGui::PopID();
        }

        if (InspectorSection(Get("dispatches.title"), "Dispatches", static_cast<int>(dispatches.size()), false)) {
            ImGui::PushID("Dispatches");
            DrawDispatchesEditor(dispatches, kind, ownerID, draggable);
            ImGui::PopID();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Script attachment body — shared by the entity Script panel and the
    //  sector inspector's Scripts section
    // ─────────────────────────────────────────────────────────────────────────
    // Everything about one attached script except removing it (each caller
    // owns that): file picker, enabled flag, compile error and public
    // variables. Takes the owner-agnostic
    // ScriptAttachmentData so entity and sector scripts get the exact same
    // controls. `ownerLabel` ("entity 4", "sector 2") only shows up in log
    // messages. Callers must have pushed an ID unique to this attachment.
    static void DrawScriptAttachmentFields(ScriptAttachmentData &script, const std::string &ownerLabel) {
        BeginSection("Script File");

        {
            PrefabField prefabField("fileName");
            if (MapEditorInternal::DrawAssetField(Get("component.script.file_name").c_str(), script.fileName, AssetKind::Script)) {
                LevelSystem::ReconcileScriptPublicValues(script, ownerLabel);
            }
            Tooltip(Get("editor.tooltip.component.script.file").c_str());
        }

        {
            PrefabField prefabField("enabled");
            ImGui::Checkbox(Get("component.script.enabled").c_str(), &script.enabled);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Refresh Fields")) {
            LevelSystem::ReconcileScriptPublicValues(script, ownerLabel);
        }

        if (!script.fileName.empty()) {
            ImGui::SameLine();

            // fileName is the Assets-relative path without ".lua".
            if (ImGui::SmallButton(Get("component.script.open_file").c_str()))
                MapEditorInternal::assetBrowser.RequestOpenScript(ProjectManager::GetAssetsPath() / (script.fileName + ".lua"));
            Tooltip(Get("editor.tooltip.component.script.open_file").c_str());
        }

        EndSection();

        // Syntax/compile error in the file itself (never runs the script).
        // Runtime errors still go to the in-game console.
        if (const std::string *loadError = LevelSystem::GetScriptLoadError(script.fileName)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            ImGui::TextWrapped("Script error: %s", loadError->c_str());
            ImGui::PopStyleColor();
        }

        const std::vector<ScriptPublicField> *fields =
                LevelSystem::GetPublicFieldsForScript(script.fileName);

        if (fields == nullptr) {
            if (!script.fileName.empty()) {
                ImGui::TextDisabled("Script not found or has no public fields.");
            }
            return;
        }

        BeginSection("Public Variables");

        // Drop values for fields the script no longer declares (e.g. after
        // editing it while this inspector is open).
        std::erase_if(script.publicValues, [fields](const auto &entry) {
            return std::ranges::none_of(*fields, [&entry](const ScriptPublicField &field) {
                return field.name == entry.first;
            });
        });

        for (const ScriptPublicField &field: *fields) {
            auto valueIt = script.publicValues.find(field.name);

            if (valueIt == script.publicValues.end()) {
                script.publicValues[field.name] = field.defaultValue;
                valueIt = script.publicValues.find(field.name);
            }

            PrefabField prefabField("publicValues/" + field.name);
            DrawScriptValueEditor(field, valueIt->second);
        }

        EndSection();
    }

    // One floor or ceiling of a sector floor: texture with its offset, scale
    // and flips beside it, then color and slope.
    static void DrawSectorSurface(const char *id, const char *textureTooltipKey, SectorSurface &surface,
                                  const bool draggable) {
        TextureBlock(id, surface.texture, textureTooltipKey, [&] {
            TextureTransformRows("sector", surface.textureOffset, surface.textureScale,
                                 surface.flipTextureX, surface.flipTextureY, draggable);

            const std::string resetLabel = Get("common.reset");

            Property(Get("inspector.color").c_str());
            const float colorWidth = WidthBeforeButtons({resetLabel.c_str()});
            FieldWidth(colorWidth);
            ImGui::ColorEdit4("##Color", &surface.color.x,
                              ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | CompactColorFlags(colorWidth, 4));
            ImGui::SameLine();
            if (ImGui::Button((resetLabel + "##color").c_str())) surface.color = {1.0f, 1.0f, 1.0f, 1.0f};

            // Direction, strength, Reset on one row.
            const char *slopeDirections[] = {"+X", "-X", "+Z", "-Z"};
            int slopeDirection = static_cast<int>(surface.slopeDirection);

            Property(Get("inspector.slope").c_str());
            FieldWidth(64.0f);
            if (ImGui::Combo("##SlopeDirection", &slopeDirection, slopeDirections, IM_ARRAYSIZE(slopeDirections)))
                surface.slopeDirection = static_cast<SlopeDirection>(slopeDirection);
            Tooltip(Get("sector.slope_direction").c_str());

            ImGui::SameLine();
            FieldWidth(WidthBeforeButtons({resetLabel.c_str()}));
            InputOrDrag("##SlopeStrength", &surface.slopeStrength, draggable, .1f);
            Tooltip(Get("sector.slope_strength").c_str());

            ImGui::SameLine();
            if (ImGui::Button((resetLabel + "##slope").c_str())) {
                surface.slopeDirection = PLUS_X;
                surface.slopeStrength = 0.0f;
            }
        });
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Sector Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawSectorEditor(Sector &sector, bool *open, const bool draggable, bool *copyRequested) {
        bool deleteRequested = false;
        int floorToRemove = -1;

        if (sector.floors.empty()) {
            sector.floors.push_back({
                {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
                {40.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}}
            });
        }

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin(Get("sector.title").c_str(), open, kInspectorFlags)) {
            ImGui::End();
            return false;
        }

        // ── Header: name, ID, Copy, Delete ───────────────────────────────────
        {
            const std::string copyLabel = Get("sector.copy");
            const std::string deleteLabel = Get("common.delete");

            const float buttonsWidth = copyRequested != nullptr
                ? HeaderButtonsWidth({copyLabel.c_str(), deleteLabel.c_str()})
                : HeaderButtonsWidth({deleteLabel.c_str()});

            InspectorNameRow(sector.name, sector.id, buttonsWidth, "editor.tooltip.inspector.name");

            if (copyRequested != nullptr) {
                ImGui::SameLine();
                if (ImGui::Button(copyLabel.c_str())) *copyRequested = true;
                Tooltip(Get("editor.tooltip.sector.copy").c_str());
            }

            ImGui::SameLine();
            if (DangerButton(deleteLabel.c_str())) {
                deleteRequested = true;
                if (open != nullptr) *open = false;
            }
            Tooltip(Get("editor.tooltip.sector.delete").c_str());
        }

        // ── Floors ───────────────────────────────────────────────────────────
        for (size_t floorIndex = 0; floorIndex < sector.floors.size(); ++floorIndex) {
            ImGui::PushID(static_cast<int>(floorIndex));

            SectorFloor &sectorFloor = sector.floors[floorIndex];

            const bool invalidRoom = sectorFloor.floor.height > sectorFloor.ceiling.height;

            const bool overlapsPrevious = floorIndex > 0 && sectorFloor.floor.height < sector.floors[floorIndex - 1].ceiling.height;

            const bool overlapsNext = floorIndex + 1 < sector.floors.size() && sectorFloor.ceiling.height > sector.floors[floorIndex + 1].floor.height;

            const bool hasInvalidHeights = invalidRoom || overlapsPrevious || overlapsNext;

            // The warning marker can come and go without resetting the
            // header's open/closed state (InspectorSection's "###" ID).
            const std::string sectionTitle =
                    Get("sector.floor") + " " + std::to_string(floorIndex + 1) + (hasInvalidHeights ? "  (!)" : "");

            // DefaultOpen only applies the first time ImGui sees this ID, so a
            // floor the user collapses stays collapsed.
            const bool floorOpen = InspectorSection(sectionTitle, "floor_header");

            // The warning is inside the body, which is hidden while collapsed.
            if (hasInvalidHeights) Tooltip(Get("editor.tooltip.sector.invalid_floor_heights").c_str());

            if (!floorOpen) {
                ImGui::PopID();
                continue;
            }

            if (hasInvalidHeights) WarningText(Get("editor.tooltip.sector.invalid_floor_heights").c_str());

            // ── Heights ──────────────────────────────────────────────────────
            if (BeginProperties("##Heights")) {
                float floorHeight = sectorFloor.floor.height;

                Property(Get("sector.floor_height").c_str());
                if (InputOrDrag("##FloorHeight", &floorHeight, draggable)) {
                    const float minimumHeight = floorIndex > 0 ? sector.floors[floorIndex - 1].ceiling.height : std::numeric_limits<float>::lowest();

                    const float maximumHeight = sectorFloor.ceiling.height - MIN_ROOM_HEIGHT;

                    if (minimumHeight <= maximumHeight) sectorFloor.floor.height = std::clamp(floorHeight, minimumHeight, maximumHeight);
                }
                Tooltip(Get("editor.tooltip.sector.floor_height").c_str());

                float ceilingHeight = sectorFloor.ceiling.height;

                Property(Get("sector.ceil_height").c_str());
                if (InputOrDrag("##CeilingHeight", &ceilingHeight, draggable)) {
                    const float minimumHeight = sectorFloor.floor.height + MIN_ROOM_HEIGHT;

                    const float maximumHeight = floorIndex + 1 < sector.floors.size() ? sector.floors[floorIndex + 1].floor.height : std::numeric_limits<float>::max();

                    if (minimumHeight <= maximumHeight) sectorFloor.ceiling.height = std::clamp(ceilingHeight, minimumHeight, maximumHeight);
                }
                Tooltip(Get("editor.tooltip.sector.ceil_height").c_str());

                EndProperties();
            }

            // ── Floor / Ceiling tabs, like a wall's Top / Bottom / Middle ────
            ImGui::Spacing();
            if (ImGui::BeginTabBar("##Surfaces")) {
                if (ImGui::BeginTabItem(Get("sector.floor_surface").c_str())) {
                    DrawSectorSurface("floor", "editor.tooltip.sector.floor_texture", sectorFloor.floor, draggable);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(Get("sector.ceiling_surface").c_str())) {
                    DrawSectorSurface("ceiling", "editor.tooltip.sector.ceil_texture", sectorFloor.ceiling, draggable);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            // Small, on the right, after the floor's settings.
            ImGui::Spacing();
            const std::string removeLabel = Get("sector.remove_floor");
            const float removeWidth = ImGui::CalcTextSize(removeLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - removeWidth));

            ImGui::BeginDisabled(sector.floors.size() <= 1);
            if (SmallDangerButton(removeLabel.c_str())) floorToRemove = static_cast<int>(floorIndex);
            ImGui::EndDisabled();
            Tooltip(Get("editor.tooltip.sector.remove_floor").c_str());

            ImGui::PopID();
        }

        if (floorToRemove >= 0) sector.floors.erase(sector.floors.begin() + floorToRemove);

        ImGui::Spacing();

        if (ImGui::Button(Get("sector.add_floor").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
            const float floorHeight = sector.floors.empty() ? 0.0f : sector.floors.back().ceiling.height + 10.0f;

            sector.floors.push_back({
                {
                    floorHeight,
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    {}
                },
                {
                    floorHeight + 40.0f,
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    {}
                }
            });
        }

        Tooltip(Get("editor.tooltip.sector.add_floor").c_str());

        // ── Lighting ─────────────────────────────────────────────────────────
        if (InspectorSection(Get("sector.lighting"), "Lighting") && BeginProperties("##Lighting")) {
            float lightColor[3] = {
                sector.light.x / 255.0f,
                sector.light.y / 255.0f,
                sector.light.z / 255.0f
            };

            const std::string resetLabel = Get("common.reset");

            Property(Get("inspector.color").c_str());
            const float colorWidth = WidthBeforeButtons({resetLabel.c_str()});
            FieldWidth(colorWidth);

            if (ImGui::ColorEdit3(
                    "##LightColor", lightColor,
                    ImGuiColorEditFlags_Uint8 |
                    ImGuiColorEditFlags_DisplayRGB |
                    ImGuiColorEditFlags_PickerHueWheel |
                    CompactColorFlags(colorWidth, 3))) {
                sector.light.x = lightColor[0] * 255.0f;
                sector.light.y = lightColor[1] * 255.0f;
                sector.light.z = lightColor[2] * 255.0f;
            }

            Tooltip(Get("editor.tooltip.sector.light_color").c_str());

            ImGui::SameLine();

            if (ImGui::Button(resetLabel.c_str())) sector.light = {255.0f, 255.0f, 255.0f};

            EndProperties();
        }

        // ── Tags ─────────────────────────────────────────────────────────────
        // See DrawTagsEditor for the shared row/assignment logic reused by
        // the Wall and Entity inspectors.
        if (InspectorSection(Get("sector.tags"), "Tags", static_cast<int>(sector.tagIds.size()), false)) {
            ImGui::PushID("SectorTags");
            DrawTagsEditor(sector.tagIds);
            ImGui::PopID();
        }

        DrawEventSections(sector.events, sector.dispatches, EventOwnerKind::SECTOR, sector.id, draggable);

        // ── Parent Sector ────────────────────────────────────────────────────
        // Picked from the combo or dropped from the hierarchy. Attaching keeps
        // every value as it is - from then on, edits to the parent move this
        // sector's values by the same amount (Level::PropagateSectorChanges).
        if (InspectorSection(Get("sector.parent"), "Parent", -1, false)) {
            ImGui::PushID("SectorParent");

            Level &level = LevelManager::CurrentLevel();

            // Keyed by sector ID so an error doesn't follow the user to the next
            // sector they select.
            static ID lastParentEditSectorId = INVALID_ID;
            static bool parentRejected = false;

            if (lastParentEditSectorId != sector.id) {
                lastParentEditSectorId = sector.id;
                parentRejected = false;
            }

            const Sector *currentParent = level.GetSector(sector.parentID);
            const std::string parentPreview = currentParent != nullptr ? DescribeSector(*currentParent) : "(None)";

            ID requestedParent = sector.parentID;

            const bool parentPicked = DrawLevelObjectRefField(
                Get("sector.parent_field").c_str(), requestedParent, parentPreview,
                MapEditorInternal::SECTOR_REF_PAYLOAD, "editor.tooltip.sector.parent",
                [&level, &sector](ID &id, bool &changed) {
                    for (const Sector &candidate : level.sectors) {
                        // The sector itself and its descendants would make a cycle.
                        if (level.IsSectorAncestor(sector.id, candidate.id)) continue;

                        const bool isSelected = candidate.id == id;

                        if (ImGui::Selectable(DescribeSector(candidate).c_str(), isSelected)) {
                            id = candidate.id;
                            changed = true;
                        }

                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            );

            // Only a drop can get here with a cycle, the combo never lists one.
            if (parentPicked) parentRejected = !level.SetSectorParent(sector.id, requestedParent);

            if (parentRejected) WarningText(Get("sector.parent_cycle_error").c_str());

            if (!sector.children.empty()) SmallMetaText("%s: %d", Get("sector.child_count").c_str(), static_cast<int>(sector.children.size()));

            ImGui::PopID();
        }

        // ── Scripts ──────────────────────────────────────────────────────────
        // Sector scripts (Sector::scripts). Same per-script controls as an
        // entity's Script component (DrawScriptAttachmentFields), drawn
        // inline - one collapsible block per attached script - instead of
        // behind an Edit button.
        if (InspectorSection(Get("sector.scripts"), "Scripts", static_cast<int>(sector.scripts.size()), false)) {
            ImGui::PushID("SectorScripts");

            const std::string scriptOwnerLabel = "sector " + std::to_string(sector.id);
            ScriptInstanceID scriptToRemove = INVALID_SCRIPT_INSTANCE_ID;

            for (SectorScript &script: sector.scripts) {
                // std::to_string, not a truncating cast - instance IDs are 64-bit.
                ImGui::PushID(std::to_string(script.instanceID).c_str());

                const std::string displayName = script.fileName.empty()
                    ? std::string("Script (unassigned)")
                    : std::filesystem::path(script.fileName).filename().string();

                // "###script" keeps the header's ID (and open/closed state)
                // stable while the visible text changes with the file name.
                const std::string headerLabel = (script.enabled ? displayName : displayName + " (disabled)") + "###script";

                bool removeClicked = false;
                if (BeginEntryCard("##ScriptCard") && EntryCardHeader(headerLabel, removeClicked))
                    DrawScriptAttachmentFields(script, scriptOwnerLabel);
                EndEntryCard();

                if (removeClicked) scriptToRemove = script.instanceID;

                ImGui::PopID();
            }

            if (scriptToRemove != INVALID_SCRIPT_INSTANCE_ID) sector.RemoveScript(scriptToRemove);

            if (sector.scripts.empty()) ImGui::TextDisabled("%s", Get("sector.no_scripts").c_str());

            // Same as entities: adds an unassigned script; the file is picked
            // from the row it creates.
            if (ImGui::Button(Get("sector.add_script").c_str(), ImVec2(-FLT_MIN, 0.0f))) sector.AddScript();

            Tooltip(Get("editor.tooltip.sector.add_script").c_str());

            ImGui::PopID();
        }

        ImGui::End();
        return deleteRequested;
    }

    // Offset, scale, flips and anchor of one wall surface, as property rows.
    static void DrawWallSurfaceRows(WallSurface &surface, const bool draggable) {
        TextureTransformRows("wall", surface.textureOffset, surface.textureScale,
                             surface.flipTextureX, surface.flipTextureY, draggable);

        const WallTextureAnchorInfo *selected = FindWallTextureAnchor(surface.anchor);
        if (selected == nullptr) selected = &WALL_TEXTURE_ANCHORS[0];

        Property(Get("wall.anchor").c_str());

        if (ImGui::BeginCombo("##Anchor", Get(selected->labelKey).c_str())) {
            for (const WallTextureAnchorInfo &anchor : WALL_TEXTURE_ANCHORS) {
                const bool isSelected = anchor.anchor == surface.anchor;

                if (ImGui::Selectable(Get(anchor.labelKey).c_str(), isSelected)) surface.anchor = anchor.anchor;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Get(anchor.tooltipKey).c_str());
                if (isSelected) ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        Tooltip(Get(selected->tooltipKey).c_str());
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Wall Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawWallEditor(Wall &wall, bool *open, const bool draggable) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Get("wall.title").c_str(), open, kInspectorFlags)) {
            ImGui::End();
            return false;
        }

        // ── Header: name, ID, Delete ─────────────────────────────────────────
        {
            const std::string deleteLabel = Get("common.delete");

            InspectorNameRow(wall.name, wall.id, HeaderButtonsWidth({deleteLabel.c_str()}), "editor.tooltip.inspector.name");

            ImGui::SameLine();
            if (DangerButton(deleteLabel.c_str())) {
                deleteRequested = true;
                if (open) *open = false;
            }
            Tooltip(Get("editor.tooltip.wall.delete").c_str());
        }

        // ── General ──────────────────────────────────────────────────────────
        if (InspectorSection(Get("inspector.general"), "General") && BeginProperties("##General")) {
            Property(Get("editor.wall.length").c_str());
            ImGui::TextDisabled("%.2f", wall.length);

            Property(Get("editor.wall.angle").c_str());
            ImGui::TextDisabled("%.2f", wall.angle);

            bool wallSectorChanged = false;

            Property(Get("wall.front_sector").c_str());
            wallSectorChanged |= InputID("##FrontSector", wall.frontSector);
            Tooltip(Get("editor.tooltip.wall.front_sector").c_str());

            Property(Get("wall.back_sector").c_str());
            wallSectorChanged |= InputID("##BackSector", wall.backSector);
            Tooltip(Get("editor.tooltip.wall.back_sector").c_str());

            if (wallSectorChanged)
                MapQueries::RebuildSectorRuntimeLinks(LevelManager::CurrentLevel());

            Property(Get("inspector.color").c_str());
            ImGui::ColorEdit4(
                "##WallColor",
                &wall.color.x,
                ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_AlphaPreviewHalf |
                ImGuiColorEditFlags_HDR |
                ImGuiColorEditFlags_Float |
                CompactColorFlags(ImGui::GetContentRegionAvail().x, 4)
            );
            Tooltip(Get("editor.tooltip.wall.color").c_str());

            EndProperties();
        }

        // ── Textures ─────────────────────────────────────────────────────────
        // Only a portal has a step under the neighbour's floor, so only a
        // portal gets a Bottom texture; a solid wall shows Top everywhere.
        // Only a portal has openings too, so only a portal gets a Middle.
        if (InspectorSection(Get("wall.textures"), "Textures")) {
            if (wall.IsPortal()) {
                if (ImGui::BeginTabBar("##Surfaces")) {
                    if (ImGui::BeginTabItem(Get("wall.texture_top").c_str())) {
                        TextureBlock("Top", wall.top.texture, "editor.tooltip.wall.texture",
                                     [&] { DrawWallSurfaceRows(wall.top, draggable); });
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem(Get("wall.texture_bottom").c_str())) {
                        TextureBlock("Bottom", wall.bottom.texture, "editor.tooltip.wall.texture",
                                     [&] { DrawWallSurfaceRows(wall.bottom, draggable); });
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem(Get("wall.texture_middle").c_str())) {
                        TextureBlock("Middle", wall.middle.texture, "editor.tooltip.wall.texture_middle",
                                     [&] { DrawWallSurfaceRows(wall.middle, draggable); });

                        ImGui::Checkbox(Get("wall.has_collision").c_str(), &wall.middleHasCollision);
                        Tooltip(Get("editor.tooltip.wall.middle_has_collision").c_str());

                        ImGui::Checkbox(Get("wall.ignored_by_raycast").c_str(), &wall.middleIgnoredByRaycast);
                        Tooltip(Get("editor.tooltip.wall.middle_ignored_by_raycast").c_str());

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }
            else TextureBlock("Top", wall.top.texture, "editor.tooltip.wall.texture",
                              [&] { DrawWallSurfaceRows(wall.top, draggable); });
        }

        // ── Tags ─────────────────────────────────────────────────────────────
        if (InspectorSection(Get("wall.tags"), "Tags", static_cast<int>(wall.tagIds.size()), false)) {
            ImGui::PushID("WallTags");
            DrawTagsEditor(wall.tagIds);
            ImGui::PopID();
        }

        DrawEventSections(wall.events, wall.dispatches, EventOwnerKind::LINE, wall.id, draggable);

        ImGui::End();
        return deleteRequested;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Linedef Editor
    // ─────────────────────────────────────────────────────────────────────────
    void DrawLevelEventSections(Level &level, const bool draggable) {
        ImGui::PushID("LevelEvents");
        DrawEventSections(level.worldSettings.events, level.worldSettings.dispatches, EventOwnerKind::LEVEL, ID{0}, draggable);
        ImGui::PopID();
    }

    bool DrawLinedefEditor(Linedef &linedef, bool *open, const bool draggable) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Get("linedef.title").c_str(), open, kInspectorFlags)) {
            ImGui::End();
            return false;
        }

        // ── Summary header ───────────────────────────────────────────────────
        DrawInspectorHeader("Linedef", DescribeLinedef(linedef).c_str());

        BeginSection("Identity");
        SmallMetaText("ID: %u", linedef.id);

        ImGui::SameLine(60);

        SmallMetaText("%s: %f", Get("editor.wall.length").c_str(), linedef.length);

        ImGui::SameLine();

        SmallMetaText("%s: %f", Get("editor.wall.angle").c_str(), linedef.angle);

        FieldWidth(220.0f);
        ImGui::InputText(Get("linedef.name").c_str(), &linedef.name);

        // Nothing shares a linedef's ends, so they are edited directly.
        bool pointsChanged = false;

        FieldWidth(220.0f);
        pointsChanged |= InputOrDrag2(Get("linedef.start").c_str(), &linedef.start.x, draggable);

        FieldWidth(220.0f);
        pointsChanged |= InputOrDrag2(Get("linedef.end").c_str(), &linedef.end.x, draggable);

        if (pointsChanged) linedef.RefreshDerived();

        ImGui::Checkbox(Get("linedef.enabled").c_str(), &linedef.enabled);
        Tooltip(Get("editor.tooltip.linedef.enabled").c_str());
        EndSection();

        DrawEventSections(linedef.events, linedef.dispatches, EventOwnerKind::LINEDEF, linedef.id, draggable);

        // ── Actions ──────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (DangerButton(Get("common.delete").c_str())) {
            deleteRequested = true;
            if (open) *open = false;
        }
        Tooltip(Get("editor.tooltip.linedef.delete").c_str());

        ImGui::SameLine();

        if (ImGui::Button(Get("common.close").c_str()))
            if (open) *open = false;

        ImGui::End();
        return deleteRequested;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Prefab instance marks
    // ─────────────────────────────────────────────────────────────────────────
    namespace {
        constexpr ImVec4 PREFAB_OVERRIDE_TEXT = {0.55f, 0.78f, 1.0f, 1.0f};
        constexpr ImU32 PREFAB_OVERRIDE_BAR = IM_COL32(60, 150, 255, 255);

        // A prefab action picked in the inspector. Run by EndPrefabFields
        // (or at the end of the entity window), after the widgets that might
        // still hold pointers into the component storages are done.
        struct PrefabAction {
            enum class Kind { None, RevertPath, ApplyPath, RevertComponent, ApplyComponent, RestoreComponent, RevertAll, ApplyAll, Unpack };
            Kind kind = Kind::None;
            ID entityID = INVALID_ID;
            std::string path;
            int componentType = -1;
            std::uint64_t instanceID = 0;
        };

        struct PrefabFieldContext {
            bool active = false;
            ID entityID = INVALID_ID;
            std::string pathPrefix; // "entity/" or "<key>/<prefab id>/"
            std::vector<std::string> overridden;
        };

        PrefabFieldContext prefabFieldContext;
        PrefabAction pendingPrefabAction;

        void RunPendingPrefabAction() {
            const PrefabAction action = pendingPrefabAction;
            pendingPrefabAction = {};
            if (action.kind == PrefabAction::Kind::None) return;

            Level& level = LevelManager::CurrentLevel();
            Entity* entity = level.GetEntity(action.entityID);
            if (entity == nullptr || !entity->prefabLink.IsInstance()) return;

            std::string error;
            bool ok = true;

            switch (action.kind) {
                case PrefabAction::Kind::RevertPath: Prefab::RevertOverride(level, *entity, action.path); break;
                case PrefabAction::Kind::ApplyPath: ok = Prefab::ApplyOverride(level, *entity, action.path, &error); break;
                case PrefabAction::Kind::RevertComponent: Prefab::RevertComponent(level, *entity, action.componentType, action.instanceID); break;
                case PrefabAction::Kind::ApplyComponent: ok = Prefab::ApplyComponent(level, *entity, action.componentType, action.instanceID, &error); break;
                case PrefabAction::Kind::RestoreComponent: Prefab::RestoreComponent(level, *entity, action.componentType, action.instanceID); break;
                case PrefabAction::Kind::RevertAll: Prefab::RevertAll(level, *entity); break;
                case PrefabAction::Kind::ApplyAll: ok = Prefab::ApplyAll(level, *entity, &error); break;
                case PrefabAction::Kind::Unpack: Prefab::Unpack(*entity); break;
                case PrefabAction::Kind::None: break;
            }

            if (!ok) spdlog::error("Prefab: {}", error);
        }

        // Revert / Apply to Prefab for one component, on its card or window.
        void DrawPrefabComponentMenuItems(const Entity& entity, const int componentType, const std::uint64_t instanceID) {
            const bool added = Prefab::PrefabInstanceIDOf(entity, componentType, instanceID) == 0;
            const bool overridden = added || !Prefab::ComponentOverrides(LevelManager::CurrentLevel(), entity, componentType, instanceID).empty();

            if (ImGui::MenuItem(added ? "Remove (Revert)" : "Revert", nullptr, false, overridden))
                pendingPrefabAction = {PrefabAction::Kind::RevertComponent, entity.id, {}, componentType, instanceID};
            if (ImGui::MenuItem(added ? "Add to Prefab" : "Apply to Prefab", nullptr, false, overridden))
                pendingPrefabAction = {PrefabAction::Kind::ApplyComponent, entity.id, {}, componentType, instanceID};
        }
    }

    void BeginPrefabFields(Entity &entity, const int componentType, const std::uint64_t instanceID) {
        prefabFieldContext = {};
        if (!entity.prefabLink.IsInstance()) return;

        if (componentType < 0) {
            prefabFieldContext.active = true;
            prefabFieldContext.entityID = entity.id;
            prefabFieldContext.pathPrefix = "entity/";
            prefabFieldContext.overridden = Prefab::EntityOverrides(entity);
            return;
        }

        const std::uint64_t prefabID = Prefab::PrefabInstanceIDOf(entity, componentType, instanceID);
        if (prefabID == 0) return; // added on the instance: the whole component is the override

        prefabFieldContext.active = true;
        prefabFieldContext.entityID = entity.id;
        prefabFieldContext.pathPrefix = std::string(Prefab::ComponentKey(componentType)) + "/" + std::to_string(prefabID) + "/";
        prefabFieldContext.overridden = Prefab::ComponentOverrides(LevelManager::CurrentLevel(), entity, componentType, instanceID);
    }

    void EndPrefabFields() {
        prefabFieldContext = {};
        RunPendingPrefabAction();
    }

    PrefabField::PrefabField(std::string fieldName) : field(std::move(fieldName)) {
        active = prefabFieldContext.active;
        if (!active) return;

        overridden = std::ranges::find(prefabFieldContext.overridden, field) != prefabFieldContext.overridden.end();
        startY = ImGui::GetCursorScreenPos().y;
        if (overridden) ImGui::PushStyleColor(ImGuiCol_Text, PREFAB_OVERRIDE_TEXT);
    }

    PrefabField::~PrefabField() {
        if (!active) return;
        if (overridden) ImGui::PopStyleColor();

        const float endY = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
        if (endY <= startY) return;

        const ImVec2 windowPos = ImGui::GetWindowPos();
        const float windowWidth = ImGui::GetWindowWidth();

        if (overridden)
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(windowPos.x + 2.0f, startY),
                                                      ImVec2(windowPos.x + 5.0f, endY), PREFAB_OVERRIDE_BAR);

        const std::string popupID = "prefab_field_menu_" + field;

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseHoveringRect(ImVec2(windowPos.x, startY), ImVec2(windowPos.x + windowWidth, endY)) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup(popupID.c_str());

        if (ImGui::BeginPopup(popupID.c_str())) {
            const std::string path = prefabFieldContext.pathPrefix + field;
            ImGui::TextDisabled("%s", field.c_str());
            if (ImGui::MenuItem("Revert", nullptr, false, overridden))
                pendingPrefabAction = {PrefabAction::Kind::RevertPath, prefabFieldContext.entityID, path};
            if (ImGui::MenuItem("Apply to Prefab", nullptr, false, overridden))
                pendingPrefabAction = {PrefabAction::Kind::ApplyPath, prefabFieldContext.entityID, path};
            ImGui::EndPopup();
        }
    }

    void DrawPrefabInstanceBar(Entity &entity) {
        if (!entity.prefabLink.IsInstance()) return;

        ImGui::PushID("prefab_instance_bar");

        ImGui::TextColored(PREFAB_OVERRIDE_TEXT, "Prefab: %s", entity.prefabLink.prefab.c_str());
        if (Prefab::Get(entity.prefabLink.prefab) == nullptr)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Prefab file missing - keeping the last saved values.");

        if (ImGui::SmallButton("Open"))
            MapEditorInternal::assetBrowser.RequestOpenPrefab(ProjectManager::GetAssetsPath() / entity.prefabLink.prefab);
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply All")) pendingPrefabAction = {PrefabAction::Kind::ApplyAll, entity.id};
        Tooltip("Writes every override, added and removed component into the prefab, so every instance gets them.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert All")) pendingPrefabAction = {PrefabAction::Kind::RevertAll, entity.id};
        Tooltip("Drops every override, removes added components and brings back removed ones.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Unpack")) pendingPrefabAction = {PrefabAction::Kind::Unpack, entity.id};
        Tooltip("Turns this into a plain entity that no longer follows the prefab.");

        // Prefab components this instance removed, each restorable.
        for (const PrefabRemovedComponent& removed : entity.prefabLink.removed) {
            ImGui::PushID(std::to_string(removed.componentType).c_str());
            ImGui::PushID(std::to_string(removed.prefabInstanceID).c_str());
            ImGui::TextDisabled("Removed: %s", GetComponentDisplayName(removed.componentType).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Restore"))
                pendingPrefabAction = {PrefabAction::Kind::RestoreComponent, entity.id, {}, removed.componentType, removed.prefabInstanceID};
            ImGui::PopID();
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    std::string PrefabCardLabel(const Entity &entity, const int componentType, const std::uint64_t instanceID, const std::string &label) {
        if (!entity.prefabLink.IsInstance() || componentType == CMP_TRANSFORM || componentType == CMP_UI_TRANSFORM) return label;
        return Prefab::PrefabInstanceIDOf(entity, componentType, instanceID) == 0 ? "+ " + label : label;
    }

    void DrawPrefabCardMarks(const Entity &entity, const int componentType, const std::uint64_t instanceID) {
        if (!entity.prefabLink.IsInstance()) return;

        const bool added = Prefab::PrefabInstanceIDOf(entity, componentType, instanceID) == 0 &&
                           componentType != CMP_TRANSFORM && componentType != CMP_UI_TRANSFORM;
        const bool overridden = added ||
            !Prefab::ComponentOverrides(LevelManager::CurrentLevel(), entity, componentType, instanceID).empty();

        if (overridden) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const float x = ImGui::GetWindowPos().x + 2.0f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, min.y), ImVec2(x + 3.0f, max.y), PREFAB_OVERRIDE_BAR);
        }

        if (componentType == CMP_TRANSFORM || componentType == CMP_UI_TRANSFORM) return;

        if (ImGui::BeginPopupContextItem("prefab_card_menu")) {
            DrawPrefabComponentMenuItems(entity, componentType, instanceID);
            ImGui::EndPopup();
        }
    }

    void FlushPrefabActions() {
        RunPendingPrefabAction();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Entity Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawEntityEditor(Entity &entity, EntityInspectorState &state, bool *open, const bool draggable) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        const std::string entityWindowTitle = state.windowTitle.empty() ? Get("entity.title") : state.windowTitle;

        const bool wasOpen = open == nullptr || *open;
        const bool windowVisible = ImGui::Begin(entityWindowTitle.c_str(), open, kInspectorFlags);

        // The window's X closes the component window too.
        if (wasOpen && open != nullptr && !*open) {
            state.editingComponent = false;
            state.selectedComponent = -1;
        }

        if (!windowVisible) {
            ImGui::End();
            return false;
        }

        if (state.drawExtras) state.drawExtras();

        DrawPrefabInstanceBar(entity);
        BeginPrefabFields(entity, -1, 0);

        // ── Header: enabled, name, ID, Delete ────────────────────────────────
        ImGui::PushID("entity_header");
        {
            const std::string deleteLabel = Get("common.delete");

            // Entity-level active state (Lua: entity.enabled). Disabling
            // an Entity pauses every attached script's ticking without
            // touching each script's own enabled flag - see Entity::enabled.
            // The prefab mark covers the whole row.
            PrefabField prefabField("enabled");

            ImGui::Checkbox("##Enabled", &entity.enabled);
            Tooltip(Get("editor.tooltip.entity.enabled").c_str());
            ImGui::SameLine();

            InspectorNameRow(entity.name, entity.id, state.allowDelete ? HeaderButtonsWidth({deleteLabel.c_str()}) : 0.0f,
                             "editor.tooltip.inspector.name");

            if (state.allowDelete) {
                ImGui::SameLine();

                // Not the Delete key while typing: it deletes text there.
                if (DangerButton(deleteLabel.c_str())
                    || (!ImGui::GetIO().WantTextInput && InputManager::GetKeyDown(SDL_SCANCODE_DELETE)))
                {
                    deleteRequested            = true;
                    state.editingComponent     = false;
                    state.selectedComponent    = -1;
                    if (open) *open = false;
                }
                Tooltip(Get("editor.tooltip.entity.common.delete").c_str());
            }
        }
        ImGui::PopID();

        // ── Snap to Wall / Snap to Floor, under the name ─────────────────────
        if (state.allowSnapToWall || state.allowSnapToFloor) {
            ImGui::PushID("snap_buttons");

            using WallSnapStatus = EntityInspectorState::WallSnapStatus;

            const std::string wallLabel = Get(
                state.wallSnapStatus == WallSnapStatus::Picking ? "entity.snap_to_wall_cancel"
                : state.wallSnapStatus == WallSnapStatus::Snapped ? "entity.disable_wall_snap"
                : "entity.snap_to_wall");
            const std::string floorLabel = Get("entity.snap_to_floor");

            // Side by side, splitting the row.
            const int buttonCount = (state.allowSnapToWall ? 1 : 0) + (state.allowSnapToFloor ? 1 : 0);
            const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * static_cast<float>(buttonCount - 1)) /
                                      static_cast<float>(buttonCount);

            if (state.allowSnapToWall) {
                if (ImGui::Button(wallLabel.c_str(), ImVec2(buttonWidth, 0.0f))) state.wallSnapButtonRequested = true;
                Tooltip(Get(
                    state.wallSnapStatus == WallSnapStatus::Picking ? "editor.tooltip.entity.snap_to_wall_cancel"
                    : state.wallSnapStatus == WallSnapStatus::Snapped ? "editor.tooltip.entity.disable_wall_snap"
                    : "editor.tooltip.entity.snap_to_wall").c_str());

                if (state.allowSnapToFloor) ImGui::SameLine();
            }

            if (state.allowSnapToFloor) {
                if (ImGui::Button(floorLabel.c_str(), ImVec2(buttonWidth, 0.0f))) state.snapToFloorRequested = true;
                Tooltip(Get("editor.tooltip.entity.snap_to_floor").c_str());
            }

            // A second row while snapped: Gap, Switch Sides.
            if (state.allowSnapToWall && state.wallSnapStatus == WallSnapStatus::Snapped && BeginProperties("##WallSnap")) {
                const std::string switchLabel = Get("entity.switch_sides");

                Property(Get("entity.wall_gap").c_str());
                FieldWidth(WidthBeforeButtons({switchLabel.c_str()}));

                if (ImGui::DragFloat("##WallSnapGap", &state.wallSnapGap, 0.01f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
                    state.wallSnapGapChanged = true;
                Tooltip(Get("editor.tooltip.entity.wall_gap").c_str());

                ImGui::SameLine();
                if (ImGui::Button(switchLabel.c_str())) state.switchSidesRequested = true;
                Tooltip(Get("editor.tooltip.entity.switch_sides").c_str());

                EndProperties();
            }

            ImGui::PopID();
        }

        // ── Components ───────────────────────────────────────────────────────
        // One card per component instance: [name]  [Edit]. An entity can have
        // several of every type except Transform, so the Edit button selects
        // the instance (`instanceID`; a ScriptInstanceID for scripts). Cards
        // of the same type can be dragged onto each other to reorder them -
        // the first one is what entity.<component> returns in Lua.
        struct ComponentDragPayload {
            int componentType;
            std::uint64_t instanceID;
        };

        auto DrawComponentCard = [&](
            const char *label,
            const int componentType,
            const std::uint64_t instanceID,
            const size_t indexInType
        ) {
            ImGui::PushID(componentType);
            // std::to_string, not a truncating cast to int - instance IDs are
            // 64-bit and must stay unique across every row in this list.
            ImGui::PushID(std::to_string(instanceID).c_str());

            const ImGuiStyle &style = ImGui::GetStyle();
            const std::string editLabel = Get("entity.edit_component");
            const float editWidth = ImGui::CalcTextSize(editLabel.c_str()).x + style.FramePadding.x * 2.0f;
            const float rowWidth = ImGui::GetContentRegionAvail().x;

            // Subtle background for the row
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const float rowHeight = ImGui::GetFrameHeight();

            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(rowMin, ImVec2(rowMin.x + rowWidth, rowMin.y + rowHeight),
                              ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f), 3.0f);

            const std::string cardLabel = PrefabCardLabel(entity, componentType, instanceID, label);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.FramePadding.x);
            ImGui::AlignTextToFramePadding();
            ImGui::Selectable(cardLabel.c_str(), false, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(rowWidth - editWidth - style.FramePadding.x - style.ItemSpacing.x, rowHeight));
            DrawPrefabCardMarks(entity, componentType, instanceID);

            if (componentType != CMP_TRANSFORM) {
                if (ImGui::BeginDragDropSource()) {
                    const ComponentDragPayload payload{componentType, instanceID};
                    ImGui::SetDragDropPayload("TILKY_COMPONENT", &payload, sizeof(payload));
                    ImGui::TextUnformatted(label);
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *dropped = ImGui::AcceptDragDropPayload("TILKY_COMPONENT")) {
                        const auto *payload = static_cast<const ComponentDragPayload *>(dropped->Data);
                        if (payload->componentType == componentType && payload->instanceID != instanceID)
                            MoveComponentInstance(componentType, payload->instanceID, indexInType);
                    }

                    // An asset from the Asset Browser fills this component's
                    // file field: a texture a Sprite's front slot, a model a
                    // Model, a sound an Audio Source, a flipbook a Flipbook.
                    const auto acceptAsset = [](const AssetKind kind) -> std::optional<std::string> {
                        const ImGuiPayload *dropped = ImGui::AcceptDragDropPayload(AssetBrowser::DragDropPayloadTypeFor(kind));
                        if (dropped == nullptr) return std::nullopt;
                        return AssetBrowser::ToAssetReference(static_cast<const char *>(dropped->Data), kind);
                    };

                    switch (componentType) {
                        case CMP_SPRITE:
                            if (const auto reference = acceptAsset(AssetKind::Texture))
                                if (auto *c = entity.GetComponentInstance<ComponentSprite>(instanceID)) c->textureFileNames[0] = *reference;
                            break;
                        case CMP_MODEL:
                            if (const auto reference = acceptAsset(AssetKind::Model))
                                if (auto *c = entity.GetComponentInstance<ComponentModel>(instanceID)) c->fileName = *reference;
                            break;
                        case CMP_AUDIO_SOURCE:
                            if (const auto reference = acceptAsset(AssetKind::Sound))
                                if (auto *c = entity.GetComponentInstance<ComponentAudioSource>(instanceID)) c->soundFileName = *reference;
                            break;
                        case CMP_FLIPBOOK:
                            if (const auto reference = acceptAsset(AssetKind::Flipbook))
                                if (auto *c = entity.GetComponentInstance<ComponentFlipbook>(instanceID)) c->flipbookFileName = *reference;
                            break;
                        default: break;
                    }

                    ImGui::EndDragDropTarget();
                }

                Tooltip(Get("editor.tooltip.entity.component_drag").c_str());
            }

            ImGui::SameLine(rowWidth - editWidth);

            if (ImGui::Button(editLabel.c_str())) {
                state.selectedComponent  = componentType;
                state.editingComponent   = true;
                state.selectedInstanceID = instanceID;
            }

            ImGui::PopID();
            ImGui::PopID();
        };

        // "Sprite", or "Sprite 1", "Sprite 2"... when there are several.
        auto DrawComponentCards = [&](const int componentType, const std::string &name, const std::vector<std::uint64_t> &instances) {
            for (size_t i = 0; i < instances.size(); ++i) {
                const std::string label = instances.size() > 1 ? name + " " + std::to_string(i + 1) : name;
                DrawComponentCard(label.c_str(), componentType, instances[i], i);
            }
        };

        if (InspectorSection(Get("entity.components"), "Components")) {
#define DRAW_ENTITY_COMPONENT_ROWS(Type, Bit, Storage, LabelKey) \
            if constexpr (Bit != CMP_SCRIPT) DrawComponentCards(Bit, Get(LabelKey), ComponentInstancesOf<Type>(entity));

            TILKY_NORMAL_COMPONENTS(DRAW_ENTITY_COMPONENT_ROWS)

#undef DRAW_ENTITY_COMPONENT_ROWS

            // Scripts are labelled by their file instead of the component name.
            {
                const std::vector<ComponentScript *> scripts = entity.GetScripts();

                for (size_t i = 0; i < scripts.size(); ++i) {
                    const ComponentScript *script = scripts[i];
                    const std::string displayName = script->fileName.empty()
                        ? std::string("Script (unassigned)")
                        : std::filesystem::path(script->fileName).filename().string();

                    const std::string label = script->enabled ? displayName : displayName + " (disabled)";

                    DrawComponentCard(label.c_str(), CMP_SCRIPT, script->instanceID, i);
                }
            }

            // "Add component..." - picking a type adds it straight away.
            ImGui::PushID("add_component_combo");

            std::array<std::string, CMP_NORMAL_COUNT> componentNames{};

#define FILL_COMPONENT_NAME(Type, Bit, Storage, LabelKey) \
            componentNames[Bit] = Get(LabelKey);
            TILKY_NORMAL_COMPONENTS(FILL_COMPONENT_NAME)
#undef FILL_COMPONENT_NAME

            ImGui::Spacing();
            FieldWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##AddComponent", Get("entity.add_component_placeholder").c_str())) {
                for (int i = 0; i < CMP_NORMAL_COUNT; i++) {
                    if (i == CMP_TRANSFORM) continue;
                    if (ImGui::Selectable(componentNames[i].c_str())) {
                        AddEditorComponentByType(entity, i);
                        state.addingComponent = false;
                        state.componentToAdd  = CMP_SPRITE;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::PopID();
        }

        // ── Tags ─────────────────────────────────────────────────────────────
        if (InspectorSection(Get("entity.tags"), "Tags", static_cast<int>(entity.tagIds.size()), false)) {
            ImGui::PushID("EntityTags");
            {
                PrefabField prefabField("tags");
                DrawTagsEditor(entity.tagIds);
            }
            ImGui::PopID();
        }

        // ── Events / Dispatches ──────────────────────────────────────────────
        // Each list is one prefab field, overridden as a whole.
        if (InspectorSection(Get("events.title"), "Events", static_cast<int>(entity.events.size()), false)) {
            ImGui::PushID("Events");
            {
                PrefabField prefabField("events");
                DrawEventsEditor(entity.events, EventOwnerKind::ENTITY, entity.id);
            }
            ImGui::PopID();
        }

        if (InspectorSection(Get("dispatches.title"), "Dispatches", static_cast<int>(entity.dispatches.size()), false)) {
            ImGui::PushID("Dispatches");
            {
                PrefabField prefabField("dispatches");
                DrawDispatchesEditor(entity.dispatches, EventOwnerKind::ENTITY, entity.id, draggable);
            }
            ImGui::PopID();
        }

        // Runs a Revert/Apply picked from the bar, a card or a field.
        EndPrefabFields();

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

        std::string componentName = GetComponentDisplayName(state.selectedComponent);
        const std::string windowTitle = componentName + "##component_editor" + state.windowIdSuffix;

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
        ImGui::PushID(std::to_string(state.selectedInstanceID).c_str());

        BeginPrefabFields(entity, state.selectedComponent, state.selectedInstanceID);

        bool closeRequested = false;
        auto CloseEditor    = [&]() { closeRequested = true; };

        // Sprite/Model/Collider/AudioSource sit at an offset from the Transform.
        auto DrawOffsetSection = [&](Vector3 &offset, const char* tooltipKey = "editor.tooltip.component.offset", const float speed = 1.0f) {
            {
                PrefabField prefabField("offset");
                BeginSection(Get("component.offset").c_str());
                ImGui::TextDisabled("X                Y               Z");
                FieldWidth(220.0f);
                InputOrDrag3("##offset", &offset.x, draggable, speed);
                Tooltip(Get(tooltipKey).c_str());
                ResetFloat3Button("rst_offset", &offset.x);
                EndSection();
            }
        };

        // ── Summary header ────────────────────────────────────────────────────
        DrawInspectorHeader("Component", componentName.c_str());

        if (entity.prefabLink.IsInstance() && state.selectedComponent != CMP_TRANSFORM &&
            Prefab::PrefabInstanceIDOf(entity, state.selectedComponent, state.selectedInstanceID) == 0)
            ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "+ Added to this instance (not in the prefab)");

        // ════════════════════════════════════════════════════════════════════
        //  Transform
        // ════════════════════════════════════════════════════════════════════
        if (state.selectedComponent == CMP_TRANSFORM) {
            auto* c = entity.GetComponent<ComponentTransform>();

            if (c) {
                BeginSection("Position");
                ImGui::TextDisabled("X                Y               Z");
                FieldWidth(220.0f);

                if (InputOrDrag3("##pos", &c->position.x, draggable)) c->isDirty = true;

                ResetFloat3Button("rst_pos", &c->position.x);
                EndSection();

                BeginSection("Rotation");
                ImGui::TextDisabled("X                Y               Z");

                static ID rotationEditorOwner = INVALID_ENTITY_ID;
                static Vector3 rotationEuler = {0.0f, 0.0f, 0.0f};

                // Re-read too when something else (the runtime editor's rotate
                // gizmo, a script) changed the rotation, or the next edit here
                // would write the stale angles back over it. Two quaternions
                // are the same rotation when |dot| is 1.
                const Quaternion shownRotation = Quaternion::FromEulerDegrees(rotationEuler.x, rotationEuler.y, rotationEuler.z);
                const Quaternion actualRotation = c->rotation.Normalized();
                const float rotationDot =
                    shownRotation.x * actualRotation.x + shownRotation.y * actualRotation.y +
                    shownRotation.z * actualRotation.z + shownRotation.w * actualRotation.w;

                if (rotationEditorOwner != c->ownerID || std::abs(rotationDot) < 0.99999f) {
                    rotationEditorOwner = c->ownerID;
                    rotationEuler = c->rotation.ToEulerDegrees();
                }

                FieldWidth(220.0f);

                if (InputOrDrag3("##rotation", &rotationEuler.x, draggable)) {
                    c->rotation = Quaternion::FromEulerDegrees(rotationEuler.x, rotationEuler.y, rotationEuler.z);
                    c->isDirty = true;
                }

                // ImGui::SameLine();
                //
                // if (ImGui::Button("Reset##rst_rotation")) {
                //     rotationEuler = {0.0f, 0.0f, 0.0f};
                //     c->rotation = Quaternion::Identity();
                //     c->isDirty = true;
                // }

                ResetFloat3Button("##rotation", &c->rotation.x);

                EndSection();

                {
                    PrefabField prefabField("scale");
                    BeginSection("Scale");
                    ImGui::TextDisabled("X                Y               Z");
                    FieldWidth(220.0f);

                    if (InputOrDrag3("##scale", &c->scale.x, draggable)) c->isDirty = true;

                    ResetFloat3Button("rst_scale", &c->scale.x);
                    EndSection();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentTransform>();
                    CloseEditor();
                }
            }
            else ImGui::TextDisabled("Transform component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Sprite
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_SPRITE) {
            auto *c = entity.GetComponentInstance<ComponentSprite>(state.selectedInstanceID);

            if (c) {
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

                {
                    PrefabField prefabField("sideCount");
                    ImGui::Text("%s", Get("component.sprite.directional").c_str());
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(160.0f);

                    if (ImGui::Combo("##sprite_direction_mode", &directionMode, directionModes, 3)) {
                        if (directionMode == 0) c->sideCount = SIDECOUNT_SINGLE;
                        else if (directionMode == 1) c->sideCount = SIDECOUNT_90;
                        else if (directionMode == 2) c->sideCount = SIDECOUNT_45;
                    }
                }

                ImGui::Spacing();

                {
                    PrefabField prefabField("textureFileNames");
                    if (ImGui::SmallButton("Clear All")) for (std::string& name : c->textureFileNames) name.clear();


                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    DrawDirectionalTextureSlots(c->textureFileNames, c->sideCount);
                }

                EndSection();

                ImGui::Spacing();

                {
                    PrefabField prefabField("color");
                    ImGui::Text("%s", Get("component.sprite.color").c_str());
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(160.0f);

                    if (ImGui::ColorEdit4(
                        Get("wall.color").c_str(),
                        &c->color.x,
                        ImGuiColorEditFlags_AlphaBar |
                        ImGuiColorEditFlags_AlphaPreviewHalf |
                        ImGuiColorEditFlags_HDR |
                        ImGuiColorEditFlags_Float
                    )) {}
                }

                ImGui::Spacing();

                {
                    PrefabField prefabField("isStatic");
                    ImGui::Checkbox(Get("component.sprite.is_static").c_str(), &c->isStatic);
                    Tooltip(Get("editor.tooltip.component.sprite.is_static").c_str());
                }
                {
                    PrefabField prefabField("isActive");
                    ImGui::Checkbox(Get("component.sprite.is_active").c_str(), &c->isActive);
                    Tooltip(Get("editor.tooltip.component.sprite.is_active").c_str());
                }

                DrawOffsetSection(c->offset);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentSprite>(state.selectedInstanceID);
                    CloseEditor();
                }
            }
            else ImGui::TextDisabled("Sprite component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Audio Source
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_AUDIO_SOURCE) {
            auto *c = entity.GetComponentInstance<ComponentAudioSource>(state.selectedInstanceID);
            if (c) {
                BeginSection("Sound");
                {
                    PrefabField prefabField("soundFileName");
                    MapEditorInternal::DrawAssetField("Sound", c->soundFileName, AssetKind::Sound);
                    Tooltip(Get("editor.tooltip.component.audio_source.sound").c_str());
                }
                FieldWidth(120.0f);
                {
                    PrefabField prefabField("pitch");
                    InputOrDrag(Get("component.audio_source.pitch").c_str(), &c->pitch, draggable, 0.01f);
                }
                FieldWidth(120.0f);
                {
                    PrefabField prefabField("gain");
                    InputOrDrag(Get("component.audio_source.gain").c_str(),  &c->gain,  draggable, 0.01f);
                }
                {
                    PrefabField prefabField("looping");
                    ImGui::Checkbox(Get("component.audio_source.looping").c_str(),       &c->looping);
                }
                {
                    PrefabField prefabField("playOnStart");
                    ImGui::Checkbox(Get("component.audio_source.play_on_start").c_str(), &c->playOnStart);
                }
                EndSection();

                BeginSection("Attenuation");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("referenceDistance");
                    InputOrDrag(Get("component.audio_source.ref_distance").c_str(), &c->referenceDistance, draggable, 0.1f);
                    Tooltip(Get("editor.tooltip.component.audio_source.ref_distance").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("maxDistance");
                    InputOrDrag(Get("component.audio_source.max_distance").c_str(), &c->maxDistance,       draggable, 1.0f);
                    Tooltip(Get("editor.tooltip.component.audio_source.max_distance").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("rollOffFactor");
                    InputOrDrag(Get("component.audio_source.rolloff").c_str(),       &c->rollOffFactor,     draggable, 0.01f);
                    Tooltip(Get("editor.tooltip.component.audio_source.rolloff").c_str());
                }
                EndSection();

                BeginSection("Cone");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("innerConeAngle");
                    InputOrDrag(Get("component.audio_source.inner_angle").c_str(), &c->innerConeAngle, draggable, 1.0f);
                    Tooltip(Get("editor.tooltip.component.audio_source.inner_angle").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("outerConeAngle");
                    InputOrDrag(Get("component.audio_source.outer_angle").c_str(), &c->outerConeAngle, draggable, 1.0f);
                    Tooltip(Get("editor.tooltip.component.audio_source.outer_angle").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("outerGain");
                    InputOrDrag(Get("component.audio_source.outer_gain").c_str(),  &c->outerGain,      draggable, 0.01f);
                }
                EndSection();

                DrawOffsetSection(c->offset);

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentAudioSource>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Audio component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Script
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_SCRIPT) {
            // Looks up the specific instance the row list's Edit button
            // selected (see DrawEntityEditor's per-script row loop) rather
            // than always the first-by-owner script - an Entity may have
            // several scripts attached.
            auto *c = entity.GetScript(state.selectedInstanceID);

            if (c) {
                DrawScriptAttachmentFields(*c, "entity " + std::to_string(entity.id));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveScript(state.selectedInstanceID);
                    state.selectedInstanceID = INVALID_SCRIPT_INSTANCE_ID;
                    CloseEditor();
                }
            }
            else ImGui::TextDisabled("Script component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Player Controller
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_PLAYER_CONTROLLER) {
            auto *c = entity.GetComponentInstance<ComponentPlayerController>(state.selectedInstanceID);
            if (c) {
                BeginSection("Movement");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("speed");
                    InputOrDrag(Get("component.player_controller.speed").c_str(), &c->speed, draggable);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("runningSpeed");
                    InputOrDrag(Get("component.player_controller.running_speed").c_str(), &c->runningSpeed, draggable);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("acceleration");
                    InputOrDrag(Get("component.player_controller.acceleration").c_str(), &c->acceleration, draggable);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("deceleration");
                    InputOrDrag(Get("component.player_controller.deceleration").c_str(), &c->deceleration, draggable);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("airControl");
                    InputOrDrag(Get("component.player_controller.air_control").c_str(), &c->airControl, draggable);
                }

                EndSection();

                BeginSection("Jumping");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("jumpPower");
                    InputOrDrag(Get("component.player_controller.jump_power").c_str(), &c->jumpPower, draggable);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("jumpBufferMs");
                    InputOrDrag(Get("component.player_controller.jump_buffer_ms").c_str(), &c->jumpBufferMs, draggable);
                    Tooltip(Get("editor.tooltip.component.player_controller.jump_buffer").c_str());
                }
                EndSection();

                BeginSection("Camera");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("eyeHeight");
                    InputOrDrag(Get("component.player_controller.eye_height").c_str(), &c->eyeHeight, draggable);
                    Tooltip(Get("editor.tooltip.component.player_controller.eye_height").c_str());
                }
                //FieldWidth(-1.0f);
                {
                    PrefabField prefabField("sensitivityX");
                    ImGui::SliderFloat(Get("component.player_controller.sensitivity_x").c_str(),
                        &c->sensitivityX, 0.001f, 2.0f);
                }
                //FieldWidth(-1.0f);
                {
                    PrefabField prefabField("sensitivityY");
                    ImGui::SliderFloat(Get("component.player_controller.sensitivity_y").c_str(),
                                       &c->sensitivityY, 0.001f, 2.0f);
                }
                //FieldWidth(-1.0f);
                {
                    PrefabField prefabField("maxPitch");
                    ImGui::SliderFloat(Get("component.player_controller.max_pitch").c_str(),
                                       &c->maxPitch, 0.0f, 360.0f);
                }
                //FieldWidth(-1.0f);
                {
                    PrefabField prefabField("minPitch");
                    ImGui::SliderFloat(Get("component.player_controller.min_pitch").c_str(),
                                       &c->minPitch, 0.0f, 360.0f);
                }
                //FieldWidth(-1.0f);
                {
                    PrefabField prefabField("maxYaw");
                    ImGui::SliderFloat(Get("component.player_controller.max_yaw").c_str(),
                                       &c->maxYaw, 0.0f, 360.0f);
                }
                //FieldWidth(-1.0f);
                {
                    PrefabField prefabField("minYaw");
                    ImGui::SliderFloat(Get("component.player_controller.min_yaw").c_str(),
                                       &c->minYaw, 0.0f, 360.0f);
                }
                if (c->minYaw < .0f) c->minYaw = 0.0f;

                EndSection();

                BeginSection("Use");
                {
                    PrefabField prefabField("useKey");

                    // Click, then press a key; Escape or a second click cancels,
                    // right click clears it (no Use key).
                    SDL_Scancode *key = &c->useKey;
                    const bool listening = MapEditorInternal::keyBindBeingListened == key;

                    if (listening) {
                        const SDL_Scancode pressed = InputManager::GetAnyKeyDown();

                        if (pressed == SDL_SCANCODE_ESCAPE) MapEditorInternal::keyBindBeingListened = nullptr;
                        else if (pressed != SDL_SCANCODE_UNKNOWN) {
                            *key = pressed;
                            MapEditorInternal::keyBindBeingListened = nullptr;
                        }
                    }

                    const std::string keyLabel = (listening
                        ? Get("editor.user_settings.key_listening")
                        : *key == SDL_SCANCODE_UNKNOWN
                            ? Get("editor.user_settings.key_none")
                            : InputManager::ScancodeToString(*key)) + "##UseKey";

                    if (ImGui::Button(keyLabel.c_str(), ImVec2(160.0f, 0.0f)))
                        MapEditorInternal::keyBindBeingListened = listening ? nullptr : key;
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        *key = SDL_SCANCODE_UNKNOWN;
                        if (listening) MapEditorInternal::keyBindBeingListened = nullptr;
                    }

                    ImGui::SameLine();
                    ImGui::TextUnformatted(Get("component.player_controller.use_key").c_str());
                    Tooltip(Get("editor.tooltip.component.player_controller.use_key").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("useReach");
                    if (InputOrDrag(Get("component.player_controller.use_reach").c_str(), &c->useReach, draggable))
                        c->useReach = std::max(0.0f, c->useReach);
                    Tooltip(Get("editor.tooltip.component.player_controller.use_reach").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("useRayType");
                    DrawEnumCombo(Get("component.player_controller.use_ray_type").c_str(), c->useRayType, kRayTypeOptionsWithGeneral);
                    Tooltip(Get("editor.tooltip.component.player_controller.use_ray_type").c_str());
                }
                EndSection();

                BeginSection("State");
                {
                    PrefabField prefabField("isActive");
                    if (ImGui::Checkbox(Get("component.player_controller.is_active").c_str(), &c->isActive) && c->isActive)
                        LevelManager::CurrentLevel().ActivatePlayerController(*c);
                    Tooltip(Get("editor.tooltip.component.player_controller.is_active").c_str());
                }
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentPlayerController>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Player Controller component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Camera
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_CAMERA) {
            auto *c = entity.GetComponentInstance<ComponentCamera>(state.selectedInstanceID);
            if (c) {
                BeginSection("Projection");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("fov");
                    ImGui::SliderFloat(Get("component.camera.fov").c_str(), &c->fov, 1.0f, 179.0f);
                    Tooltip(Get("editor.tooltip.component.camera.fov").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("aspectRatio");
                    InputOrDrag(Get("component.camera.aspect_ratio").c_str(), &c->aspectRatio, draggable);
                }
                EndSection();

                BeginSection("Clipping Planes");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("nearPlane");
                    InputOrDrag(Get("component.camera.near_plane").c_str(), &c->nearPlane, draggable);
                    Tooltip(Get("editor.tooltip.component.camera.near_plane").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("farPlane");
                    InputOrDrag(Get("component.camera.far_plane").c_str(),  &c->farPlane,  draggable);
                    Tooltip(Get("editor.tooltip.component.camera.far_plane").c_str());
                }
                EndSection();

                BeginSection("Gameplay");
                {
                    PrefabField prefabField("smoothStep");
                    ImGui::Checkbox(Get("component.camera.smooth_step").c_str(), &c->smoothStep);
                    Tooltip(Get("editor.tooltip.component.camera.smooth_step").c_str());
                }
                {
                    PrefabField prefabField("smoothingStrength");
                    InputOrDrag(Get("component.camera.smoothing_strength").c_str(), &c->smoothingStrength, draggable);
                }

                BeginSection("State");
                {
                    PrefabField prefabField("isActive");
                    if (ImGui::Checkbox(Get("component.camera.is_active").c_str(), &c->isActive) && c->isActive)
                        LevelManager::CurrentLevel().ActivateCamera(*c);
                    Tooltip(Get("editor.tooltip.component.camera.is_active").c_str());
                }
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentCamera>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Camera component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Collider
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_COLLIDER) {
            auto *c = entity.GetComponentInstance<ComponentCollider>(state.selectedInstanceID);
            if (c) {
                BeginSection("Shape");

                // In ColliderType order, so the index is the type.
                const std::array<std::string, 3> typeLabels = {
                    Get("component.collider.type.sphere"),
                    Get("component.collider.type.box"),
                    Get("component.collider.type.capsule")
                };
                const char *items[] = { typeLabels[0].c_str(), typeLabels[1].c_str(), typeLabels[2].c_str() };
                int selectedColliderIndex = static_cast<int>(c->type);

                {
                    PrefabField prefabField("type");
                    FieldWidth(140.0f);
                    if (ImGui::Combo(Get("component.collider.type").c_str(),
                                     &selectedColliderIndex, items, IM_ARRAYSIZE(items))) {
                        // Type decides where the collider sits in the storage, so it goes
                        // through SetType. That can move the component, so fetch it again.
                        LevelManager::CurrentLevel().colliders.SetType(
                            state.selectedInstanceID, static_cast<ColliderType>(selectedColliderIndex));
                        c = entity.GetComponentInstance<ComponentCollider>(state.selectedInstanceID);
                    }
                }

                {
                    PrefabField prefabField("scale");
                    // Relative to the Transform's scale, so small steps.
                    constexpr float COLLIDER_SIZE_SPEED = 0.01f;

                    if (c->type == COLLIDERTYPE_SPHERE) {
                        FieldWidth(120.0f);
                        InputOrDrag(Get("component.collider.sphere.scale").c_str(), &c->scale.x, draggable, COLLIDER_SIZE_SPEED);
                        Tooltip(Get("editor.tooltip.component.collider.sphere_radius").c_str());
                    }
                    else if (c->type == COLLIDERTYPE_CAPSULE) {
                        FieldWidth(120.0f);
                        InputOrDrag(Get("component.collider.sphere.scale").c_str(), &c->scale.x, draggable, COLLIDER_SIZE_SPEED);
                        Tooltip(Get("editor.tooltip.component.collider.capsule_radius").c_str());
                        FieldWidth(120.0f);
                        InputOrDrag(Get("component.collider.capsule.height").c_str(), &c->scale.y, draggable, COLLIDER_SIZE_SPEED);
                        Tooltip(Get("editor.tooltip.component.collider.capsule_height").c_str());
                    }
                    else {
                        ImGui::TextDisabled("X                Y               Z");
                        FieldWidth(220.0f);
                        InputOrDrag3("##collider_scale", &c->scale.x, draggable, COLLIDER_SIZE_SPEED);
                        Tooltip(Get("editor.tooltip.component.collider.box_scale").c_str());
                    }
                }

                EndSection();

                BeginSection("Behaviour");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("stepSize");
                    InputOrDrag(Get("component.collider.step_size").c_str(), &c->stepSize, draggable);
                    Tooltip(Get("editor.tooltip.component.collider.step_size").c_str());
                }
                {
                    PrefabField prefabField("maxSlope");
                    InputOrDrag(Get("component.collider.max_slope").c_str(), &c->maxSlope, draggable);
                    Tooltip(Get("editor.tooltip.component.collider.max_slope").c_str());
                    c->maxSlope = std::clamp(c->maxSlope, 0.0f, 90.0f);
                }
                {
                    PrefabField prefabField("isTrigger");
                    ImGui::Checkbox(Get("component.collider.is_trigger").c_str(), &c->isTrigger);
                    Tooltip(Get("editor.tooltip.component.collider.is_trigger").c_str());
                }
                // Goes through SetActive so physics moves it in or out of the active colliders.
                {
                    PrefabField prefabField("isActive");
                    bool colliderActive = c->isActive;
                    if (ImGui::Checkbox(Get("component.collider.is_active").c_str(), &colliderActive)) {
                        LevelManager::CurrentLevel().colliders.SetActive(state.selectedInstanceID, colliderActive);
                        c = entity.GetComponentInstance<ComponentCollider>(state.selectedInstanceID);
                    }
                }
                EndSection();

                DrawOffsetSection(c->offset, "editor.tooltip.component.collider.offset", 0.01f);

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentCollider>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else ImGui::TextDisabled("Collider component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Rigidbody
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_RIGIDBODY) {
            auto *c = entity.GetComponentInstance<ComponentRigidbody>(state.selectedInstanceID);
            if (c) {
                if (entity.GetComponent<ComponentRigidbody>() != c)
                    ImGui::TextWrapped("%s", Get("component.rigidbody.not_first").c_str());

                if (!entity.HasComponent<ComponentCollider>())
                    ImGui::TextWrapped("%s", Get("component.rigidbody.no_collider").c_str());

                BeginSection("Physics");
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("mass");
                    InputOrDrag(Get("component.rigidbody.mass").c_str(),          &c->mass,         draggable);
                    Tooltip(Get("editor.tooltip.component.rigidbody.mass").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("gravityScale");
                    InputOrDrag(Get("component.rigidbody.gravityScale").c_str(),  &c->gravityScale, draggable);
                    Tooltip(Get("editor.tooltip.component.rigidbody.gravity_scale").c_str());
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("friction");
                    InputOrDrag(Get("component.rigidbody.friction").c_str(),      &c->friction,     draggable);
                }
                {
                    PrefabField prefabField("isStatic");
                    ImGui::Checkbox(Get("component.rigidbody.is_static").c_str(), &c->isStatic);
                    Tooltip(Get("editor.tooltip.component.rigidbody.is_static").c_str());
                }
                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentRigidbody>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Rigidbody component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Model
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_MODEL) {
            auto *c = entity.GetComponentInstance<ComponentModel>(state.selectedInstanceID);
            if (c) {
                // Dependency scan of the selected file. Reading a model is not
                // cheap, so it only reruns when the file changes or on Rescan.
                struct ModelDependencyReport {
                    std::string fileName;
                    bool scanned = false;
                    bool readable = false;
                    std::string error;
                    std::vector<ModelLoader::ModelDependency> dependencies;
                };

                static ModelDependencyReport report;

                BeginSection(Get("component.model.section").c_str());

                {
                    PrefabField prefabField("fileName");
                    MapEditorInternal::DrawAssetField(Get("component.model.file_name").c_str(), c->fileName, AssetKind::Model, 64.0f);
                    Tooltip(Get("editor.tooltip.component.model.file_name").c_str());
                }

                if (c->fileName.empty()) ImGui::TextDisabled("%s", Get("editor.none").c_str());
                else ImGui::TextWrapped("%s", c->fileName.c_str());

                EndSection();

                DrawOffsetSection(c->offset);

                if (!c->fileName.empty()) {
                    const bool rescan = ImGui::SmallButton(Get("component.model.rescan").c_str());

                    if (rescan || !report.scanned || report.fileName != c->fileName) {
                        report = {};
                        report.fileName = c->fileName;
                        report.scanned = true;
                        report.readable = ModelLoader::CollectDependencies(
                            ModelLoader::ResolveModelPath(c->fileName),
                            ProjectManager::GetAssetsPath(),
                            report.dependencies,
                            report.error
                        );
                    }

                    BeginSection(Get("component.model.dependencies").c_str());

                    const ImVec4 missingColor = {0.95f, 0.35f, 0.30f, 1.0f};

                    if (!report.readable) ImGui::TextColored(missingColor, "%s", report.error.c_str());
                    else if (report.dependencies.empty()) ImGui::TextDisabled("%s", Get("component.model.no_dependencies").c_str());

                    for (const ModelLoader::ModelDependency& dependency : report.dependencies) {
                        const bool isTexture = dependency.kind == ModelLoader::DependencyKind::Texture;
                        const std::string label = isTexture
                            ? dependency.reference + "  (" + dependency.materialName + ")"
                            : dependency.reference;

                        switch (dependency.status) {
                            case ModelLoader::DependencyStatus::Found:
                                ImGui::BulletText("%s", label.c_str());
                                Tooltip(dependency.resolvedPath.string().c_str());
                                break;
                            case ModelLoader::DependencyStatus::Embedded:
                                ImGui::BulletText("%s  [%s]", label.c_str(), Get("component.model.embedded").c_str());
                                break;
                            case ModelLoader::DependencyStatus::Missing:
                                ImGui::Bullet();
                                ImGui::TextColored(missingColor, "%s  [%s]", label.c_str(), Get("component.model.missing").c_str());
                                break;
                        }
                    }

                    EndSection();
                }

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentModel>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Model component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Flipbook
        // ════════════════════════════════════════════════════════════════════
        else if (state.selectedComponent == CMP_FLIPBOOK) {
            auto *c = entity.GetComponentInstance<ComponentFlipbook>(state.selectedInstanceID);
            if (c) {
                // On a UI entity (the UI editor opens this same inspector) it
                // drives a UI Sprite, which only ever shows slot 0.
                const bool uiEntity = entity.HasComponent<ComponentUITransform>();

                std::vector<ComponentInstanceID> sprites;
                if (uiEntity) for (const ComponentUISprite *sprite : entity.GetComponents<ComponentUISprite>()) sprites.push_back(sprite->instanceID);
                else for (const ComponentSprite *sprite : entity.GetComponents<ComponentSprite>()) sprites.push_back(sprite->instanceID);

                BeginSection(Get("component.flipbook.section").c_str());

                {
                    PrefabField prefabField("flipbookFileName");
                    MapEditorInternal::DrawAssetField(Get("component.flipbook.file_name").c_str(), c->flipbookFileName, AssetKind::Flipbook);
                    Tooltip(Get("editor.tooltip.component.flipbook.file_name").c_str());
                }

                if (!c->flipbookFileName.empty() && ImGui::SmallButton(Get("component.flipbook.open_editor").c_str()))
                    MapEditorInternal::assetBrowser.RequestOpenFlipbook(ProjectManager::GetAssetsPath() / c->flipbookFileName);

                // Which of the entity's sprites it drives. "First sprite" follows
                // whichever sprite is first, even after reordering.
                int selectedSprite = -1;
                for (int i = 0; i < static_cast<int>(sprites.size()); ++i)
                    if (sprites[i] == c->spriteInstanceID) selectedSprite = i;

                const auto spriteLabel = [&](const int index) {
                    return index < 0
                        ? Get("component.flipbook.first_sprite")
                        : Get(uiEntity ? "editor.ui.sprite.title" : "component.sprite") + " " + std::to_string(index + 1);
                };

                {
                    PrefabField prefabField("spriteInstanceID");
                    FieldWidth(160.0f);
                    if (ImGui::BeginCombo(Get("component.flipbook.sprite").c_str(), spriteLabel(selectedSprite).c_str())) {
                        if (ImGui::Selectable(spriteLabel(-1).c_str(), selectedSprite == -1))
                            c->spriteInstanceID = INVALID_COMPONENT_INSTANCE_ID;

                        for (int i = 0; i < static_cast<int>(sprites.size()); ++i) {
                            ImGui::PushID(i);
                            if (ImGui::Selectable(spriteLabel(i).c_str(), selectedSprite == i))
                                c->spriteInstanceID = sprites[i];
                            ImGui::PopID();
                        }

                        ImGui::EndCombo();
                    }
                    Tooltip(Get("editor.tooltip.component.flipbook.sprite").c_str());
                }

                FieldWidth(120.0f);
                {
                    PrefabField prefabField("speed");
                    InputOrDrag(Get("component.flipbook.speed").c_str(), &c->speed, draggable, 0.01f);
                    Tooltip(Get("editor.tooltip.component.flipbook.speed").c_str());
                }

                EndSection();

                // What the file holds, and anything that will show up blank in game.
                BeginSection(Get("component.flipbook.info").c_str());

                const ImVec4 warningColor = {0.95f, 0.65f, 0.30f, 1.0f};
                const FlipbookAsset *asset = c->flipbookFileName.empty() ? nullptr : FlipbookLibrary::Get(c->flipbookFileName);

                if (sprites.empty()) ImGui::TextColored(warningColor, "%s", Get("component.flipbook.no_sprite").c_str());

                if (c->flipbookFileName.empty()) ImGui::TextDisabled("%s", Get("editor.none").c_str());
                else if (asset == nullptr) ImGui::TextColored(warningColor, "%s", Get("component.flipbook.missing_file").c_str());
                else {
                    const char *loopModeKeys[] = {
                        "flipbook_editor.loop_mode.once", "flipbook_editor.loop_mode.loop", "flipbook_editor.loop_mode.ping_pong"
                    };

                    ImGui::Text("%s: %d", Get("flipbook_editor.frames").c_str(), static_cast<int>(asset->frames.size()));
                    ImGui::Text("%s: %.2f", Get("flipbook_editor.fps").c_str(), asset->fps);
                    ImGui::Text("%s: %s", Get("flipbook_editor.loop_mode").c_str(), Get(loopModeKeys[static_cast<int>(asset->loopMode)]).c_str());
                    if (asset->playOnStart) ImGui::TextUnformatted(Get("flipbook_editor.play_on_start").c_str());

                    // The slots the driven sprite actually draws: a UI Sprite
                    // only slot 0, a Sprite whatever its side count uses.
                    if (!sprites.empty()) {
                        static constexpr int SINGLE_SLOTS[] = {0};
                        static constexpr int SIDE_90_SLOTS[] = {0, 2, 4, 6};
                        static constexpr int SIDE_45_SLOTS[] = {0, 1, 2, 3, 4, 5, 6, 7};
                        static constexpr const char *SIDE_NAMES[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

                        std::span<const int> usedSlots = SINGLE_SLOTS;

                        if (!uiEntity) {
                            const ComponentInstanceID targetID = sprites[std::max(selectedSprite, 0)];
                            const ComponentSprite *target = entity.GetComponentInstance<ComponentSprite>(targetID);

                            if (target != nullptr && target->sideCount == SIDECOUNT_90) usedSlots = SIDE_90_SLOTS;
                            else if (target != nullptr && target->sideCount == SIDECOUNT_45) usedSlots = SIDE_45_SLOTS;
                        }

                        for (const FlipbookFrame &frame : asset->frames) {
                            std::string missing;

                            for (const int slot : usedSlots)
                                if (frame.textures[slot].empty()) missing += std::string(missing.empty() ? "" : ", ") + SIDE_NAMES[slot];

                            if (!missing.empty())
                                ImGui::TextColored(warningColor, "%s '%s': %s", Get("component.flipbook.missing_sides").c_str(),
                                                   frame.name.c_str(), missing.c_str());
                        }
                    }
                }

                EndSection();

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (DangerButton(Get("common.delete").c_str())) {
                    entity.RemoveComponentInstance<ComponentFlipbook>(state.selectedInstanceID);
                    CloseEditor();
                }
            } else { ImGui::TextDisabled("Flipbook component missing"); }
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
        ImGui::PopID();
        ImGui::End();

        EndPrefabFields();
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
    namespace {
        struct Toast {
            char message[128];
            float remainingSeconds;
            bool isError;
        };

        Toast s_toast{};

        // The window's `pivot` point goes at `position`.
        void DrawToast(const float dt, const ImVec2 position, const ImVec2 pivot) {
            if (s_toast.remainingSeconds <= 0.0f) return;
            s_toast.remainingSeconds -= dt;

            ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
            ImGui::SetNextWindowBgAlpha(0.88f);

            constexpr ImGuiWindowFlags flags =
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoNav |
                    ImGuiWindowFlags_AlwaysAutoResize;

            ImGui::Begin("##Toast", nullptr, flags);

            const ImVec4 col = s_toast.isError
                                   ? ImVec4(1.00f, 0.45f, 0.45f, 1.00f)
                                   : ImVec4(0.55f, 1.00f, 0.65f, 1.00f);

            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Text("  %s  ", s_toast.message);
            ImGui::PopStyleColor();

            ImGui::End();
        }
    }

    void ShowNotification(const char *msg, const bool isError, const float duration) {
        std::strncpy(s_toast.message, msg, sizeof(s_toast.message) - 1);
        s_toast.message[sizeof(s_toast.message) - 1] = '\0';
        s_toast.remainingSeconds = duration;
        s_toast.isError = isError;
    }

    void DrawNotification(const float dt) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        constexpr float margin = 16.0f;

        DrawToast(dt, ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - margin), ImVec2(0.5f, 1.0f));
    }

    void DrawNotificationBelow(const float dt, const float topY) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();

        DrawToast(dt, ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, topY), ImVec2(0.5f, 0.0f));
    }

    void SetImGuiFocus(const bool focus) {
        ImGuiIO &io = ImGui::GetIO();
        if (focus) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        else       io.ConfigFlags |=  ImGuiConfigFlags_NoMouse;
    }

} // namespace ImGuiDrawFunctions

namespace ImGuiDrawFunctions {
    int DrawHistoryList(const std::vector<std::string>& undoLabels,
                        const std::vector<std::string>& redoLabels,
                        const std::string& initialLabel) {
        int clicked = -1;

        const int done = static_cast<int>(undoLabels.size());
        const int total = done + static_cast<int>(redoLabels.size());

        if (ImGui::BeginChild("##HistoryList")) {
            for (int position = total; position >= 0; --position) {
                const bool redoRow = position > done;
                const std::string& label = position == 0 ? initialLabel
                                         : redoRow ? redoLabels[position - done - 1]
                                         : undoLabels[position - 1];

                ImGui::PushID(position);
                if (redoRow) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

                if (ImGui::Selectable(label.c_str(), position == done) && position != done) clicked = position;

                if (redoRow) ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        return clicked;
    }
}
