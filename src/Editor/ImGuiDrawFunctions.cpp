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
#include <cctype>
#include <cfloat>
#include <map>
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
#include "Headers/Runtime/Gameplay/ParticleSystem.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"
#include "Headers/Objects/FlipbookAsset.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Objects/EventFields.hpp"
#include "Headers/Runtime/EventSystem.hpp"
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

    // Set from EntityInspectorState each time the entity inspector draws.
    static bool particlePreviewAllowed = false;

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

    template<typename T>
    static void RemoveEditorComponent(Entity &entity, const std::uint64_t instanceID) {
        if constexpr (std::is_same_v<T, ComponentScript>) entity.RemoveScript(instanceID);
        else if constexpr (IsSingleComponent<T>) entity.RemoveComponent<T>();
        else entity.RemoveComponentInstance<T>(instanceID);
    }

    static void RemoveEditorComponentByType(Entity &entity, const int componentType, const std::uint64_t instanceID) {
        switch (componentType) {
    #define REMOVE_EDITOR_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    case Bit: RemoveEditorComponent<Type>(entity, instanceID); break;
            TILKY_COMPONENTS(REMOVE_EDITOR_COMPONENT_CASE)
    #undef REMOVE_EDITOR_COMPONENT_CASE
            default: break;
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
    // the Map Editor's hierarchy, or of the object dragged straight off the
    // canvas (see SubmitCanvasEntityDragSource / SubmitCanvasRefDragSource).
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
    // undoes what the press did on the way here (an entity or wall move, a
    // box selection, a started drawing) - the user was assigning the
    // object, not editing the map.
    bool AcceptLevelObjectDrop(const char *payloadType, ID &id) {
        if (!ImGui::BeginDragDropTarget()) return false;

        bool changed = false;

        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(payloadType)) {
            MapEditorInternal::LevelObjectDragPayload dropped;
            std::memcpy(&dropped, payload->Data, sizeof(dropped));

            id = dropped.id;
            changed = true;

            if (dropped.fromCanvas) {
                if (std::strcmp(payloadType, MapEditorInternal::ENTITY_REF_PAYLOAD) == 0) MapEditorInternal::RevertCanvasEntityDrag();
                else MapEditorInternal::RevertCanvasRefDrag();
            }
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

    // "Title" or, with a shared window ID, "Title###id".
    static std::string InspectorWindowTitle(const std::string &title, const char *windowId) {
        return windowId == nullptr ? title : title + "###" + windowId;
    }

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

    // The ⋮ button the component cards use; defined further down.
    namespace {
        bool MenuDotsButton(const char *id, float size);
    }

    // What a frame's ⋮ menu asked for this frame; applied after the frames
    // are drawn so the list never changes under the loop.
    struct FrameAction {
        enum class Kind { None, MoveUp, MoveDown, Duplicate, PlacementToAll, Remove } kind = Kind::None;
        size_t index = 0;
    };

    // "Frame 2   brick_wall   0.50 s": a framed, collapsible header with the
    // frame's thumbnail, and a ⋮ menu at its right end that works while it is
    // collapsed.
    static bool FrameHeader(const SurfaceTexture &texture, const size_t index, FrameAction &action) {
        const TextureFrame &frame = texture.frames[index];
        const size_t count = texture.frames.size();
        const ImGuiStyle &style = ImGui::GetStyle();

        // Only a lone frame starts open; with several, the list reads at a glance.
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                                         ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding |
                                         (count == 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0);

        // In the field colours rather than the section blue, so frames read
        // as parts of the surface, not as sections of their own.
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        const bool open = ImGui::TreeNodeEx("##frame", flags);
        ImGui::PopStyleColor(3);

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const ImVec2 afterHeader = ImGui::GetCursorScreenPos();
        const float height = headerMax.y - headerMin.y;
        const bool openMenu = ImGui::IsItemClicked(ImGuiMouseButton_Right);

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        float x = headerMin.x + ImGui::GetTreeNodeToLabelSpacing();

        const float thumb = height - 4.0f;
        if (const ImTextureID preview = MapEditorInternal::GetPreviewTextureID(frame.texture); preview != ImTextureID{})
            drawList->AddImage(preview, ImVec2(x, headerMin.y + 2.0f), ImVec2(x + thumb, headerMin.y + 2.0f + thumb));
        else
            drawList->AddRect(ImVec2(x, headerMin.y + 2.0f), ImVec2(x + thumb, headerMin.y + 2.0f + thumb),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), 2.0f);
        x += thumb + style.ItemInnerSpacing.x * 2.0f;

        char title[64];
        std::snprintf(title, sizeof(title), Get("texture.frame.label").c_str(), static_cast<int>(index) + 1);

        const std::string name = frame.texture.empty()
            ? Get("texture.frame.empty")
            : std::filesystem::path(frame.texture).stem().string();

        char duration[32] = "";
        if (count > 1) std::snprintf(duration, sizeof(duration), "%.2f s", frame.duration);

        const float textY = headerMin.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
        const float right = headerMax.x - height;

        // The frame showing right now (Play, 3D view) has its title in the
        // accent colour.
        const bool showing = count > 1 && static_cast<size_t>(texture.CurrentFrameIndex()) == index;

        drawList->PushClipRect(ImVec2(x, headerMin.y), ImVec2(right, headerMax.y), true);
        drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(showing ? ImGuiCol_CheckMark : ImGuiCol_Text), title);
        x += ImGui::CalcTextSize(title).x + style.ItemSpacing.x * 2.0f;
        drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), name.c_str());

        if (duration[0] != '\0') {
            const float durationX = right - ImGui::CalcTextSize(duration).x - style.ItemSpacing.x;
            drawList->AddText(ImVec2(std::max(durationX, x + ImGui::CalcTextSize(name.c_str()).x + style.ItemSpacing.x), textY),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), duration);
        }
        drawList->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(headerMax.x - height, headerMin.y));
        if (MenuDotsButton("##frame_menu", height) || openMenu) ImGui::OpenPopup("frame_menu");
        Tooltip(Get("editor.tooltip.texture.frame.menu").c_str());

        if (ImGui::BeginPopup("frame_menu")) {
            ImGui::TextDisabled("%s", title);
            ImGui::Separator();

            if (ImGui::MenuItem(Get("texture.frame.move_up").c_str(), nullptr, false, index > 0))
                action = {FrameAction::Kind::MoveUp, index};
            if (ImGui::MenuItem(Get("texture.frame.move_down").c_str(), nullptr, false, index + 1 < count))
                action = {FrameAction::Kind::MoveDown, index};
            if (ImGui::MenuItem(Get("texture.frame.duplicate").c_str()))
                action = {FrameAction::Kind::Duplicate, index};
            if (ImGui::MenuItem(Get("texture.frame.placement_to_all").c_str(), nullptr, false, count > 1))
                action = {FrameAction::Kind::PlacementToAll, index};
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Get("editor.tooltip.texture.frame.placement_to_all").c_str());

            ImGui::Separator();
            if (ImGui::MenuItem(Get("texture.frame.remove").c_str()))
                action = {FrameAction::Kind::Remove, index};
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Get("editor.tooltip.texture.frame.remove").c_str());

            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(afterHeader);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return open;
    }

    // Duration with its unit, as an input or a drag like every other field.
    static bool DurationField(const char *id, float &seconds, const bool draggable) {
        const bool changed = draggable
            ? ImGui::DragFloat(id, &seconds, 0.01f, 0.0f, FLT_MAX, "%.2f s", ImGuiSliderFlags_AlwaysClamp)
            : ImGui::InputFloat(id, &seconds, 0.0f, 0.0f, "%.2f s");
        if (changed) seconds = std::max(seconds, 0.0f);
        return changed;
    }

    // A surface's texture: the Animation mode, then each frame as its own
    // collapsible section (texture, duration, offset, scale, flips, offset
    // per second), then Add Frame. `drawSettings` adds the surface's own
    // property rows (anchor, color, slope) in a table below. The thumbnail
    // is the Texture Field Size user setting, shrunk to fit the value column.
    template<typename DrawSettings>
    static void TextureBlock(const char *id, SurfaceTexture &texture, const char *tooltipKey, const bool draggable,
                             DrawSettings &&drawSettings) {
        ImGui::PushID(id);

        const char *placementPrefix = std::strcmp(id, "floor") == 0 || std::strcmp(id, "ceiling") == 0 ? "sector" : "wall";

        // ── Animation ────────────────────────────────────────────────────
        if (BeginProperties("##Animation")) {
            const TextureAnimationModeInfo *selected = FindTextureAnimationMode(texture.animation);
            if (selected == nullptr) selected = &TEXTURE_ANIMATION_MODES[0];

            Property(Get("texture.animation").c_str());
            FieldWidth(-FLT_MIN);
            ImGui::BeginDisabled(texture.frames.size() < 2);
            if (ImGui::BeginCombo("##Animation", Get(selected->labelKey).c_str())) {
                for (const TextureAnimationModeInfo &mode: TEXTURE_ANIMATION_MODES) {
                    const bool isSelected = mode.mode == texture.animation;

                    if (ImGui::Selectable(Get(mode.labelKey).c_str(), isSelected)) {
                        texture.animation = mode.mode;
                        texture.Restart();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Get(mode.tooltipKey).c_str());
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            Tooltip(Get(texture.frames.size() < 2 ? "editor.tooltip.texture.animation.needs_frames" : selected->tooltipKey).c_str());

            EndProperties();
        }

        // ── Frames ───────────────────────────────────────────────────────
        // No frames yet: one texture field to drop onto makes the first.
        if (texture.frames.empty() && BeginProperties("##NoFrames")) {
            Property(Get("inspector.texture").c_str());

            const float thumbnail = std::min(MapEditorInternal::textureFieldSize, ImGui::GetContentRegionAvail().x);
            std::string first;
            if (MapEditorInternal::DrawAssetField("", first, AssetKind::Texture, thumbnail) && !first.empty())
                texture.AddFrame(std::move(first));
            Tooltip(Get(tooltipKey).c_str());

            EndProperties();
        }

        FrameAction action;

        for (size_t frameIndex = 0; frameIndex < texture.frames.size(); ++frameIndex) {
            ImGui::PushID(static_cast<int>(frameIndex));

            if (FrameHeader(texture, frameIndex, action)) {
                TextureFrame &frame = texture.frames[frameIndex];

                if (BeginProperties("##Frame")) {
                    Property(Get("inspector.texture").c_str());
                    const float thumbnail = std::min(MapEditorInternal::textureFieldSize, ImGui::GetContentRegionAvail().x);
                    MapEditorInternal::DrawAssetField("", frame.texture, AssetKind::Texture, thumbnail);
                    Tooltip(Get(tooltipKey).c_str());

                    Property(Get("texture.frame.duration").c_str());
                    FieldWidth(-FLT_MIN);
                    ImGui::BeginDisabled(texture.frames.size() < 2);
                    DurationField("##Duration", frame.duration, draggable);
                    ImGui::EndDisabled();
                    Tooltip(Get(texture.frames.size() < 2 ? "editor.tooltip.texture.frame.duration_single"
                                                          : "editor.tooltip.texture.frame.duration").c_str());

                    TextureTransformRows(placementPrefix, frame.textureOffset, frame.textureScale,
                                         frame.flipTextureX, frame.flipTextureY, draggable);

                    Property(Get("texture.offset_per_second").c_str());
                    InputOrDrag2("##OffsetPerSecond", &frame.offsetPerSecond.x, draggable, 0.1f);
                    Tooltip(Get("editor.tooltip.texture.offset_per_second").c_str());

                    EndProperties();
                }

                ImGui::Spacing();
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        std::vector<TextureFrame> &frames = texture.frames;
        const auto at = [&frames](const size_t index) { return frames.begin() + static_cast<std::ptrdiff_t>(index); };

        switch (action.kind) {
            case FrameAction::Kind::MoveUp: std::swap(frames[action.index], frames[action.index - 1]); break;
            case FrameAction::Kind::MoveDown: std::swap(frames[action.index], frames[action.index + 1]); break;
            case FrameAction::Kind::Duplicate: frames.insert(at(action.index + 1), frames[action.index]); break;
            case FrameAction::Kind::PlacementToAll:
                for (TextureFrame &frame: frames) frame.CopyPlacement(frames[action.index]);
                break;
            case FrameAction::Kind::Remove:
                frames.erase(at(action.index));
                texture.Restart();
                break;
            case FrameAction::Kind::None: break;
        }

        // A new frame starts as a copy of the last one, open for editing.
        if (!frames.empty()) {
            ImGui::Spacing();
            if (ImGui::Button(Get("texture.frame.add").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                frames.push_back(frames.back());
                ImGui::PushID(static_cast<int>(frames.size() - 1));
                ImGui::GetStateStorage()->SetInt(ImGui::GetID("##frame"), 1);
                ImGui::PopID();
            }
            Tooltip(Get("editor.tooltip.texture.frame.add").c_str());
        }

        // ── The surface's own settings ───────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        if (BeginProperties("##SurfaceSettings")) {
            drawSettings();
            EndProperties();
        }

        ImGui::PopID();
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

        // Normal project tags not assigned yet, in ID (creation) order like
        // the Project Settings list. Event tags only link events.
        std::vector<std::pair<std::string, TagId> > availableTags;
        for (const auto &[name, id]: TagRegistry::GetAllOfKind(TagRegistry::TagKind::NORMAL))
            if (std::ranges::find(tagIds, id) == tagIds.end()) availableTags.emplace_back(name, id);

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
    // An event reaches every dispatch with the same tag. Those link tags are
    // the project's event tags; the payloads' tag filters test normal tags.

    // The last entry of an event menu tag dropdown: opens the New Tag popup.
    // Call inside the combo; true when clicked.
    static bool NewTagEntry(const bool afterTags) {
        if (afterTags) ImGui::Separator();
        return ImGui::Selectable(Get("tags.new_entry").c_str());
    }

    static std::string newTagName;

    // Opens the New Tag popup; call after EndCombo, at the same ID level as
    // DrawNewTagPopup.
    static void OpenNewTagPopup() {
        newTagName.clear();
        ImGui::OpenPopup("##NewTagPopup");
    }

    // Asks for a name and creates a project tag of `kind` with it, saving the
    // project settings like the Project Settings list does. Returns the new
    // tag once created.
    static std::optional<TagId> DrawNewTagPopup(const TagRegistry::TagKind kind) {
        std::optional<TagId> created;
        if (!ImGui::BeginPopup("##NewTagPopup")) return created;

        ImGui::TextUnformatted(Get(kind == TagRegistry::TagKind::EVENT ? "tags.new_event_tag_title" : "tags.new_tag_title").c_str());

        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(220.0f);
        const bool entered = ImGui::InputText("##Name", &newTagName, ImGuiInputTextFlags_EnterReturnsTrue);

        const bool empty = newTagName.empty();
        const bool taken = !empty && TagRegistry::Exists(newTagName);
        if (taken) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Get("tags.notification.duplicate_name").c_str());

        ImGui::BeginDisabled(empty || taken);
        const bool create = ImGui::Button(Get("tags.create").c_str()) || (entered && !empty && !taken);
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(Get("common.cancel").c_str())) ImGui::CloseCurrentPopup();

        if (create && !empty && !taken) {
            created = TagRegistry::Create(newTagName, kind);
            if (created && !MapEditorInternal::SaveProjectSettings())
                ShowNotification(Get("tags.notification.save_failed").c_str(), /*isError=*/true);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        return created;
    }

    // A dropdown of the project tags of one kind, ending with "New tag...".
    // A tag that no longer exists shows as deleted (or as "no tags" while
    // there are none). With `useTag`, it starts with "(no tag)", which turns
    // it off; picking a tag turns it back on.
    static bool DrawTagCombo(const char *label, TagId &tag, const TagRegistry::TagKind kind, bool *useTag = nullptr) {
        const std::vector<std::pair<std::string, TagId> > tags = TagRegistry::GetAllOfKind(kind);

        const auto name = TagRegistry::GetName(tag);
        const char *noTagsKey = kind == TagRegistry::TagKind::EVENT ? "events.no_event_tags" : "events.no_tags";
        const std::string preview = useTag && !*useTag ? Get("events.no_link_tag")
                                    : name ? *name : Get(tags.empty() ? noTagsKey : "tags.missing");

        ImGui::PushID(label);

        bool changed = false;
        bool openNewTag = false;
        if (ImGui::BeginCombo(label, preview.c_str())) {
            if (useTag && ImGui::Selectable(Get("events.no_link_tag").c_str(), !*useTag)) {
                *useTag = false;
                changed = true;
            }

            for (const auto &[tagName, id]: tags) {
                const bool selected = id == tag && (!useTag || *useTag);
                if (ImGui::Selectable(tagName.c_str(), selected)) {
                    tag = id;
                    if (useTag) *useTag = true;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }

            openNewTag = NewTagEntry(!tags.empty());
            ImGui::EndCombo();
        }

        if (openNewTag) OpenNewTagPopup();
        if (const std::optional<TagId> created = DrawNewTagPopup(kind)) {
            tag = *created;
            if (useTag) *useTag = true;
            changed = true;
        }

        ImGui::PopID();
        return changed;
    }

    // For the item just drawn in a dropdown (an option, or an event or
    // dispatch type): the tooltip written as "editor.tooltip.<its key>" in
    // the language file. Nothing when there is none.
    static void OptionTooltip(const std::string &key) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
        if (const std::string *text = Localisation::Find("editor.tooltip." + key)) ImGui::SetTooltip("%s", text->c_str());
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
                OptionTooltip(key);
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
        {PayloadDispatchChangeVariable::SUBTRACT, "events.operation.subtract"},
        {PayloadDispatchChangeVariable::MULTIPLY, "events.operation.multiply"},
        {PayloadDispatchChangeVariable::DIVIDE, "events.operation.divide"}
    };

    constexpr std::pair<LineSide, const char *> kLineSideOptions[] = {
        {LineSide::EITHER, "events.side.either"},
        {LineSide::FRONT, "events.side.front"},
        {LineSide::BACK, "events.side.back"}
    };

    // Click, then press a key; Escape or a second click cancels, right click
    // clears it (no key). `id` keeps the button's ImGui ID unique.
    static void DrawKeyBindButton(const char* id, SDL_Scancode* key, const float width = 160.0f) {
        const bool listening = MapEditorInternal::keyBindBeingListened == key;

        if (listening) {
            MapEditorInternal::componentKeyBindDrawnFrame = ImGui::GetFrameCount();
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
                : InputManager::ScancodeToString(*key)) + "##" + id;

        if (ImGui::Button(keyLabel.c_str(), ImVec2(width, 0.0f))) {
            MapEditorInternal::keyBindBeingListened = listening ? nullptr : key;
            MapEditorInternal::componentKeyBindDrawnFrame = ImGui::GetFrameCount();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            *key = SDL_SCANCODE_UNKNOWN;
            if (listening) MapEditorInternal::keyBindBeingListened = nullptr;
        }
    }

    constexpr std::pair<MapEditorInternal::MakeLiftSettings::Motion, const char *> kMakeLiftMotionOptions[] = {
        {MapEditorInternal::MakeLiftSettings::DOWN_WAIT_UP, "sector.make_lift.motion.down_wait_up"},
        {MapEditorInternal::MakeLiftSettings::UP_WAIT_DOWN, "sector.make_lift.motion.up_wait_down"},
        {MapEditorInternal::MakeLiftSettings::BACK_AND_FORTH, "sector.make_lift.motion.back_and_forth"},
        {MapEditorInternal::MakeLiftSettings::DOWN_STAY, "sector.make_lift.motion.down_stay"},
        {MapEditorInternal::MakeLiftSettings::UP_STAY, "sector.make_lift.motion.up_stay"}
    };

    constexpr std::pair<TeleportTarget, const char *> kTeleportTargetOptions[] = {
        {TeleportTarget::COORDINATES, "events.teleport.to.coordinates"},
        {TeleportTarget::ENTITY, "events.teleport.to.entity"},
        {TeleportTarget::SECTOR, "events.teleport.to.sector"}
    };

    constexpr std::pair<TeleportFacing, const char *> kTeleportFacingOptions[] = {
        {TeleportFacing::SET, "events.teleport.facing.set"},
        {TeleportFacing::KEEP, "events.teleport.facing.keep"},
        {TeleportFacing::TARGET, "events.teleport.facing.target"}
    };

    // The translation key of `value` in an option table.
    template<typename Enum, size_t N>
    static const char *EnumOptionKey(const Enum value, const std::pair<Enum, const char *> (&options)[N]) {
        for (const auto &[option, key]: options) if (option == value) return key;
        return options[0].second;
    }

    constexpr std::pair<KeyEventInput, const char *> kKeyEventInputOptions[] = {
        {KeyEventInput::KEY, "events.key_input.key"},
        {KeyEventInput::MOUSE_LEFT, "events.key_input.mouse_left"},
        {KeyEventInput::MOUSE_RIGHT, "events.key_input.mouse_right"},
        {KeyEventInput::MOUSE_MIDDLE, "events.key_input.mouse_middle"},
        {KeyEventInput::MOUSE_BACK, "events.key_input.mouse_back"},
        {KeyEventInput::MOUSE_FORWARD, "events.key_input.mouse_forward"}
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
        {SectorHeightMode::HIGHEST_NEIGHBOR_CEILING, "events.height_mode.highest_neighbor_ceiling"},
        {SectorHeightMode::NEXT_HIGHER_NEIGHBOR_FLOOR, "events.height_mode.next_higher_neighbor_floor"},
        {SectorHeightMode::NEXT_LOWER_NEIGHBOR_FLOOR, "events.height_mode.next_lower_neighbor_floor"},
        {SectorHeightMode::NEXT_HIGHER_NEIGHBOR_CEILING, "events.height_mode.next_higher_neighbor_ceiling"},
        {SectorHeightMode::NEXT_LOWER_NEIGHBOR_CEILING, "events.height_mode.next_lower_neighbor_ceiling"}
    };

    constexpr std::pair<PayloadDispatchSectorLift::Type, const char *> kLiftTypeOptions[] = {
        {PayloadDispatchSectorLift::DOWN_WAIT_UP, "events.lift.type.down_wait_up"},
        {PayloadDispatchSectorLift::UP_WAIT_DOWN, "events.lift.type.up_wait_down"},
        {PayloadDispatchSectorLift::PERPETUAL, "events.lift.type.perpetual"}
    };

    constexpr std::pair<PayloadDispatchSectorLift::LowMode, const char *> kLiftLowModeOptions[] = {
        {PayloadDispatchSectorLift::LOWEST_NEIGHBOR_FLOOR, "events.lift.low_mode.lowest_neighbor_floor"},
        {PayloadDispatchSectorLift::FIXED_LOW, "events.door.open_mode.fixed_height"}
    };

    constexpr std::pair<PayloadDispatchSectorLift::HighMode, const char *> kLiftHighModeOptions[] = {
        {PayloadDispatchSectorLift::HIGHEST_NEIGHBOR_FLOOR, "events.lift.high_mode.highest_neighbor_floor"},
        {PayloadDispatchSectorLift::FIXED_HIGH, "events.door.open_mode.fixed_height"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::Retrigger, const char *> kLiftRetriggerOptions[] = {
        {PayloadDispatchSectorDoor::RESTART, "events.lift.retrigger.restart"},
        {PayloadDispatchSectorDoor::TOGGLE, "events.door.retrigger.toggle"},
        {PayloadDispatchSectorDoor::IGNORE_WHILE_OPEN, "events.door.retrigger.ignore"}
    };

    constexpr std::pair<PayloadDispatchSectorLightEffect::Type, const char *> kLightEffectTypeOptions[] = {
        {PayloadDispatchSectorLightEffect::FLICKER, "events.light_effect.type.flicker"},
        {PayloadDispatchSectorLightEffect::STROBE, "events.light_effect.type.strobe"},
        {PayloadDispatchSectorLightEffect::GLOW, "events.light_effect.type.glow"},
        {PayloadDispatchSectorLightEffect::FIRE_FLICKER, "events.light_effect.type.fire_flicker"},
        {PayloadDispatchSectorLightEffect::NONE, "events.light_effect.type.none"}
    };

    constexpr std::pair<PayloadDispatchSectorLightEffect::DimMode, const char *> kLightDimModeOptions[] = {
        {PayloadDispatchSectorLightEffect::COLOR, "events.light_effect.dim_mode.color"},
        {PayloadDispatchSectorLightEffect::LOWEST_NEIGHBOR, "events.light_effect.dim_mode.lowest_neighbor"}
    };

    constexpr std::pair<FieldComponent, const char *> kFieldComponentOptions[] = {
        {FieldComponent::TRANSFORM, "component.transform"},
        {FieldComponent::SPRITE, "component.sprite"},
        {FieldComponent::AUDIO_SOURCE, "component.audio_source"},
        {FieldComponent::SCRIPT, "component.script"},
        {FieldComponent::PLAYER_CONTROLLER, "component.player_controller"},
        {FieldComponent::CAMERA, "component.camera"},
        {FieldComponent::COLLIDER, "component.collider"},
        {FieldComponent::RIGIDBODY, "component.rigidbody"},
        {FieldComponent::MODEL, "component.model"},
        {FieldComponent::FLIPBOOK, "component.flipbook"},
        {FieldComponent::UI_TRANSFORM, "component.ui_transform"},
        {FieldComponent::UI_SPRITE, "component.ui_sprite"},
        {FieldComponent::UI_TEXT, "component.ui_text"},
        {FieldComponent::UI_BUTTON, "component.ui_button"},
        {FieldComponent::PATHFINDER, "component.pathfinder"},
        {FieldComponent::MONSTER_AI, "component.monster_ai"},
        {FieldComponent::PARTICLE_EMITTER, "component.particle_emitter"}
    };

    constexpr std::pair<PayloadDispatchEntitySetField::Operation, const char *> kSetFieldNumberOperations[] = {
        {PayloadDispatchEntitySetField::SET, "events.operation.set"},
        {PayloadDispatchEntitySetField::ADD, "events.operation.add"},
        {PayloadDispatchEntitySetField::MULTIPLY, "events.operation.multiply"}
    };

    constexpr std::pair<PayloadDispatchEntitySetField::Operation, const char *> kSetFieldBoolOperations[] = {
        {PayloadDispatchEntitySetField::SET, "events.operation.set"},
        {PayloadDispatchEntitySetField::TOGGLE, "events.toggle.toggle"}
    };

    constexpr std::pair<bool, const char *> kFieldValueSourceOptions[] = {
        {false, "events.value_source.fixed"},
        {true, "events.value_source.variable"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::Type, const char *> kDoorTypeOptions[] = {
        {PayloadDispatchSectorDoor::OPEN_WAIT_CLOSE, "events.door.type.open_wait_close"},
        {PayloadDispatchSectorDoor::OPEN_STAY, "events.door.type.open_stay"},
        {PayloadDispatchSectorDoor::CLOSE_WAIT_OPEN, "events.door.type.close_wait_open"},
        {PayloadDispatchSectorDoor::CLOSE_STAY, "events.door.type.close_stay"},
        {PayloadDispatchSectorDoor::OPEN_CLOSE_TOGGLE, "events.door.type.toggle"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::OpenMode, const char *> kDoorOpenModeOptions[] = {
        {PayloadDispatchSectorDoor::LOWEST_NEIGHBOR_CEILING, "events.door.open_mode.lowest_neighbor_ceiling"},
        {PayloadDispatchSectorDoor::FIXED_HEIGHT, "events.door.open_mode.fixed_height"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::FloorOpenMode, const char *> kDoorFloorOpenModeOptions[] = {
        {PayloadDispatchSectorDoor::LOWEST_NEIGHBOR_FLOOR, "events.door.floor_open_mode.lowest_neighbor_floor"},
        {PayloadDispatchSectorDoor::FIXED_FLOOR_HEIGHT, "events.door.open_mode.fixed_height"}
    };

    constexpr std::pair<PayloadDispatchSectorDoor::Retrigger, const char *> kDoorRetriggerOptions[] = {
        {PayloadDispatchSectorDoor::RESTART, "events.door.retrigger.restart"},
        {PayloadDispatchSectorDoor::TOGGLE, "events.door.retrigger.toggle"},
        {PayloadDispatchSectorDoor::IGNORE_WHILE_OPEN, "events.door.retrigger.ignore"}
    };

    // The same for a Close, Wait, Open door, which closes first.
    constexpr std::pair<PayloadDispatchSectorDoor::Retrigger, const char *> kDoorCloseFirstRetriggerOptions[] = {
        {PayloadDispatchSectorDoor::RESTART, "events.door.retrigger.restart_close"},
        {PayloadDispatchSectorDoor::TOGGLE, "events.door.retrigger.toggle"},
        {PayloadDispatchSectorDoor::IGNORE_WHILE_OPEN, "events.door.retrigger.ignore"}
    };

    constexpr std::pair<ToggleableComponent, const char *> kToggleableComponentOptions[] = {
        {ToggleableComponent::COLLIDER, "component.collider"},
        {ToggleableComponent::SPRITE, "component.sprite"},
        {ToggleableComponent::SCRIPT, "component.script"},
        {ToggleableComponent::CAMERA, "component.camera"},
        {ToggleableComponent::PLAYER_CONTROLLER, "component.player_controller"},
        {ToggleableComponent::UI_SPRITE, "component.ui_sprite"},
        {ToggleableComponent::PATHFINDER, "component.pathfinder"},
        {ToggleableComponent::MONSTER_AI, "component.monster_ai"},
        {ToggleableComponent::PARTICLE_EMITTER, "component.particle_emitter"}
    };

    constexpr std::pair<PathfinderAction, const char *> kPathfinderActionOptions[] = {
        {PathfinderAction::GO_TO_COORDINATES, "events.pathfinder.action.go_to_coordinates"},
        {PathfinderAction::GO_TO_SECTOR, "events.pathfinder.action.go_to_sector"},
        {PathfinderAction::FOLLOW_ENTITY, "events.pathfinder.action.follow_entity"},
        {PathfinderAction::FOLLOW_INSTIGATOR, "events.pathfinder.action.follow_instigator"},
        {PathfinderAction::STOP, "events.pathfinder.action.stop"},
        {PathfinderAction::RESUME, "events.pathfinder.action.resume"},
        {PathfinderAction::PAUSE, "events.pathfinder.action.pause"},
        {PathfinderAction::PICK_NEW_DIRECTION, "events.pathfinder.action.pick_new_direction"}
    };

    constexpr std::pair<MonsterAIAction, const char *> kMonsterAIActionOptions[] = {
        {MonsterAIAction::WAKE_ON_PLAYER, "events.monster_ai.action.wake_on_player"},
        {MonsterAIAction::WAKE_ON_INSTIGATOR, "events.monster_ai.action.wake_on_instigator"},
        {MonsterAIAction::WAKE_ON_ENTITY, "events.monster_ai.action.wake_on_entity"},
        {MonsterAIAction::SLEEP, "events.monster_ai.action.sleep"},
        {MonsterAIAction::HURT, "events.monster_ai.action.hurt"}
    };

    constexpr std::pair<NoiseTarget, const char *> kNoiseTargetOptions[] = {
        {NoiseTarget::PLAYER, "events.noise.target.player"},
        {NoiseTarget::INSTIGATOR, "events.noise.target.instigator"},
        {NoiseTarget::SELF, "events.noise.target.self"}
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
    // `allowNone` starts the list with "(none)", which sets 0.
    static bool DrawEventVariableCombo(const char *label, EventVariableID &id, const float width, const bool allowNone = false) {
        const Level &level = LevelManager::CurrentLevel();
        bool changed = false;

        FieldWidth(width);
        if (!ImGui::BeginCombo(label, EventVariableLabel(level, id).c_str())) return false;

        if (allowNone && ImGui::Selectable(Get("events.variable.none").c_str(), id == 0)) {
            id = 0;
            changed = true;
        }

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

    // Kept out of DrawPayloadTypeCombo's per-type lambda: a std::visit in there
    // instantiates a whole visit table for every payload type, and that many
    // functions made GCC 16.1's LTO write one of them twice ("two or more
    // sections for ...__visit_invoke...") and fail the Release link.
    template<typename Variant>
    static const TagFilter *FilterOfVariant(const Variant &payload) {
        return std::visit([](const auto &p) { return FilterOf(p); }, payload);
    }

    // Whether the owner can have a `Payload`: the owner kind fits, the UI
    // Button events only go on an entity with a UI Button, and the Pathfinder
    // event and dispatch only on one with a Pathfinder.
    template<typename Payload>
    static bool PayloadAllowedOn(const EventOwnerKind kind, const ID ownerID) {
        if ((Payload::owners & OwnerBit(kind)) == 0) return false;

        if constexpr (NeedsUIButton<Payload>)
            return kind == EventOwnerKind::ENTITY && LevelManager::CurrentLevel().ui_buttons.Has(ownerID);

        if constexpr (NeedsPathfinder<Payload>)
            return kind == EventOwnerKind::ENTITY && LevelManager::CurrentLevel().pathfinders.Has(ownerID);

        if constexpr (NeedsMonsterAI<Payload>)
            return kind == EventOwnerKind::ENTITY && LevelManager::CurrentLevel().monsterAIs.Has(ownerID);

        return true;
    }

    // The types an object of `kind` can have. Picking another type resets the
    // fields, except a tag filter both types have.
    template<typename Variant>
    static void DrawPayloadTypeCombo(Variant &payload, const EventOwnerKind kind, const ID ownerID) {
        if (!ImGui::BeginCombo("##Type", Get(GetTranslationKey(payload)).c_str())) return;

        ForEachPayloadType<Variant>([&]<typename Payload>(std::type_identity<Payload>) {
            if (!PayloadAllowedOn<Payload>(kind, ownerID)) return;

            const bool selected = std::holds_alternative<Payload>(payload);
            const bool picked = ImGui::Selectable(Get(Payload::translationKey).c_str(), selected);
            OptionTooltip(Payload::translationKey);
            if (picked && !selected) {
                const TagFilter *oldFilter = FilterOfVariant(payload);
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
    static void DrawAddEntryCombo(const char *placeholderKey, std::vector<Entry> &entries, const EventOwnerKind kind,
                                  const ID ownerID) {
        FieldWidth(ImGui::GetContentRegionAvail().x);
        if (!ImGui::BeginCombo("##Add", Get(placeholderKey).c_str())) return;

        using Variant = decltype(Entry::payload);
        ForEachPayloadType<Variant>([&]<typename Payload>(std::type_identity<Payload>) {
            if (!PayloadAllowedOn<Payload>(kind, ownerID)) return;
            const bool picked = ImGui::Selectable(Get(Payload::translationKey).c_str());
            OptionTooltip(Payload::translationKey);
            if (!picked) return;

            Entry entry;
            entry.payload = Payload{};

            // The first event tag, so a new entry starts on a real link.
            const auto eventTags = TagRegistry::GetAllOfKind(TagRegistry::TagKind::EVENT);
            if (!eventTags.empty()) entry.tagId = eventTags.front().second;

            // A new dispatch listens on nothing until a tag is picked.
            if constexpr (std::is_same_v<Entry, Dispatch>) entry.useTag = false;

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
        for (const auto &[name, id]: TagRegistry::GetAllOfKind(TagRegistry::TagKind::NORMAL))
            if (std::ranges::find(filter.tags, id) == filter.tags.end()) availableTags.emplace_back(name, id);

        FieldWidth(-FLT_MIN);
        bool openNewTag = false;
        if (ImGui::BeginCombo("##AddFilterTag", Get("events.filter_tag_add").c_str())) {
            for (const auto &[name, id]: availableTags)
                if (ImGui::Selectable(name.c_str())) filter.tags.push_back(id);

            openNewTag = NewTagEntry(!availableTags.empty());
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.events.filter_tags").c_str());

        if (openNewTag) OpenNewTagPopup();
        if (const std::optional<TagId> created = DrawNewTagPopup(TagRegistry::TagKind::NORMAL))
            filter.tags.push_back(*created);

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

        // The card is drawn in the frame colour, which would hide the
        // checkboxes, text fields and dropdowns on it, so inside it they take
        // the panel's background instead.
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        return visible;
    }

    static void EndEntryCard() {
        ImGui::PopStyleColor();
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

    // "Type  ->  Tag" (no arrow when the tag is off), then " + 2 calls".
    static std::string EntryHeader(const char *translationKey, const bool useTag, const TagId tag, const size_t calls,
                                   const char *idSuffix) {
        std::string header = Get(translationKey);

        if (useTag) {
            const auto tagName = TagRegistry::GetName(tag);
            header += "  ->  " + (tagName ? *tagName : Get("tags.missing"));
        }

        if (calls == 1) header += "  +  " + Get("events.header.one_call");
        else if (calls > 1) header += "  +  " + std::to_string(calls) + " " + Get("events.header.calls");

        return header + idSuffix;
    }

    // "Name  (Type)  ->  Tag" for a named dispatch, then "  ->  2 next" for its flow.
    static std::string DispatchHeader(const Dispatch &dispatch, const char *idSuffix) {
        const std::string type = EntryHeader(GetTranslationKey(dispatch.payload), dispatch.useTag, dispatch.tagId, 0, "");
        std::string header = dispatch.name.empty() ? type : dispatch.name + "  (" + type + ")";
        if (!dispatch.flows.empty())
            header += "  ->  " + std::to_string(dispatch.flows.size()) + " " + Get("events.header.next");
        return header + idSuffix;
    }

    // Property row: which of the sector's floors, numbered from 1 as in the
    // sector inspector. `allowAny` adds "Any floor" for -1.
    static void DrawFloorIndexRow(int &floorIndex, const ID sectorID, const bool allowAny,
                                  const char *labelKey = "events.floor") {
        const Sector *sector = LevelManager::CurrentLevel().GetSector(sectorID);
        const int floorCount = sector ? static_cast<int>(sector->floors.size()) : 0;

        const auto floorLabel = [](const int index) {
            return index < 0 ? Get("events.floor_any") : Get("events.floor") + " " + std::to_string(index + 1);
        };

        Property(Get(labelKey).c_str());
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

    // Property row: the next level in the project's order, or one by name.
    static void DrawLevelNameRow(std::string &levelName, bool &nextLevel) {
        Property(Get("events.level").c_str());

        const std::string preview = nextLevel ? Get("events.level_next") : levelName.empty() ? Get("events.level_none") : levelName;
        if (ImGui::BeginCombo("##Level", preview.c_str())) {
            if (ImGui::Selectable(Get("events.level_next").c_str(), nextLevel)) nextLevel = true;

            std::vector<std::string> names;
            for (const std::filesystem::path &file: LevelSerialization::ListLevelFiles())
                names.push_back(file.stem().string());
            std::ranges::sort(names);
            names.erase(std::unique(names.begin(), names.end()), names.end());

            for (const std::string &name: names)
                if (ImGui::Selectable(name.c_str(), !nextLevel && name == levelName)) {
                    levelName = name;
                    nextLevel = false;
                }

            if (names.empty()) ImGui::TextDisabled("%s", Get("events.level_none_available").c_str());
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.dispatches.level").c_str());
    }

    // ── Direct calls ───────────────────────────────────────────────────────

    constexpr std::pair<PayloadDispatchSetUIText::Part, const char *> kUITextPartOptions[] = {
        {PayloadDispatchSetUIText::TEXT, "events.ui_text_part.text"},
        {PayloadDispatchSetUIText::PREFIX, "events.ui_text_part.prefix"},
        {PayloadDispatchSetUIText::SUFFIX, "events.ui_text_part.suffix"}
    };

    constexpr std::pair<PayloadDispatchMusic::Action, const char *> kMusicActionOptions[] = {
        {PayloadDispatchMusic::PLAY, "events.music.action.play"},
        {PayloadDispatchMusic::STOP, "events.music.action.stop"},
        {PayloadDispatchMusic::PAUSE, "events.music.action.pause"},
        {PayloadDispatchMusic::SWITCH_TO_TRACK, "events.music.action.switch"},
        {PayloadDispatchMusic::SET_VOLUME, "events.music.action.volume"}
    };

    constexpr std::pair<bool, const char *> kRelayModeOptions[] = {
        {false, "events.relay_mode.tag"},
        {true, "events.relay_mode.direct"}
    };

    // A prefab can't point at the objects of a level.
    static bool EditingPrefab() {
        const Level *current = &LevelManager::CurrentLevel();
        return std::ranges::none_of(LevelManager::loadedLevels, [&](const Level &level) { return &level == current; });
    }

    // How the event's instigator reads in the call target dropdown, nullptr
    // for events that have none.
    static const char *InstigatorLabelKey(const EventPayload &payload) {
        return std::visit([]<typename Payload>(const Payload &) -> const char * {
            if constexpr (std::is_same_v<Payload, PayloadSectorEntityEnter>) return "events.call.instigator.entered";
            else if constexpr (std::is_same_v<Payload, PayloadSectorEntityExit>) return "events.call.instigator.left";
            else if constexpr (std::is_same_v<Payload, PayloadSectorEntityStay> ||
                               std::is_same_v<Payload, PayloadSectorEntityGrounded>) return "events.call.instigator.inside";
            else if constexpr (std::is_same_v<Payload, PayloadLineEntityCross>) return "events.call.instigator.crossed";
            else if constexpr (std::is_same_v<Payload, PayloadLineEntityCollide> ||
                               std::is_same_v<Payload, PayloadEntityCollide>) return "events.call.instigator.touched";
            else if constexpr (std::is_same_v<Payload, PayloadReceiveRay>) return "events.call.instigator.ray";
            else if constexpr (std::is_same_v<Payload, PayloadLuaSignal>) return "events.call.instigator.signal";
            else if constexpr (std::is_same_v<Payload, PayloadPathfinderArrived>) return "events.call.instigator.followed";
            else if constexpr (std::is_same_v<Payload, PayloadMonsterTargetSpotted> || std::is_same_v<Payload, PayloadMonsterMeleeReady> ||
                               std::is_same_v<Payload, PayloadMonsterMissileReady> || std::is_same_v<Payload, PayloadMonsterActive>) return "events.call.instigator.monster_target";
            else if constexpr (std::is_same_v<Payload, PayloadMonsterPain> || std::is_same_v<Payload, PayloadMonsterDied>) return "events.call.instigator.attacker";
            else if constexpr (std::is_same_v<Payload, PayloadPathfinderBlocked> || std::is_same_v<Payload, PayloadPathfinderStuck> ||
                               std::is_same_v<Payload, PayloadPathfinderStartedMoving> || std::is_same_v<Payload, PayloadPathfinderStoppedMoving> ||
                               std::is_same_v<Payload, PayloadMonsterTargetLost>) return nullptr;
            else if constexpr (std::is_same_v<Payload, PayloadUIButtonClick> || std::is_same_v<Payload, PayloadUIButtonRelease> ||
                               std::is_same_v<Payload, PayloadUIButtonHoverEnter> ||
                               std::is_same_v<Payload, PayloadUIButtonHoverExit>) return "events.call.instigator.player";
            else if constexpr (std::is_same_v<Payload, PayloadTimer> || std::is_same_v<Payload, PayloadConditionMet> ||
                               std::is_same_v<Payload, PayloadOnStart> ||
                               std::is_same_v<Payload, PayloadKeyPress> || std::is_same_v<Payload, PayloadKeyRelease> ||
                               std::is_same_v<Payload, PayloadSectorMoveFinished>) return nullptr;
            else return "events.call.instigator.relay"; // the entity itself (enable, destroyed, ...)
        }, payload);
    }

    static const char *SelfLabelKey(const EventOwnerKind kind) {
        switch (kind) {
            case EventOwnerKind::LINE: return "events.call.self.wall";
            case EventOwnerKind::SECTOR: return "events.call.self.sector";
            case EventOwnerKind::ENTITY: return "events.call.self.entity";
            case EventOwnerKind::LINEDEF: return "events.call.self.linedef";
            case EventOwnerKind::LEVEL: return "events.call.self.level";
        }
        return "events.call.self.entity";
    }

    static std::string CallTargetLabel(const CallTarget target, const EventOwnerKind callerKind, const char *instigatorKey) {
        switch (target) {
            case CallTarget::SELF: return Get(SelfLabelKey(callerKind));
            case CallTarget::INSTIGATOR: return Get(instigatorKey ? instigatorKey : "events.call.instigator.relay");
            case CallTarget::ENTITY: return Get("events.call.entity");
            case CallTarget::SECTOR: return Get("events.call.sector");
            case CallTarget::WALL: return Get("events.call.wall");
            case CallTarget::LINEDEF: return Get("events.call.linedef");
            case CallTarget::TAGGED: return Get("events.call.tagged");
        }
        return {};
    }

    // "events.call_target.<kind>": the tooltip of a call target, whatever its
    // label says for this event.
    static std::string CallTargetTooltipKey(const CallTarget target) {
        switch (target) {
            case CallTarget::SELF: return "events.call_target.self";
            case CallTarget::INSTIGATOR: return "events.call_target.instigator";
            case CallTarget::ENTITY: return "events.call_target.entity";
            case CallTarget::SECTOR: return "events.call_target.sector";
            case CallTarget::WALL: return "events.call_target.wall";
            case CallTarget::LINEDEF: return "events.call_target.linedef";
            case CallTarget::TAGGED: return "events.call_target.tagged";
        }
        return {};
    }

    static bool IsPickedTarget(const CallTarget target) {
        return target == CallTarget::ENTITY || target == CallTarget::SECTOR || target == CallTarget::WALL ||
               target == CallTarget::LINEDEF;
    }

    // The objects the call reaches that the editor can know: Self, a picked
    // one, or everything with the tag. Empty for the instigator.
    static std::vector<std::pair<EventOwnerKind, ID> > EditorCallTargets(const Level &level, const DirectCall &call,
                                                                         const EventOwnerKind callerKind, const ID callerID) {
        std::vector<std::pair<EventOwnerKind, ID> > targets;
        switch (call.target) {
            case CallTarget::SELF: targets.emplace_back(callerKind, callerID); break;
            case CallTarget::ENTITY: targets.emplace_back(EventOwnerKind::ENTITY, call.targetID); break;
            case CallTarget::SECTOR: targets.emplace_back(EventOwnerKind::SECTOR, call.targetID); break;
            case CallTarget::WALL: targets.emplace_back(EventOwnerKind::LINE, call.targetID); break;
            case CallTarget::LINEDEF: targets.emplace_back(EventOwnerKind::LINEDEF, call.targetID); break;
            case CallTarget::TAGGED: {
                const auto has = [&](const std::vector<TagId> &tags) { return std::ranges::find(tags, call.targetTag) != tags.end(); };
                for (const Entity &entity: level.entities) if (has(entity.tagIds)) targets.emplace_back(EventOwnerKind::ENTITY, entity.id);
                for (const Sector &sector: level.sectors) if (has(sector.tagIds)) targets.emplace_back(EventOwnerKind::SECTOR, sector.id);
                for (const Wall &wall: level.walls) if (has(wall.tagIds)) targets.emplace_back(EventOwnerKind::LINE, wall.id);
                break;
            }
            case CallTarget::INSTIGATOR:
                break;
        }
        return targets;
    }

    static const std::vector<Dispatch> *DispatchesOf(const Level &level, const EventOwnerKind kind, const ID id) {
        switch (kind) {
            case EventOwnerKind::LINE: {
                const auto it = level.wallIDToIndex.find(id);
                return it != level.wallIDToIndex.end() ? &level.walls[it->second].dispatches : nullptr;
            }
            case EventOwnerKind::SECTOR: {
                const Sector *sector = level.GetSector(id);
                return sector ? &sector->dispatches : nullptr;
            }
            case EventOwnerKind::ENTITY: {
                const Entity *entity = level.GetEntity(id);
                return entity ? &entity->dispatches : nullptr;
            }
            case EventOwnerKind::LINEDEF: {
                const Linedef *linedef = level.GetLinedef(id);
                return linedef ? &linedef->dispatches : nullptr;
            }
            case EventOwnerKind::LEVEL: return &level.worldSettings.dispatches;
        }
        return nullptr;
    }

    // Dispatch name -> how many dispatches have it, over the given objects
    // (or, for the instigator, over every entity of the level).
    static std::map<std::string, int> CallableNames(const Level &level, const std::vector<std::pair<EventOwnerKind, ID> > &targets,
                                                    const bool everyEntity) {
        std::map<std::string, int> names;
        const auto count = [&](const std::vector<Dispatch> &dispatches) {
            for (const Dispatch &dispatch: dispatches) if (!dispatch.name.empty()) ++names[dispatch.name];
        };

        if (everyEntity) for (const Entity &entity: level.entities) count(entity.dispatches);
        else for (const auto &[kind, id]: targets) if (const std::vector<Dispatch> *dispatches = DispatchesOf(level, kind, id)) count(*dispatches);

        return names;
    }

    // A searchable dropdown of the level's objects of `kind` (an entity,
    // sector, wall or linedef), which also takes them dragged from the
    // hierarchy or the canvas. The caller puts the Property label first.
    static void DrawLevelObjectPicker(const char *imguiID, const EventOwnerKind kind, ID &targetID) {
        Level &level = LevelManager::CurrentLevel();
        static std::string search;

        std::string preview = Get("events.call.pick_object");
        const char *payloadType = MapEditorInternal::ENTITY_REF_PAYLOAD;
        std::vector<std::pair<ID, std::string> > options;

        switch (kind) {
            case EventOwnerKind::ENTITY:
                for (const Entity &entity: level.entities) options.emplace_back(entity.id, DescribeEntity(entity));
                break;
            case EventOwnerKind::SECTOR:
                payloadType = MapEditorInternal::SECTOR_REF_PAYLOAD;
                for (const Sector &sector: level.sectors) options.emplace_back(sector.id, DescribeSector(sector));
                break;
            case EventOwnerKind::LINE:
                payloadType = MapEditorInternal::WALL_REF_PAYLOAD;
                for (const Wall &wall: level.walls) options.emplace_back(wall.id, DescribeWall(wall));
                break;
            case EventOwnerKind::LINEDEF:
                payloadType = MapEditorInternal::LINEDEF_REF_PAYLOAD;
                for (const Linedef &linedef: level.linedefs) options.emplace_back(linedef.id, DescribeLinedef(linedef));
                break;
            default:
                return;
        }

        if (targetID != INVALID_ID) {
            const auto it = std::ranges::find(options, targetID, &std::pair<ID, std::string>::first);
            preview = it != options.end() ? it->second : Get("events.call.missing_object");
        }

        if (ImGui::BeginCombo(imguiID, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing()) {
                search.clear();
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##Search", Get("events.call.search").c_str(), &search);

            std::string needle = search;
            std::ranges::transform(needle, needle.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

            for (const auto &[id, label]: options) {
                std::string lower = label;
                std::ranges::transform(lower, lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!needle.empty() && lower.find(needle) == std::string::npos) continue;

                ImGui::PushID(static_cast<int>(id));
                if (ImGui::Selectable(label.c_str(), id == targetID)) targetID = id;
                ImGui::PopID();
            }

            if (options.empty()) ImGui::TextDisabled("%s", Get("events.call.no_objects").c_str());
            ImGui::EndCombo();
        }
        AcceptLevelObjectDrop(payloadType, targetID);
    }

    // The object a picked call target (ENTITY, SECTOR, WALL, LINEDEF) names.
    static void DrawCallObjectPicker(DirectCall &call) {
        EventOwnerKind kind;
        switch (call.target) {
            case CallTarget::ENTITY: kind = EventOwnerKind::ENTITY; break;
            case CallTarget::SECTOR: kind = EventOwnerKind::SECTOR; break;
            case CallTarget::WALL: kind = EventOwnerKind::LINE; break;
            case CallTarget::LINEDEF: kind = EventOwnerKind::LINEDEF; break;
            default: return;
        }

        Property("");
        DrawLevelObjectPicker("##Object", kind, call.targetID);
        if (ImGui::GetDragDropPayload() == nullptr) Tooltip(Get("editor.tooltip.events.call_object").c_str());
    }

    // The target, its object or tag, the dispatch name and what will answer.
    // `instigatorKey` is how the event's instigator reads, nullptr if it has
    // none. Sets `remove` when the row's x is clicked (if `removable`).
    static void DrawDirectCallRows(DirectCall &call, const EventOwnerKind callerKind, const ID callerID,
                                   const char *instigatorKey, const bool removable, bool &remove) {
        Level &level = LevelManager::CurrentLevel();
        const bool inPrefab = EditingPrefab();

        // Target.
        Property(Get("events.call").c_str());
        const std::string removeLabel = "x";
        if (removable) FieldWidth(WidthBeforeButtons({removeLabel.c_str()}));

        if (ImGui::BeginCombo("##Target", CallTargetLabel(call.target, callerKind, instigatorKey).c_str())) {
            for (const CallTarget target: {CallTarget::SELF, CallTarget::INSTIGATOR, CallTarget::ENTITY, CallTarget::SECTOR,
                                           CallTarget::WALL, CallTarget::LINEDEF, CallTarget::TAGGED}) {
                if (target == CallTarget::INSTIGATOR && !instigatorKey) continue;
                if (inPrefab && IsPickedTarget(target)) continue;

                if (ImGui::Selectable(CallTargetLabel(target, callerKind, instigatorKey).c_str(), target == call.target) &&
                    target != call.target) {
                    call.target = target;
                    call.targetID = INVALID_ID;
                }
                OptionTooltip(CallTargetTooltipKey(target));
            }
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.events.call_target").c_str());

        if (removable) {
            ImGui::SameLine();
            if (SmallDangerButton(removeLabel.c_str())) remove = true;
            Tooltip(Get("editor.tooltip.events.call_remove").c_str());
        }

        // Which object, or which tag.
        if (IsPickedTarget(call.target)) DrawCallObjectPicker(call);
        else if (call.target == CallTarget::TAGGED) {
            Property("");
            DrawTagCombo("##TargetTag", call.targetTag, TagRegistry::TagKind::NORMAL);
            Tooltip(Get("editor.tooltip.events.call_tag").c_str());
        }

        // Which dispatch: the names the target has, or (for the instigator)
        // the names entities in the level have; any name can be typed.
        const bool runtime = call.target == CallTarget::INSTIGATOR;
        const std::vector<std::pair<EventOwnerKind, ID> > targets = EditorCallTargets(level, call, callerKind, callerID);
        const std::map<std::string, int> names = CallableNames(level, targets, runtime);

        Property(Get("events.call_dispatch").c_str());
        const std::string preview = call.dispatch.empty() ? Get("events.call.pick_dispatch") : call.dispatch;
        if (ImGui::BeginCombo("##Dispatch", preview.c_str())) {
            static std::string typed;
            if (ImGui::IsWindowAppearing()) typed = call.dispatch;

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##Typed", Get("events.call.type_name").c_str(), &typed,
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                call.dispatch = typed;
                ImGui::CloseCurrentPopup();
            }

            for (const auto &[name, count]: names) {
                const std::string label = count > 1 ? name + "  (x" + std::to_string(count) + ")" : name;
                if (ImGui::Selectable(label.c_str(), name == call.dispatch)) call.dispatch = name;
            }

            if (names.empty()) ImGui::TextDisabled("%s", Get("events.call.no_names").c_str());
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.events.call_dispatch").c_str());

        // What will answer.
        if (call.dispatch.empty()) return;

        const ImVec4 warning(1.0f, 0.78f, 0.25f, 1.0f);
        Property("");

        if (runtime) {
            std::vector<std::string> answering;
            for (const Entity &entity: level.entities)
                if (std::ranges::any_of(entity.dispatches, [&](const Dispatch &d) { return d.name == call.dispatch; }))
                    answering.push_back(DescribeEntity(entity));

            if (answering.empty()) {
                ImGui::TextColored(warning, "%s", Get("events.call.hint.no_entity_answers").c_str());
            }
            else {
                std::string list;
                for (size_t i = 0; i < answering.size() && i < 3; ++i) list += (i ? ", " : "") + answering[i];
                if (answering.size() > 3) list += ", +" + std::to_string(answering.size() - 3);
                ImGui::TextDisabled("%s %s", Get("events.call.hint.answered_by").c_str(), list.c_str());
            }
            return;
        }

        const auto it = names.find(call.dispatch);
        const int count = it != names.end() ? it->second : 0;

        if (count == 0) {
            const bool missing = IsPickedTarget(call.target) || call.target == CallTarget::SELF;
            ImGui::TextColored(warning, "%s", Get(missing ? "events.call.hint.missing" : "events.call.hint.nothing_answers").c_str());
        }
        else if (count == 1) ImGui::TextDisabled("%s", Get("events.call.hint.one_runs").c_str());
        else ImGui::TextDisabled("%d %s", count, Get("events.call.hint.many_run").c_str());
    }

    // The event's direct calls and the button adding one. Inside the
    // event's property table.
    static void DrawEventCalls(Event &event, const EventOwnerKind kind, const ID ownerID) {
        const char *instigatorKey = InstigatorLabelKey(event.payload);
        int toRemove = -1;

        for (size_t i = 0; i < event.calls.size(); ++i) {
            ImGui::PushID(static_cast<int>(i) + 1000);
            bool remove = false;
            DrawDirectCallRows(event.calls[i], kind, ownerID, instigatorKey, true, remove);
            if (remove) toRemove = static_cast<int>(i);
            ImGui::PopID();
        }

        if (toRemove >= 0) event.calls.erase(event.calls.begin() + toRemove);

        Property("");
        if (ImGui::Button(Get("events.call.add").c_str(), ImVec2(-FLT_MIN, 0.0f))) event.calls.emplace_back();
        Tooltip(Get("editor.tooltip.events.call_add").c_str());
    }

    static void DrawEventsEditor(std::vector<Event> &events, const EventOwnerKind kind, const ID ownerID) {
        int eventToRemove = -1;

        for (size_t i = 0; i < events.size(); ++i) {
            Event &event = events[i];
            ImGui::PushID(static_cast<int>(i));

            // "###event" keeps the header's open state while its text changes.
            const std::string header = EntryHeader(GetTranslationKey(event.payload), event.useTag, event.tagId,
                                                   event.calls.size(), "###event");

            bool deleteClicked = false;
            if (BeginEntryCard("##EventCard") && EntryCardHeader(header, deleteClicked)) {
                // What every event has...
                if (BeginProperties("##Common")) {
                    Property(Get("events.type").c_str());
                    DrawPayloadTypeCombo(event.payload, kind, ownerID);

                    Property(Get("events.link_tag").c_str());
                    DrawTagCombo("##LinkTag", event.tagId, TagRegistry::TagKind::EVENT, &event.useTag);
                    Tooltip(Get("editor.tooltip.events.link_tag").c_str());

                    DrawEventCalls(event, kind, ownerID);

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
                           !std::is_same_v<Payload, PayloadOnStart>;
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
                            else if constexpr (std::is_same_v<Payload, PayloadKeyPress> ||
                                               std::is_same_v<Payload, PayloadKeyRelease>) {
                                Property(Get("events.key_input").c_str());
                                DrawEnumCombo("##KeyInput", payload.input, kKeyEventInputOptions);
                                Tooltip(Get("editor.tooltip.events.key_input").c_str());

                                if (payload.input == KeyEventInput::KEY) {
                                    Property(Get("events.key").c_str());
                                    DrawKeyBindButton("KeyEventKey", &payload.key, -FLT_MIN);
                                    Tooltip(Get("editor.tooltip.events.key").c_str());
                                }
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

        DrawAddEntryCombo("events.add_placeholder", events, kind, ownerID);
    }

    // ── Set Field ──────────────────────────────────────────────────────────

    // How many components of the type the entity has.
    static int FieldComponentCount(Level &level, const ID owner, const FieldComponent component) {
        switch (component) {
            case FieldComponent::TRANSFORM: return static_cast<int>(level.transforms.InstancesOf(owner).size());
            case FieldComponent::SPRITE: return static_cast<int>(level.sprites.InstancesOf(owner).size());
            case FieldComponent::AUDIO_SOURCE: return static_cast<int>(level.audioSources.InstancesOf(owner).size());
            case FieldComponent::SCRIPT: return static_cast<int>(level.scripts.GetAll(owner).size());
            case FieldComponent::PLAYER_CONTROLLER: return static_cast<int>(level.playerControllers.InstancesOf(owner).size());
            case FieldComponent::CAMERA: return static_cast<int>(level.cameras.InstancesOf(owner).size());
            case FieldComponent::COLLIDER: return static_cast<int>(level.colliders.InstancesOf(owner).size());
            case FieldComponent::RIGIDBODY: return static_cast<int>(level.rigidbodies.InstancesOf(owner).size());
            case FieldComponent::MODEL: return static_cast<int>(level.models.InstancesOf(owner).size());
            case FieldComponent::FLIPBOOK: return static_cast<int>(level.flipbooks.InstancesOf(owner).size());
            case FieldComponent::UI_TRANSFORM: return static_cast<int>(level.ui_transforms.InstancesOf(owner).size());
            case FieldComponent::UI_SPRITE: return static_cast<int>(level.ui_sprites.InstancesOf(owner).size());
            case FieldComponent::UI_TEXT: return static_cast<int>(level.ui_texts.InstancesOf(owner).size());
            case FieldComponent::UI_BUTTON: return static_cast<int>(level.ui_buttons.InstancesOf(owner).size());
            case FieldComponent::PATHFINDER: return static_cast<int>(level.pathfinders.InstancesOf(owner).size());
            case FieldComponent::MONSTER_AI: return static_cast<int>(level.monsterAIs.InstancesOf(owner).size());
            case FieldComponent::PARTICLE_EMITTER: return static_cast<int>(level.particleEmitters.InstancesOf(owner).size());
        }
        return 0;
    }

    static const char *FieldComponentKey(const FieldComponent component) {
        for (const auto &[value, key]: kFieldComponentOptions)
            if (value == component) return key;
        return "component.script";
    }

    // "Sprite 2", or for scripts "2: Scripts/Door".
    static std::string FieldComponentLabel(Level &level, const ID owner, const FieldComponent component, const int index) {
        const std::string number = std::to_string(index + 1);

        if (component == FieldComponent::SCRIPT) {
            const std::vector<ComponentScript *> scripts = level.scripts.GetAll(owner);
            if (index >= 0 && index < static_cast<int>(scripts.size()))
                return number + ": " + (scripts[index]->fileName.empty() ? Get("events.field.no_script") : scripts[index]->fileName);
        }

        return Get(FieldComponentKey(component)) + " " + number;
    }

    // One field the Field dropdown offers.
    struct FieldChoice {
        std::string key;
        std::string label;
        FieldValueKind kind;
        const ScriptPublicField *script = nullptr; // script fields only
    };

    static std::vector<FieldChoice> FieldChoices(Level &level, const ID owner, const FieldComponent component, const int index) {
        std::vector<FieldChoice> choices;

        if (component != FieldComponent::SCRIPT) {
            for (const EventFieldInfo &info: EVENT_FIELDS)
                if (info.component == component) choices.push_back({info.key, Get(info.translationKey), info.kind});
            return choices;
        }

        const std::vector<ComponentScript *> scripts = level.scripts.GetAll(owner);
        if (index < 0 || index >= static_cast<int>(scripts.size())) return choices;

        const std::vector<ScriptPublicField> *fields = LevelSystem::GetPublicFieldsForScript(scripts[index]->fileName);
        if (!fields) return choices;

        for (const ScriptPublicField &field: *fields) {
            const std::optional<FieldValueKind> fieldKind = ScriptFieldKindOf(field);
            if (!fieldKind) continue;
            choices.push_back({field.name, field.displayName.empty() ? field.name : field.displayName, *fieldKind, &field});
        }

        return choices;
    }

    // A script value as a Set Field value.
    static FieldValue ScriptValueToField(const ScriptValue &value) {
        FieldValue field;
        std::visit([&]<typename T>(const T &v) {
            if constexpr (std::is_same_v<T, int>) field.number.x = static_cast<float>(v);
            else if constexpr (std::is_same_v<T, float>) field.number.x = v;
            else if constexpr (std::is_same_v<T, bool>) field.flag = v;
            else if constexpr (std::is_same_v<T, std::string>) field.text = v;
            else if constexpr (std::is_same_v<T, Vector2>) field.number = {v.x, v.y, 0.0f, 0.0f};
            else if constexpr (std::is_same_v<T, Vector3>) field.number = {v.x, v.y, v.z, 0.0f};
            else if constexpr (std::is_same_v<T, Vector4>) field.number = v;
            else if constexpr (std::is_same_v<T, AssetRefValue>) field.text = v.path;
        }, value);
        return field;
    }

    // What the picked field holds on the owner now: a new Set starts there.
    static FieldValue CurrentFieldValue(Level &level, const ID owner, const PayloadDispatchEntitySetField &set,
                                        const FieldChoice &choice) {
        if (!choice.script)
            return EventSystem::ReadEntityField(level, owner, set.component, set.componentIndex, set.field).value_or(FieldValue{});

        const std::vector<ComponentScript *> scripts = level.scripts.GetAll(owner);
        if (set.componentIndex >= 0 && set.componentIndex < static_cast<int>(scripts.size())) {
            const auto it = scripts[set.componentIndex]->publicValues.find(choice.key);
            if (it != scripts[set.componentIndex]->publicValues.end()) return ScriptValueToField(it->second);
        }

        return ScriptValueToField(choice.script->defaultValue);
    }

    // The value row(s) for a fixed value of `choice`'s kind.
    static void DrawFieldValueEditor(FieldValue &value, const FieldChoice &choice, const bool draggable) {
        Property(Get("events.value").c_str());

        switch (choice.kind) {
            case FieldValueKind::FLOAT:
                InputOrDrag("##Value", &value.number.x, draggable, 0.1f);
                break;
            case FieldValueKind::INT: {
                int number = static_cast<int>(std::round(value.number.x));
                if (InputOrDrag("##Value", &number, draggable, 1.0f)) value.number.x = static_cast<float>(number);
                break;
            }
            case FieldValueKind::BOOL:
                ImGui::Checkbox("##Value", &value.flag);
                break;
            case FieldValueKind::STRING:
                ImGui::InputText("##Value", &value.text);
                break;
            case FieldValueKind::VECTOR2:
                InputOrDrag2("##Value", &value.number.x, draggable, 0.1f);
                break;
            case FieldValueKind::VECTOR3:
                InputOrDrag3("##Value", &value.number.x, draggable, 0.1f);
                break;
            case FieldValueKind::VECTOR4:
                InputOrDrag4("##Value", &value.number.x, draggable, 0.1f);
                break;
            case FieldValueKind::COLOR:
                ImGui::ColorEdit4("##Value", &value.number.x, CompactColorFlags(ImGui::GetContentRegionAvail().x, 4));
                break;
            case FieldValueKind::TEXTURE:
            case FieldValueKind::SOUND:
                ImGui::PushID("ValueAsset");
                MapEditorInternal::DrawAssetField("", value.text,
                                                  choice.kind == FieldValueKind::SOUND ? AssetKind::Sound : AssetKind::Texture);
                ImGui::PopID();
                break;
            case FieldValueKind::ENUM: {
                const int current = static_cast<int>(std::round(value.number.x));
                std::string preview = std::to_string(current);
                if (choice.script)
                    for (const ScriptEnumOption &option: choice.script->enumOptions)
                        if (option.value == current) preview = option.name;

                if (ImGui::BeginCombo("##Value", preview.c_str())) {
                    if (choice.script)
                        for (const ScriptEnumOption &option: choice.script->enumOptions)
                            if (ImGui::Selectable(option.name.c_str(), option.value == current))
                                value.number.x = static_cast<float>(option.value);
                    ImGui::EndCombo();
                }
                break;
            }
        }
    }

    static void DrawSetFieldSettings(PayloadDispatchEntitySetField &set, const ID ownerID, const bool draggable) {
        using Set = PayloadDispatchEntitySetField;
        Level &level = LevelManager::CurrentLevel();

        // Component: the types the owner has, plus the picked one if it
        // doesn't have it (any more).
        Property(Get("events.component").c_str());
        if (ImGui::BeginCombo("##Component", Get(FieldComponentKey(set.component)).c_str())) {
            for (const auto &[component, key]: kFieldComponentOptions) {
                if (FieldComponentCount(level, ownerID, component) == 0 && component != set.component) continue;
                if (ImGui::Selectable(Get(key).c_str(), component == set.component) && component != set.component) {
                    set.component = component;
                    set.componentIndex = 0;
                    set.field.clear();
                }
                OptionTooltip(key);
            }
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.dispatches.set_field_component").c_str());

        const int count = FieldComponentCount(level, ownerID, set.component);

        // Which one, when there is a choice (scripts: always, by file).
        if (count > 1 || set.component == FieldComponent::SCRIPT || set.componentIndex >= count) {
            Property(Get("events.field.which").c_str());
            const std::string preview = set.componentIndex < count
                ? FieldComponentLabel(level, ownerID, set.component, set.componentIndex)
                : Get("events.field.missing_component");

            if (ImGui::BeginCombo("##Which", preview.c_str())) {
                for (int i = 0; i < count; ++i)
                    if (ImGui::Selectable(FieldComponentLabel(level, ownerID, set.component, i).c_str(), i == set.componentIndex) &&
                        i != set.componentIndex) {
                        set.componentIndex = i;
                        if (set.component == FieldComponent::SCRIPT) set.field.clear();
                    }
                ImGui::EndCombo();
            }
            Tooltip(Get("editor.tooltip.dispatches.set_field_which").c_str());
        }

        // Field.
        const std::vector<FieldChoice> choices = FieldChoices(level, ownerID, set.component, set.componentIndex);
        const auto picked = std::ranges::find(choices, set.field, &FieldChoice::key);

        Property(Get("events.field").c_str());
        const std::string fieldPreview = picked != choices.end() ? picked->label
                                         : set.field.empty() ? Get("events.field.none") : set.field + " " + Get("events.field.missing");
        if (ImGui::BeginCombo("##Field", fieldPreview.c_str())) {
            for (const FieldChoice &choice: choices) {
                if (!ImGui::Selectable(choice.label.c_str(), choice.key == set.field) || choice.key == set.field) continue;

                set.field = choice.key;
                set.value = CurrentFieldValue(level, ownerID, set, choice);

                // Keep the operation only where the new kind allows it.
                const bool numeric = IsNumericFieldKind(choice.kind);
                const bool keeps = set.operation == Set::SET ||
                                   (numeric && set.operation != Set::TOGGLE) ||
                                   (choice.kind == FieldValueKind::BOOL && set.operation == Set::TOGGLE);
                if (!keeps) set.operation = Set::SET;
            }
            if (choices.empty()) ImGui::TextDisabled("%s", Get("events.field.none_available").c_str());
            ImGui::EndCombo();
        }
        Tooltip(Get("editor.tooltip.dispatches.set_field_field").c_str());

        if (picked == choices.end()) return;

        const FieldChoice &choice = *picked;
        const bool numeric = IsNumericFieldKind(choice.kind);

        // Operation.
        if (numeric || choice.kind == FieldValueKind::BOOL) {
            Property(Get("events.operation").c_str());
            if (numeric) DrawEnumCombo("##Operation", set.operation, kSetFieldNumberOperations);
            else DrawEnumCombo("##Operation", set.operation, kSetFieldBoolOperations);
            Tooltip(Get("editor.tooltip.dispatches.set_field_operation").c_str());
        }
        else set.operation = Set::SET;

        // Value: fixed or a variable (not for asset paths, nor Toggle).
        if (set.operation != Set::TOGGLE) {
            const bool variableAllowed = choice.kind != FieldValueKind::TEXTURE && choice.kind != FieldValueKind::SOUND;
            if (!variableAllowed) set.fromVariable = false;

            if (variableAllowed) {
                Property(Get("events.value_source").c_str());
                DrawEnumCombo("##Source", set.fromVariable, kFieldValueSourceOptions);
                Tooltip(Get("editor.tooltip.dispatches.set_field_source").c_str());
            }

            if (set.fromVariable) {
                Property(Get("events.variable").c_str());
                DrawEventVariableCombo("##Variable", set.variable, -FLT_MIN);
            }
            else DrawFieldValueEditor(set.value, choice, draggable);
        }

        // Over time.
        if (numeric) {
            Property(Get("events.over_time").c_str());
            ImGui::Checkbox("##OverTime", &set.overTime);
            Tooltip(Get("editor.tooltip.dispatches.set_field_over_time").c_str());

            if (set.overTime) {
                Property(Get("events.speed").c_str());
                if (InputOrDrag("##Speed", &set.speed, draggable, 0.05f)) set.speed = std::max(0.0f, set.speed);
                Tooltip(Get("editor.tooltip.dispatches.set_field_speed").c_str());
            }
        }

        // Revert.
        Property(Get("events.revert").c_str());
        ImGui::Checkbox("##Revert", &set.revert);
        Tooltip(Get("editor.tooltip.dispatches.set_field_revert").c_str());

        if (set.revert) {
            Property(Get("events.revert_after").c_str());
            if (InputOrDrag("##RevertAfter", &set.revertAfter, draggable, 0.05f)) set.revertAfter = std::max(0.0f, set.revertAfter);
            Tooltip(Get("editor.tooltip.dispatches.set_field_revert_after").c_str());
        }
    }

    // ── Flow ───────────────────────────────────────────────────────────────

    static const char *ComparisonSymbol(const Condition::ConditionType type) {
        switch (type) {
            case Condition::EQUAL: return "=";
            case Condition::LESS_THAN: return "<";
            case Condition::GREATER_THAN: return ">";
            case Condition::LESS_THAN_OR_EQUAL: return "<=";
            case Condition::GREATER_THAN_OR_EQUAL: return ">=";
            default: return "";
        }
    }

    // Who the call reaches, as it reads in a flow step's sentence.
    static std::string FlowTargetText(const Level &level, const DirectCall &call, const EventOwnerKind callerKind) {
        const auto picked = [&](const std::optional<std::string> &label) {
            if (call.targetID == INVALID_ID) return Get("events.call.pick_object");
            return label ? *label : Get("events.call.missing_object");
        };

        switch (call.target) {
            case CallTarget::SELF: return Get(SelfLabelKey(callerKind));
            case CallTarget::INSTIGATOR: return Get("events.call.instigator.relay");
            case CallTarget::ENTITY: {
                const Entity *entity = level.GetEntity(call.targetID);
                return picked(entity ? std::optional(DescribeEntity(*entity)) : std::nullopt);
            }
            case CallTarget::SECTOR: {
                const Sector *sector = level.GetSector(call.targetID);
                return picked(sector ? std::optional(DescribeSector(*sector)) : std::nullopt);
            }
            case CallTarget::WALL: {
                const auto it = level.wallIDToIndex.find(call.targetID);
                return picked(it != level.wallIDToIndex.end() ? std::optional(DescribeWall(level.walls[it->second])) : std::nullopt);
            }
            case CallTarget::LINEDEF: {
                const Linedef *linedef = level.GetLinedef(call.targetID);
                return picked(linedef ? std::optional(DescribeLinedef(*linedef)) : std::nullopt);
            }
            case CallTarget::TAGGED: {
                const auto tag = TagRegistry::GetName(call.targetTag);
                return Get("events.flow.sentence.tagged") + " " + (tag ? *tag : Get("tags.missing"));
            }
        }
        return {};
    }

    // "After 2 s, if Keys >= 3  ->  call Door A . Open"
    static std::string FlowSentence(const Level &level, const DispatchFlow &flow, const EventOwnerKind callerKind) {
        std::string sentence = flow.wait > 0.0f
            ? Get("events.flow.sentence.after") + " " + FormatEventNumber(flow.wait) + " s"
            : Get("events.flow.sentence.next_frame");

        std::visit([&]<typename Kind>(const Kind &kind) {
            if constexpr (std::is_same_v<Kind, FlowVariable>) {
                const Condition &condition = kind.condition;
                if (condition.type == Condition::NONE) return;

                const auto side = [&](const Condition::InputField &field) {
                    if (const float *number = std::get_if<float>(&field)) return FormatEventNumber(*number);
                    return EventVariableLabel(level, std::get<EventVariableID>(field));
                };
                sentence += ", " + Get("events.flow.sentence.if") + " " + side(condition.left) + " " +
                        ComparisonSymbol(condition.type) + " " + side(condition.right);
            }
            else if constexpr (std::is_same_v<Kind, FlowChance>) {
                sentence += ", " + FormatEventNumber(kind.chance) + "% " + Get("events.flow.sentence.chance");
            }
        }, flow.condition);

        sentence += "  ->  " + Get("events.flow.sentence.call") + " " + FlowTargetText(level, flow.call, callerKind) +
                " . " + (flow.call.dispatch.empty() ? Get("events.call.pick_dispatch") : flow.call.dispatch);
        return sentence;
    }

    // The dispatch's Flow section: each step as a sentence, then its wait,
    // condition and call. Under the dispatch's other settings.
    static void DrawDispatchFlows(Dispatch &dispatch, const EventOwnerKind kind, const ID ownerID) {
        const Level &level = LevelManager::CurrentLevel();

        EntrySettingsGap();
        ImGui::TextUnformatted(Get("events.flow").c_str());
        Tooltip(Get("editor.tooltip.dispatches.flow").c_str());

        if (!BeginProperties("##Flow")) return;

        int toRemove = -1;

        for (size_t i = 0; i < dispatch.flows.size(); ++i) {
            DispatchFlow &flow = dispatch.flows[i];
            ImGui::PushID(static_cast<int>(i) + 2000);

            if (i > 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, 8.0f);

            Property((Get("events.flow.step") + " " + std::to_string(i + 1)).c_str());
            ImGui::TextWrapped("%s", FlowSentence(level, flow, kind).c_str());

            Property(Get("events.flow.wait").c_str());
            if (ImGui::InputFloat("##Wait", &flow.wait, 0.0f, 0.0f, "%.2f s")) flow.wait = std::max(0.0f, flow.wait);
            Tooltip(Get("editor.tooltip.dispatches.flow_wait").c_str());

            Property(Get("events.flow.condition").c_str());
            if (ImGui::BeginCombo("##FlowCondition", Get(std::visit([](const auto &c) {
                return std::decay_t<decltype(c)>::translationKey;
            }, flow.condition)).c_str())) {
                ForEachPayloadType<FlowCondition>([&]<typename Kind>(std::type_identity<Kind>) {
                    const bool selected = std::holds_alternative<Kind>(flow.condition);
                    if (ImGui::Selectable(Get(Kind::translationKey).c_str(), selected) && !selected) flow.condition = Kind{};
                    OptionTooltip(Kind::translationKey);
                    if (selected) ImGui::SetItemDefaultFocus();
                });
                ImGui::EndCombo();
            }
            Tooltip(Get("editor.tooltip.dispatches.flow_condition").c_str());

            if (auto *variable = std::get_if<FlowVariable>(&flow.condition)) DrawConditionFields(variable->condition);
            else if (auto *chance = std::get_if<FlowChance>(&flow.condition)) {
                Property(Get("events.chance").c_str());
                ImGui::SliderFloat("##FlowChance", &chance->chance, 0.0f, 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
                Tooltip(Get("editor.tooltip.dispatches.flow_chance").c_str());
            }

            bool remove = false;
            DrawDirectCallRows(flow.call, kind, ownerID, "events.call.instigator.relay", true, remove);
            if (remove) toRemove = static_cast<int>(i);

            ImGui::PopID();
        }

        if (toRemove >= 0) dispatch.flows.erase(dispatch.flows.begin() + toRemove);

        Property("");
        if (ImGui::Button(Get("events.flow.add").c_str(), ImVec2(-FLT_MIN, 0.0f))) dispatch.flows.emplace_back();
        Tooltip(Get("editor.tooltip.dispatches.flow_add").c_str());

        EndProperties();
    }

    static void DrawDispatchesEditor(std::vector<Dispatch> &dispatches, const EventOwnerKind kind, const ID ownerID,
                                     const bool draggable) {
        Level &level = LevelManager::CurrentLevel();
        int dispatchToRemove = -1;

        for (size_t i = 0; i < dispatches.size(); ++i) {
            Dispatch &dispatch = dispatches[i];
            ImGui::PushID(static_cast<int>(i));

            const std::string header = DispatchHeader(dispatch, "###dispatch");

            bool deleteClicked = false;
            if (BeginEntryCard("##DispatchCard") && EntryCardHeader(header, deleteClicked)) {
                // What every dispatch has...
                if (BeginProperties("##Common")) {
                    Property(Get("events.type").c_str());
                    DrawPayloadTypeCombo(dispatch.payload, kind, ownerID);

                    Property(Get("events.dispatch_name").c_str());
                    ImGui::InputTextWithHint("##Name", Get("events.dispatch_name.hint").c_str(), &dispatch.name);
                    Tooltip(Get("editor.tooltip.dispatches.name").c_str());

                    Property(Get("events.link_tag").c_str());
                    DrawTagCombo("##LinkTag", dispatch.tagId, TagRegistry::TagKind::EVENT, &dispatch.useTag);
                    Tooltip(Get("editor.tooltip.dispatches.link_tag").c_str());

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
                           !std::is_same_v<Payload, PayloadDispatchEntityEnable> &&
                           !std::is_same_v<Payload, PayloadDispatchPause> &&
                           !std::is_same_v<Payload, PayloadDispatchResume> &&
                           !std::is_same_v<Payload, PayloadDispatchQuit>;
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
                            else if constexpr (std::is_base_of_v<TeleportSettings, Payload>) {
                                // Who: the entities standing in this sector.
                                if constexpr (std::is_same_v<Payload, PayloadDispatchSectorTeleportInside>) {
                                    DrawTagFilterFields(payload.filter);
                                    DrawFloorIndexRow(payload.insideFloor, ownerID, true, "events.teleport.inside_floor");
                                    Tooltip(Get("editor.tooltip.dispatches.teleport_inside_floor").c_str());
                                }

                                // Where to. A prefab has no level objects to pick.
                                const bool inPrefab = EditingPrefab();
                                Property(Get("events.teleport.to").c_str());
                                if (ImGui::BeginCombo("##TeleportTo", Get(EnumOptionKey(payload.target, kTeleportTargetOptions)).c_str())) {
                                    for (const auto &[option, key]: kTeleportTargetOptions) {
                                        if (inPrefab && option != TeleportTarget::COORDINATES) continue;
                                        if (ImGui::Selectable(Get(key).c_str(), option == payload.target) && option != payload.target) {
                                            payload.target = option;
                                            payload.targetID = INVALID_ID;
                                            payload.floorIndex = 0;
                                            if (option != TeleportTarget::ENTITY && payload.facing == TeleportFacing::TARGET)
                                                payload.facing = TeleportFacing::KEEP;
                                        }
                                        OptionTooltip(key);
                                    }
                                    ImGui::EndCombo();
                                }
                                Tooltip(Get("editor.tooltip.dispatches.teleport_to").c_str());

                                if (payload.target == TeleportTarget::COORDINATES) {
                                    Property(Get("events.position").c_str());
                                    InputOrDrag3("##Position", &payload.position.x, draggable);
                                } else if (payload.target == TeleportTarget::ENTITY) {
                                    Property(Get("events.teleport.entity").c_str());
                                    DrawLevelObjectPicker("##TeleportEntity", EventOwnerKind::ENTITY, payload.targetID);
                                    if (ImGui::GetDragDropPayload() == nullptr) Tooltip(Get("editor.tooltip.dispatches.teleport_entity").c_str());
                                } else {
                                    Property(Get("events.teleport.sector").c_str());
                                    DrawLevelObjectPicker("##TeleportSector", EventOwnerKind::SECTOR, payload.targetID);
                                    if (ImGui::GetDragDropPayload() == nullptr) Tooltip(Get("editor.tooltip.dispatches.teleport_sector").c_str());

                                    // Its floors, bottom first.
                                    const Sector *sector = level.GetSector(payload.targetID);
                                    const int floorCount = sector ? static_cast<int>(sector->floors.size()) : 1;
                                    payload.floorIndex = std::clamp(payload.floorIndex, 0, std::max(0, floorCount - 1));

                                    const auto floorLabel = [](const int index) {
                                        return Get("events.floor") + " " + std::to_string(index + 1);
                                    };

                                    Property(Get("events.teleport.floor").c_str());
                                    ImGui::BeginDisabled(floorCount <= 1);
                                    if (ImGui::BeginCombo("##TeleportFloor", floorLabel(payload.floorIndex).c_str())) {
                                        for (int f = 0; f < floorCount; ++f) {
                                            ImGui::PushID(f);
                                            if (ImGui::Selectable(floorLabel(f).c_str(), f == payload.floorIndex)) payload.floorIndex = f;
                                            ImGui::PopID();
                                        }
                                        ImGui::EndCombo();
                                    }
                                    ImGui::EndDisabled();
                                    Tooltip(Get("editor.tooltip.dispatches.teleport_floor").c_str());
                                }

                                // Which way it faces after.
                                Property(Get("events.teleport.facing").c_str());
                                if (ImGui::BeginCombo("##TeleportFacing", Get(EnumOptionKey(payload.facing, kTeleportFacingOptions)).c_str())) {
                                    for (const auto &[option, key]: kTeleportFacingOptions) {
                                        if (option == TeleportFacing::TARGET && payload.target != TeleportTarget::ENTITY) continue;
                                        if (ImGui::Selectable(Get(key).c_str(), option == payload.facing)) payload.facing = option;
                                        OptionTooltip(key);
                                    }
                                    ImGui::EndCombo();
                                }
                                Tooltip(Get("editor.tooltip.dispatches.teleport_facing").c_str());

                                if (payload.facing == TeleportFacing::SET) {
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
                                }

                                Property(Get("events.teleport.stop_velocity").c_str());
                                ImGui::Checkbox("##StopVelocity", &payload.stopVelocity);
                                Tooltip(Get("editor.tooltip.dispatches.teleport_stop_velocity").c_str());

                                // Fills what Coordinates / Set use from the entity as it stands now.
                                if (kind == EventOwnerKind::ENTITY &&
                                    (payload.target == TeleportTarget::COORDINATES || payload.facing == TeleportFacing::SET)) {
                                    const ComponentTransform *transform = level.transforms.Get(ownerID);
                                    Property("");
                                    ImGui::BeginDisabled(transform == nullptr);
                                    if (ImGui::Button(Get("events.use_entity").c_str(), ImVec2(-FLT_MIN, 0.0f)) && transform != nullptr) {
                                        if (payload.target == TeleportTarget::COORDINATES) payload.position = transform->position;
                                        if (payload.facing == TeleportFacing::SET) payload.rotation = transform->rotation.Normalized();
                                    }
                                    ImGui::EndDisabled();
                                    Tooltip(Get("editor.tooltip.dispatches.use_entity").c_str());
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityCallFunction>) {
                                Property(Get("events.function_name").c_str());
                                ImGui::InputText("##Function", &payload.functionName);
                                Tooltip(Get("editor.tooltip.dispatches.function_name").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityPathfinder>) {
                                // A prefab has no level objects to pick.
                                const bool inPrefab = EditingPrefab();
                                Property(Get("events.pathfinder.action").c_str());
                                if (ImGui::BeginCombo("##PathfinderAction", Get(EnumOptionKey(payload.action, kPathfinderActionOptions)).c_str())) {
                                    for (const auto &[option, key]: kPathfinderActionOptions) {
                                        if (inPrefab && (option == PathfinderAction::GO_TO_SECTOR || option == PathfinderAction::FOLLOW_ENTITY)) continue;
                                        if (ImGui::Selectable(Get(key).c_str(), option == payload.action) && option != payload.action) {
                                            payload.action = option;
                                            payload.targetID = INVALID_ID;
                                            payload.floorIndex = 0;
                                        }
                                        OptionTooltip(key);
                                    }
                                    ImGui::EndCombo();
                                }
                                Tooltip(Get("editor.tooltip.dispatches.pathfinder_action").c_str());

                                switch (payload.action) {
                                    case PathfinderAction::GO_TO_COORDINATES: {
                                        Property(Get("events.position").c_str());
                                        InputOrDrag3("##Position", &payload.position.x, draggable);

                                        const ComponentTransform *transform = level.transforms.Get(ownerID);
                                        Property("");
                                        ImGui::BeginDisabled(transform == nullptr);
                                        if (ImGui::Button(Get("events.use_entity").c_str(), ImVec2(-FLT_MIN, 0.0f)) && transform != nullptr)
                                            payload.position = transform->position;
                                        ImGui::EndDisabled();
                                        Tooltip(Get("editor.tooltip.dispatches.pathfinder_use_entity").c_str());
                                        break;
                                    }
                                    case PathfinderAction::GO_TO_SECTOR: {
                                        Property(Get("events.teleport.sector").c_str());
                                        DrawLevelObjectPicker("##PathfinderSector", EventOwnerKind::SECTOR, payload.targetID);
                                        if (ImGui::GetDragDropPayload() == nullptr) Tooltip(Get("editor.tooltip.dispatches.pathfinder_sector").c_str());

                                        const Sector *sector = level.GetSector(payload.targetID);
                                        const int floorCount = sector ? static_cast<int>(sector->floors.size()) : 1;
                                        payload.floorIndex = std::clamp(payload.floorIndex, 0, std::max(0, floorCount - 1));
                                        ImGui::BeginDisabled(floorCount <= 1);
                                        DrawFloorIndexRow(payload.floorIndex, payload.targetID, false);
                                        ImGui::EndDisabled();
                                        break;
                                    }
                                    case PathfinderAction::FOLLOW_ENTITY:
                                    case PathfinderAction::FOLLOW_INSTIGATOR:
                                        if (payload.action == PathfinderAction::FOLLOW_ENTITY) {
                                            Property(Get("events.teleport.entity").c_str());
                                            DrawLevelObjectPicker("##PathfinderEntity", EventOwnerKind::ENTITY, payload.targetID);
                                            if (payload.targetID == ownerID) payload.targetID = INVALID_ID; // can't follow itself
                                            if (ImGui::GetDragDropPayload() == nullptr) Tooltip(Get("editor.tooltip.dispatches.pathfinder_entity").c_str());
                                        }

                                        Property(Get("events.offset").c_str());
                                        InputOrDrag3("##Offset", &payload.offset.x, draggable);
                                        Tooltip(Get("editor.tooltip.dispatches.pathfinder_offset").c_str());
                                        break;
                                    case PathfinderAction::PAUSE:
                                        Property(Get("events.pathfinder.seconds").c_str());
                                        if (InputOrDrag("##Seconds", &payload.seconds, draggable, 0.05f))
                                            payload.seconds = std::max(0.0f, payload.seconds);
                                        break;
                                    case PathfinderAction::STOP:
                                    case PathfinderAction::RESUME:
                                    case PathfinderAction::PICK_NEW_DIRECTION:
                                        break;
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntityMonsterAI>) {
                                const bool inPrefab = EditingPrefab();
                                Property(Get("events.pathfinder.action").c_str());
                                if (ImGui::BeginCombo("##MonsterAction", Get(EnumOptionKey(payload.action, kMonsterAIActionOptions)).c_str())) {
                                    for (const auto &[option, key]: kMonsterAIActionOptions) {
                                        if (inPrefab && option == MonsterAIAction::WAKE_ON_ENTITY) continue;
                                        if (ImGui::Selectable(Get(key).c_str(), option == payload.action) && option != payload.action) {
                                            payload.action = option;
                                            payload.targetID = INVALID_ID;
                                        }
                                        OptionTooltip(key);
                                    }
                                    ImGui::EndCombo();
                                }
                                Tooltip(Get("editor.tooltip.dispatches.monster_ai_action").c_str());

                                if (payload.action == MonsterAIAction::WAKE_ON_ENTITY) {
                                    Property(Get("events.teleport.entity").c_str());
                                    DrawLevelObjectPicker("##MonsterEntity", EventOwnerKind::ENTITY, payload.targetID);
                                    if (payload.targetID == ownerID) payload.targetID = INVALID_ID; // can't chase itself
                                }
                                else if (payload.action == MonsterAIAction::HURT) {
                                    Property(Get("events.monster_ai.damage").c_str());
                                    InputOrDrag("##MonsterDamage", &payload.damage, draggable);
                                    payload.damage = std::max(payload.damage, 0.0f);
                                    Tooltip(Get("editor.tooltip.dispatches.monster_ai_damage").c_str());
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchNoiseAlert>) {
                                Property(Get("events.noise.target").c_str());
                                DrawEnumCombo("##NoiseTarget", payload.target, kNoiseTargetOptions);
                                Tooltip(Get("editor.tooltip.dispatches.noise_target").c_str());
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
                                using Door = PayloadDispatchSectorDoor;

                                // Close types reopen to where they started, so
                                // only these use the open heights.
                                const bool usesOpenHeights = payload.type != Door::CLOSE_WAIT_OPEN && payload.type != Door::CLOSE_STAY;
                                const bool opens = payload.type != Door::CLOSE_STAY;
                                const bool closes = payload.type != Door::OPEN_STAY;
                                const bool cycles = payload.type == Door::OPEN_WAIT_CLOSE || payload.type == Door::CLOSE_WAIT_OPEN;

                                Property(Get("events.door.type").c_str());
                                DrawEnumCombo("##Type", payload.type, kDoorTypeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.door_type").c_str());

                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                if (usesOpenHeights) {
                                    Property(Get("events.door.open_mode").c_str());
                                    DrawEnumCombo("##OpenMode", payload.openMode, kDoorOpenModeOptions);

                                    if (payload.openMode == Door::FIXED_HEIGHT) {
                                        Property(Get("events.door.open_height").c_str());
                                        InputOrDrag("##OpenHeight", &payload.openHeight, draggable, 0.1f);
                                    }
                                    else {
                                        Property(Get("events.door.lip").c_str());
                                        InputOrDrag("##Lip", &payload.lip, draggable, 0.1f);
                                        Tooltip(Get("editor.tooltip.dispatches.door_lip").c_str());
                                    }
                                }

                                Property(Get("events.door.split").c_str());
                                ImGui::Checkbox("##Split", &payload.split);
                                Tooltip(Get("editor.tooltip.dispatches.door_split").c_str());

                                if (payload.split && usesOpenHeights) {
                                    Property(Get("events.door.floor_open_mode").c_str());
                                    DrawEnumCombo("##FloorOpenMode", payload.floorOpenMode, kDoorFloorOpenModeOptions);

                                    if (payload.floorOpenMode == Door::FIXED_FLOOR_HEIGHT) {
                                        Property(Get("events.door.floor_open_height").c_str());
                                        InputOrDrag("##FloorOpenHeight", &payload.floorOpenHeight, draggable, 0.1f);
                                    }
                                    else {
                                        Property(Get("events.door.floor_lip").c_str());
                                        InputOrDrag("##FloorLip", &payload.floorLip, draggable, 0.1f);
                                        Tooltip(Get("editor.tooltip.dispatches.door_floor_lip").c_str());
                                    }
                                }

                                if (opens) {
                                    Property(Get("events.door.open_speed").c_str());
                                    if (InputOrDrag("##OpenSpeed", &payload.openSpeed, draggable, 0.1f))
                                        payload.openSpeed = std::max(0.01f, payload.openSpeed);
                                }

                                if (closes) {
                                    Property(Get("events.door.close_speed").c_str());
                                    if (InputOrDrag("##CloseSpeed", &payload.closeSpeed, draggable, 0.1f))
                                        payload.closeSpeed = std::max(0.01f, payload.closeSpeed);
                                }

                                if (cycles) {
                                    Property(Get("events.door.wait").c_str());
                                    if (InputOrDrag("##Wait", &payload.wait, draggable, 0.1f))
                                        payload.wait = std::max(0.0f, payload.wait);
                                    Tooltip(Get("editor.tooltip.dispatches.door_wait").c_str());
                                }

                                if (closes) {
                                    Property(Get("events.door.close_collision").c_str());
                                    DrawEnumCombo("##Collision", payload.closeCollision, kSectorCollisionOptions);
                                    Tooltip(Get("editor.tooltip.dispatches.door_collision").c_str());
                                }

                                if (cycles) {
                                    Property(Get("events.door.retrigger").c_str());
                                    if (payload.type == Door::CLOSE_WAIT_OPEN)
                                        DrawEnumCombo("##Retrigger", payload.retrigger, kDoorCloseFirstRetriggerOptions);
                                    else
                                        DrawEnumCombo("##Retrigger", payload.retrigger, kDoorRetriggerOptions);
                                    Tooltip(Get("editor.tooltip.dispatches.door_retrigger").c_str());
                                }

                                if (opens)
                                    DrawAssetRow("events.door.open_sound", payload.openSound, AssetKind::Sound, "editor.tooltip.dispatches.door_open_sound");

                                if (closes)
                                    DrawAssetRow("events.door.close_sound", payload.closeSound, AssetKind::Sound, "editor.tooltip.dispatches.door_close_sound");

                                Property(Get("events.gain").c_str());
                                if (InputOrDrag("##Gain", &payload.soundGain, draggable, 0.01f))
                                    payload.soundGain = std::max(0.0f, payload.soundGain);

                                Property(Get("events.pitch").c_str());
                                if (InputOrDrag("##Pitch", &payload.soundPitch, draggable, 0.01f))
                                    payload.soundPitch = std::max(0.01f, payload.soundPitch);

                                Property(Get("events.door.locked").c_str());
                                ImGui::Checkbox("##Locked", &payload.locked);
                                Tooltip(Get("editor.tooltip.dispatches.door_locked").c_str());

                                if (payload.locked) {
                                    Property(Get("events.door.required_tag").c_str());
                                    DrawTagCombo("##RequiredTag", payload.requiredTag, TagRegistry::TagKind::NORMAL);
                                    Tooltip(Get("editor.tooltip.dispatches.door_required_tag").c_str());

                                    DrawAssetRow("events.door.locked_sound", payload.lockedSound, AssetKind::Sound, "editor.tooltip.dispatches.door_locked_sound");

                                    Property(Get("events.door.locked_message").c_str());
                                    ImGui::InputText("##LockedMessage", &payload.lockedMessage);
                                    Tooltip(Get("editor.tooltip.dispatches.door_locked_message").c_str());

                                    Property(Get("events.duration").c_str());
                                    if (InputOrDrag("##LockedDuration", &payload.lockedMessageDuration, draggable, 0.05f))
                                        payload.lockedMessageDuration = std::max(0.0f, payload.lockedMessageDuration);

                                    Property(Get("events.color").c_str());
                                    ImGui::ColorEdit3("##LockedColor", &payload.lockedMessageColor.x,
                                                      CompactColorFlags(ImGui::GetContentRegionAvail().x, 3));
                                }

                                Property(Get("events.door.pathfinders_can_open").c_str());
                                ImGui::Checkbox("##PathfindersCanOpen", &payload.pathfindersCanOpen);
                                Tooltip(Get("editor.tooltip.dispatches.door_pathfinders_can_open").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorLift>) {
                                using Lift = PayloadDispatchSectorLift;

                                const bool usesLow = payload.type != Lift::UP_WAIT_DOWN;
                                const bool usesHigh = payload.type != Lift::DOWN_WAIT_UP;

                                Property(Get("events.lift.type").c_str());
                                DrawEnumCombo("##Type", payload.type, kLiftTypeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.lift_type").c_str());

                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                if (usesLow) {
                                    Property(Get("events.lift.low_mode").c_str());
                                    DrawEnumCombo("##LowMode", payload.lowMode, kLiftLowModeOptions);

                                    if (payload.lowMode == Lift::FIXED_LOW) {
                                        Property(Get("events.lift.low_height").c_str());
                                        InputOrDrag("##LowHeight", &payload.lowHeight, draggable, 0.1f);
                                    }
                                    else {
                                        Property(Get("events.lift.low_lip").c_str());
                                        InputOrDrag("##LowLip", &payload.lowLip, draggable, 0.1f);
                                        Tooltip(Get("editor.tooltip.dispatches.lift_low_lip").c_str());
                                    }
                                }

                                if (usesHigh) {
                                    Property(Get("events.lift.high_mode").c_str());
                                    DrawEnumCombo("##HighMode", payload.highMode, kLiftHighModeOptions);

                                    if (payload.highMode == Lift::FIXED_HIGH) {
                                        Property(Get("events.lift.high_height").c_str());
                                        InputOrDrag("##HighHeight", &payload.highHeight, draggable, 0.1f);
                                    }
                                    else {
                                        Property(Get("events.lift.high_lip").c_str());
                                        InputOrDrag("##HighLip", &payload.highLip, draggable, 0.1f);
                                        Tooltip(Get("editor.tooltip.dispatches.lift_high_lip").c_str());
                                    }
                                }

                                Property(Get("events.speed").c_str());
                                if (InputOrDrag("##Speed", &payload.speed, draggable, 0.1f))
                                    payload.speed = std::max(0.01f, payload.speed);
                                Tooltip(Get("editor.tooltip.dispatches.move_speed").c_str());

                                Property(Get("events.door.wait").c_str());
                                if (InputOrDrag("##Wait", &payload.wait, draggable, 0.1f))
                                    payload.wait = std::max(0.0f, payload.wait);
                                Tooltip(Get("editor.tooltip.dispatches.lift_wait").c_str());

                                Property(Get("events.lift.collision").c_str());
                                DrawEnumCombo("##Collision", payload.collision, kSectorCollisionOptions);
                                Tooltip(Get("editor.tooltip.dispatches.lift_collision").c_str());

                                if (payload.type != Lift::PERPETUAL) {
                                    Property(Get("events.door.retrigger").c_str());
                                    DrawEnumCombo("##Retrigger", payload.retrigger, kLiftRetriggerOptions);
                                    Tooltip(Get("editor.tooltip.dispatches.lift_retrigger").c_str());
                                }

                                DrawAssetRow("events.lift.start_sound", payload.startSound, AssetKind::Sound, "editor.tooltip.dispatches.lift_start_sound");
                                DrawAssetRow("events.lift.stop_sound", payload.stopSound, AssetKind::Sound, "editor.tooltip.dispatches.lift_stop_sound");

                                Property(Get("events.gain").c_str());
                                if (InputOrDrag("##Gain", &payload.soundGain, draggable, 0.01f))
                                    payload.soundGain = std::max(0.0f, payload.soundGain);

                                Property(Get("events.pitch").c_str());
                                if (InputOrDrag("##Pitch", &payload.soundPitch, draggable, 0.01f))
                                    payload.soundPitch = std::max(0.01f, payload.soundPitch);

                                Property(Get("events.door.locked").c_str());
                                ImGui::Checkbox("##Locked", &payload.locked);
                                Tooltip(Get("editor.tooltip.dispatches.lift_locked").c_str());

                                if (payload.locked) {
                                    Property(Get("events.door.required_tag").c_str());
                                    DrawTagCombo("##RequiredTag", payload.requiredTag, TagRegistry::TagKind::NORMAL);
                                    Tooltip(Get("editor.tooltip.dispatches.door_required_tag").c_str());

                                    DrawAssetRow("events.door.locked_sound", payload.lockedSound, AssetKind::Sound, "editor.tooltip.dispatches.door_locked_sound");

                                    Property(Get("events.door.locked_message").c_str());
                                    ImGui::InputText("##LockedMessage", &payload.lockedMessage);
                                    Tooltip(Get("editor.tooltip.dispatches.door_locked_message").c_str());

                                    Property(Get("events.duration").c_str());
                                    if (InputOrDrag("##LockedDuration", &payload.lockedMessageDuration, draggable, 0.05f))
                                        payload.lockedMessageDuration = std::max(0.0f, payload.lockedMessageDuration);

                                    Property(Get("events.color").c_str());
                                    ImGui::ColorEdit3("##LockedColor", &payload.lockedMessageColor.x,
                                                      CompactColorFlags(ImGui::GetContentRegionAvail().x, 3));
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorRaiseStairs>) {
                                DrawFloorIndexRow(payload.floorIndex, ownerID, false);

                                Property(Get("events.stairs.step_height").c_str());
                                InputOrDrag("##StepHeight", &payload.stepHeight, draggable, 0.1f);
                                Tooltip(Get("editor.tooltip.dispatches.stairs_step_height").c_str());

                                Property(Get("events.speed").c_str());
                                if (InputOrDrag("##Speed", &payload.speed, draggable, 0.1f))
                                    payload.speed = std::max(0.01f, payload.speed);
                                Tooltip(Get("editor.tooltip.dispatches.move_speed").c_str());

                                Property(Get("events.stairs.ignore_texture").c_str());
                                ImGui::Checkbox("##IgnoreTexture", &payload.ignoreTexture);
                                Tooltip(Get("editor.tooltip.dispatches.stairs_ignore_texture").c_str());

                                Property(Get("events.collision").c_str());
                                DrawEnumCombo("##Collision", payload.collision, kSectorCollisionOptions);
                                Tooltip(Get("editor.tooltip.dispatches.collision").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSectorLightEffect>) {
                                using Effect = PayloadDispatchSectorLightEffect;

                                Property(Get("events.light_effect.type").c_str());
                                DrawEnumCombo("##Type", payload.type, kLightEffectTypeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.light_effect_type").c_str());

                                if (payload.type != Effect::NONE) {
                                    Property(Get("events.light_effect.dim_mode").c_str());
                                    DrawEnumCombo("##DimMode", payload.dimMode, kLightDimModeOptions);
                                    Tooltip(Get("editor.tooltip.dispatches.light_effect_dim_mode").c_str());

                                    if (payload.dimMode == Effect::COLOR) {
                                        // 0-255 like the sector inspector's light.
                                        float color[3] = {payload.dimLight.x / 255.0f, payload.dimLight.y / 255.0f, payload.dimLight.z / 255.0f};
                                        Property(Get("events.light_effect.dim_light").c_str());
                                        if (ImGui::ColorEdit3("##DimLight", color, CompactColorFlags(ImGui::GetContentRegionAvail().x, 3)))
                                            payload.dimLight = {color[0] * 255.0f, color[1] * 255.0f, color[2] * 255.0f};
                                    }

                                    if (payload.type == Effect::STROBE || payload.type == Effect::FLICKER) {
                                        const bool flicker = payload.type == Effect::FLICKER;

                                        Property(Get(flicker ? "events.light_effect.bright_time_max" : "events.light_effect.bright_time").c_str());
                                        if (InputOrDrag("##BrightTime", &payload.brightTime, draggable, 0.01f))
                                            payload.brightTime = std::max(0.01f, payload.brightTime);

                                        Property(Get(flicker ? "events.light_effect.dim_time_max" : "events.light_effect.dim_time").c_str());
                                        if (InputOrDrag("##DimTime", &payload.dimTime, draggable, 0.01f))
                                            payload.dimTime = std::max(0.01f, payload.dimTime);
                                    }

                                    if (payload.type == Effect::GLOW) {
                                        Property(Get("events.speed").c_str());
                                        if (InputOrDrag("##Speed", &payload.speed, draggable, 1.0f))
                                            payload.speed = std::max(0.01f, payload.speed);
                                        Tooltip(Get("editor.tooltip.dispatches.light_speed").c_str());
                                    }

                                    if (payload.type == Effect::FIRE_FLICKER) {
                                        Property(Get("events.light_effect.interval").c_str());
                                        if (InputOrDrag("##Interval", &payload.interval, draggable, 0.01f))
                                            payload.interval = std::max(0.01f, payload.interval);
                                    }

                                    Property(Get("events.light_effect.sync").c_str());
                                    ImGui::Checkbox("##Sync", &payload.sync);
                                    Tooltip(Get("editor.tooltip.dispatches.light_effect_sync").c_str());
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEntitySetField>) {
                                DrawSetFieldSettings(payload, ownerID, draggable);
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
                                DrawTagCombo("##Tag", payload.tag, TagRegistry::TagKind::NORMAL);
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
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchEmitParticles>) {
                                DrawAssetRow("events.prefab", payload.prefab, AssetKind::Prefab, "editor.tooltip.dispatches.emit_prefab");

                                Property(Get("events.emit_count").c_str());
                                ImGui::InputInt("##EmitCount", &payload.count);
                                payload.count = std::max(payload.count, 0);
                                Tooltip(Get("editor.tooltip.dispatches.emit_count").c_str());

                                Property(Get("events.offset").c_str());
                                InputOrDrag3("##Offset", &payload.offset.x, draggable);
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
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchRelay>) {
                                Property(Get("events.relay_mode").c_str());
                                DrawEnumCombo("##RelayMode", payload.direct, kRelayModeOptions);
                                Tooltip(Get("editor.tooltip.dispatches.relay_mode").c_str());

                                if (payload.direct) {
                                    bool remove = false;
                                    DrawDirectCallRows(payload.call, kind, ownerID, "events.call.instigator.relay", false, remove);
                                }
                                else {
                                    Property(Get("events.target_tag").c_str());
                                    DrawTagCombo("##Target", payload.target, TagRegistry::TagKind::EVENT);
                                    Tooltip(Get("editor.tooltip.dispatches.relay").c_str());
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchResetEvent>) {
                                Property(Get("events.target_tag").c_str());
                                DrawTagCombo("##Target", payload.target, TagRegistry::TagKind::EVENT);
                                Tooltip(Get("editor.tooltip.dispatches.reset_event").c_str());
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchLoadLevel>) {
                                DrawLevelNameRow(payload.levelName, payload.nextLevel);
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchMusic>) {
                                Property(Get("events.music.action").c_str());
                                DrawEnumCombo("##Action", payload.action, kMusicActionOptions);
                                Tooltip(Get("editor.tooltip.dispatches.music_action").c_str());

                                if (payload.action == PayloadDispatchMusic::SWITCH_TO_TRACK) {
                                    // The tracks of the level this card is in (World Settings > Music).
                                    const std::vector<std::string> &tracks = level.worldSettings.music.tracks;
                                    const auto trackLabel = [&](const int index) {
                                        const std::string number = std::to_string(index + 1);
                                        if (index < 0 || index >= static_cast<int>(tracks.size()))
                                            return number + ": " + Get("events.music.no_track");
                                        return number + ": " + std::filesystem::path(tracks[index]).stem().string();
                                    };

                                    Property(Get("events.music.track").c_str());
                                    if (ImGui::BeginCombo("##Track", trackLabel(payload.track).c_str())) {
                                        for (int t = 0; t < static_cast<int>(tracks.size()); ++t)
                                            if (ImGui::Selectable(trackLabel(t).c_str(), t == payload.track)) payload.track = t;
                                        if (tracks.empty()) ImGui::TextDisabled("%s", Get("events.music.no_tracks").c_str());
                                        ImGui::EndCombo();
                                    }
                                    Tooltip(Get("editor.tooltip.dispatches.music_track").c_str());
                                }

                                if (payload.action == PayloadDispatchMusic::SET_VOLUME) {
                                    Property(Get("events.music.volume").c_str());
                                    if (InputOrDrag("##Volume", &payload.volume, draggable, 0.01f))
                                        payload.volume = std::max(0.0f, payload.volume);
                                }
                            }
                            else if constexpr (std::is_same_v<Payload, PayloadDispatchSetUIText>) {
                                Property(Get("events.ui_text_part").c_str());
                                DrawEnumCombo("##Part", payload.part, kUITextPartOptions);
                                Tooltip(Get("editor.tooltip.dispatches.ui_text_part").c_str());

                                Property(Get("events.text").c_str());
                                ImGui::InputTextMultiline("##Text", &payload.text,
                                                          ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 3.0f));
                                Tooltip(Get("editor.tooltip.dispatches.set_ui_text").c_str());
                            }
                        }, dispatch.payload);

                        EndProperties();
                    }

                    // Too wide for the value column, so under the table.
                    if (auto *setSprite = std::get_if<PayloadDispatchEntitySetSprite>(&dispatch.payload)) {
                        ImGui::TextUnformatted(Get("events.sprite_textures").c_str());
                        Tooltip(Get("editor.tooltip.dispatches.sprite_textures").c_str());
                        DrawDirectionalTextureSlots(setSprite->textures, SIDECOUNT_45);
                    }
                }

                DrawDispatchFlows(dispatch, kind, ownerID);
            }
            EndEntryCard();

            if (deleteClicked) dispatchToRemove = static_cast<int>(i);

            ImGui::PopID();
        }

        if (dispatchToRemove >= 0) dispatches.erase(dispatches.begin() + dispatchToRemove);

        DrawAddEntryCombo("dispatches.add_placeholder", dispatches, kind, ownerID);
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
    // `showEnabled` is off where the component header already has the
    // Enabled checkbox (the entity inspector).
    static void DrawScriptAttachmentFields(ScriptAttachmentData &script, const std::string &ownerLabel,
                                           const bool showEnabled = true) {
        BeginSection("Script File");

        {
            PrefabField prefabField("fileName");
            if (MapEditorInternal::DrawAssetField(Get("component.script.file_name").c_str(), script.fileName, AssetKind::Script)) {
                LevelSystem::ReconcileScriptPublicValues(script, ownerLabel);
            }
            Tooltip(Get("editor.tooltip.component.script.file").c_str());
        }

        if (showEnabled) {
            {
                PrefabField prefabField("enabled");
                ImGui::Checkbox(Get("component.script.enabled").c_str(), &script.enabled);
            }

            ImGui::SameLine();
        }

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
        TextureBlock(id, surface.texture, textureTooltipKey, draggable, [&] {
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
    bool DrawSectorEditor(Sector &sector, bool *open, const bool draggable, bool *copyRequested,
                          MakeDoorRequest *makeDoorRequest, const char *windowId) {
        bool deleteRequested = false;
        int floorToRemove = -1;

        if (sector.floors.empty()) {
            sector.floors.push_back({
                {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
                {40.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}}
            });
        }

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin(InspectorWindowTitle(Get("sector.title"), windowId).c_str(), open, kInspectorFlags)) {
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

            // ── Make Door: its settings in a small popup first ───────────────
            if (makeDoorRequest != nullptr) {
                // Kept between doors, so a row of doors is quick to make.
                static float doorLip = 4.0f;
                static float doorWait = 3.0f;

                // Make Door, Make Teleporter and Make Lift share a row, at one fixed width.
                constexpr float MAKE_BUTTON_WIDTH = 130.0f;

                if (ImGui::Button(Get("sector.make_door").c_str(), ImVec2(MAKE_BUTTON_WIDTH, 0.0f))) ImGui::OpenPopup("make_door");
                Tooltip(Get("editor.tooltip.sector.make_door").c_str());

                ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f));
                if (ImGui::BeginPopup("make_door")) {
                    if (BeginProperties("##MakeDoor")) {
                        Property(Get("events.door.lip").c_str());
                        if (ImGui::DragFloat("##Lip", &doorLip, 0.25f, 0.0f, 10000.0f, "%.2f")) doorLip = std::max(0.0f, doorLip);
                        Tooltip(Get("editor.tooltip.dispatches.door_lip").c_str());

                        Property(Get("events.door.wait").c_str());
                        if (ImGui::DragFloat("##Wait", &doorWait, 0.05f, 0.0f, 10000.0f, "%.2f s")) doorWait = std::max(0.0f, doorWait);
                        Tooltip(Get("editor.tooltip.dispatches.door_wait").c_str());

                        EndProperties();
                    }

                    if (ImGui::Button(Get("sector.make_door_confirm").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                        makeDoorRequest->floorIndex = static_cast<int>(floorIndex);
                        makeDoorRequest->lip = doorLip;
                        makeDoorRequest->wait = doorWait;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                // ── Make Teleporter: the destination is clicked on the map ──
                if (makeDoorRequest->teleporterAllowed) {
                    ImGui::SameLine();
                    const bool waiting = MapEditorInternal::teleporterPick.has_value() &&
                                         MapEditorInternal::teleporterPick->sectorID == sector.id;
                    ImGui::BeginDisabled(waiting);
                    if (ImGui::Button(Get(waiting ? "sector.make_teleporter_waiting" : "sector.make_teleporter").c_str(), ImVec2(MAKE_BUTTON_WIDTH, 0.0f)))
                        makeDoorRequest->teleporterFloorIndex = static_cast<int>(floorIndex);
                    ImGui::EndDisabled();
                    Tooltip(Get("editor.tooltip.sector.make_teleporter").c_str());
                }

                // ── Make Lift: motion, triggers and heights in a popup ───
                ImGui::SameLine();
                if (ImGui::Button(Get("sector.make_lift").c_str(), ImVec2(MAKE_BUTTON_WIDTH, 0.0f))) ImGui::OpenPopup("make_lift");
                Tooltip(Get("editor.tooltip.sector.make_lift").c_str());

                ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f));
                if (ImGui::BeginPopup("make_lift")) {
                    MapEditorInternal::MakeLiftSettings &lift = MapEditorInternal::makeLiftSettings;
                    using Lift = PayloadDispatchSectorLift;

                    if (BeginProperties("##MakeLift")) {
                        Property(Get("sector.make_lift.motion").c_str());
                        DrawEnumCombo("##Motion", lift.motion, kMakeLiftMotionOptions);
                        Tooltip(Get("editor.tooltip.sector.make_lift.motion").c_str());

                        Property(Get("sector.make_lift.use_walls").c_str());
                        ImGui::Checkbox("##UseWalls", &lift.useWalls);
                        Tooltip(Get("editor.tooltip.sector.make_lift.use_walls").c_str());

                        Property(Get("sector.make_lift.step_on").c_str());
                        ImGui::Checkbox("##StepOn", &lift.stepOn);
                        Tooltip(Get("editor.tooltip.sector.make_lift.step_on").c_str());

                        Property(Get("sector.make_lift.on_start").c_str());
                        ImGui::Checkbox("##OnStart", &lift.onStart);
                        Tooltip(Get("editor.tooltip.sector.make_lift.on_start").c_str());

                        if (lift.GoesDown()) {
                            Property(Get("events.lift.low_mode").c_str());
                            DrawEnumCombo("##LowMode", lift.lowMode, kLiftLowModeOptions);
                            if (lift.lowMode == Lift::FIXED_LOW) {
                                Property(Get("events.lift.low_height").c_str());
                                ImGui::DragFloat("##LowHeight", &lift.lowHeight, 0.25f, 0.0f, 0.0f, "%.2f");
                            } else {
                                Property(Get("events.lift.low_lip").c_str());
                                ImGui::DragFloat("##LowLip", &lift.lowLip, 0.25f, 0.0f, 0.0f, "%.2f");
                                Tooltip(Get("editor.tooltip.dispatches.lift_low_lip").c_str());
                            }
                        }

                        if (lift.GoesUp()) {
                            Property(Get("events.lift.high_mode").c_str());
                            DrawEnumCombo("##HighMode", lift.highMode, kLiftHighModeOptions);
                            if (lift.highMode == Lift::FIXED_HIGH) {
                                Property(Get("events.lift.high_height").c_str());
                                ImGui::DragFloat("##HighHeight", &lift.highHeight, 0.25f, 0.0f, 0.0f, "%.2f");
                            } else {
                                Property(Get("events.lift.high_lip").c_str());
                                ImGui::DragFloat("##HighLip", &lift.highLip, 0.25f, 0.0f, 0.0f, "%.2f");
                                Tooltip(Get("editor.tooltip.dispatches.lift_high_lip").c_str());
                            }
                        }

                        Property(Get("events.speed").c_str());
                        if (ImGui::DragFloat("##Speed", &lift.speed, 0.25f, 0.01f, 100000.0f, "%.2f")) lift.speed = std::max(0.01f, lift.speed);
                        Tooltip(Get("editor.tooltip.dispatches.move_speed").c_str());

                        if (lift.UsesLiftDispatch()) {
                            Property(Get("events.door.wait").c_str());
                            if (ImGui::DragFloat("##Wait", &lift.wait, 0.05f, 0.0f, 10000.0f, "%.2f s")) lift.wait = std::max(0.0f, lift.wait);
                            Tooltip(Get("editor.tooltip.dispatches.lift_wait").c_str());
                        }

                        if (lift.motion == MapEditorInternal::MakeLiftSettings::DOWN_WAIT_UP ||
                            lift.motion == MapEditorInternal::MakeLiftSettings::UP_WAIT_DOWN) {
                            Property(Get("events.door.retrigger").c_str());
                            DrawEnumCombo("##Retrigger", lift.retrigger, kLiftRetriggerOptions);
                            Tooltip(Get("editor.tooltip.dispatches.lift_retrigger").c_str());
                        }

                        Property(Get("sector.make_lift.once_only").c_str());
                        ImGui::Checkbox("##OnceOnly", &lift.onceOnly);
                        Tooltip(Get("editor.tooltip.sector.make_lift.once_only").c_str());

                        if (lift.UsesLiftDispatch()) {
                            Property(Get("events.door.locked").c_str());
                            ImGui::Checkbox("##Locked", &lift.locked);
                            Tooltip(Get("editor.tooltip.dispatches.lift_locked").c_str());

                            if (lift.locked) {
                                Property(Get("events.door.required_tag").c_str());
                                DrawTagCombo("##RequiredTag", lift.requiredTag, TagRegistry::TagKind::NORMAL);
                                Tooltip(Get("editor.tooltip.dispatches.door_required_tag").c_str());
                            }
                        }

                        EndProperties();
                    }

                    if (ImGui::Button(Get("sector.make_lift_confirm").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                        makeDoorRequest->liftFloorIndex = static_cast<int>(floorIndex);
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
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

    // The anchor of one wall surface, as a property row; offset, scale and
    // flips live on each frame.
    static void DrawWallSurfaceRows(WallSurface &surface, [[maybe_unused]] const bool draggable) {
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
    bool DrawWallEditor(Wall &wall, bool *open, const bool draggable, const char *windowId, ID *openSectorRequest) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(InspectorWindowTitle(Get("wall.title"), windowId).c_str(), open, kInspectorFlags)) {
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

        // ── Sector buttons: Front + Back on a portal, one Sector otherwise ──
        if (openSectorRequest != nullptr) {
            const bool hasFront = wall.frontSector != INVALID_ID;
            const bool hasBack = wall.backSector != INVALID_ID;

            if (hasFront && hasBack) {
                const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                if (ImGui::Button(Get("wall.front_sector").c_str(), ImVec2(width, 0.0f)))
                    *openSectorRequest = wall.frontSector;
                Tooltip(Get("editor.tooltip.wall.edit_front_sector").c_str());

                ImGui::SameLine();
                if (ImGui::Button(Get("wall.back_sector").c_str(), ImVec2(-FLT_MIN, 0.0f)))
                    *openSectorRequest = wall.backSector;
                Tooltip(Get("editor.tooltip.wall.edit_back_sector").c_str());
            }
            else if (hasFront || hasBack) {
                if (ImGui::Button(Get("sector.title").c_str(), ImVec2(-FLT_MIN, 0.0f)))
                    *openSectorRequest = hasFront ? wall.frontSector : wall.backSector;
                Tooltip(Get("editor.tooltip.wall.edit_sector").c_str());
            }
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
                        TextureBlock("Top", wall.top.texture, "editor.tooltip.wall.texture", draggable,
                                     [&] { DrawWallSurfaceRows(wall.top, draggable); });
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem(Get("wall.texture_bottom").c_str())) {
                        TextureBlock("Bottom", wall.bottom.texture, "editor.tooltip.wall.texture", draggable,
                                     [&] { DrawWallSurfaceRows(wall.bottom, draggable); });
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem(Get("wall.texture_middle").c_str())) {
                        TextureBlock("Middle", wall.middle.texture, "editor.tooltip.wall.texture_middle", draggable,
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
            else TextureBlock("Top", wall.top.texture, "editor.tooltip.wall.texture", draggable,
                              [&] { DrawWallSurfaceRows(wall.top, draggable); });
        }

        ImGui::Checkbox(Get("wall.blocks_pathfinders").c_str(), &wall.blocksPathfinders);
        Tooltip(Get("editor.tooltip.wall.blocks_pathfinders").c_str());

        ImGui::Checkbox(Get("wall.blocks_sound").c_str(), &wall.blocksSound);
        Tooltip(Get("editor.tooltip.wall.blocks_sound").c_str());

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

    void DrawEntityEventSections(Entity &entity, const bool draggable) {
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
    }

    bool DrawLinedefEditor(Linedef &linedef, bool *open, const bool draggable, const char *windowId) {
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(InspectorWindowTitle(Get("linedef.title"), windowId).c_str(), open, kInspectorFlags)) {
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

        ImGui::Checkbox(Get("linedef.blocks_pathfinders").c_str(), &linedef.blocksPathfinders);
        Tooltip(Get("editor.tooltip.linedef.blocks_pathfinders").c_str());
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

    }

    void FlushPrefabActions() {
        RunPendingPrefabAction();
    }

    namespace {
        // The ⋮ menu button: three dots drawn by hand, so no icon font is needed.
        bool MenuDotsButton(const char *id, const float size) {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            if (ImGui::IsItemHovered())
                drawList->AddRectFilled(min, ImVec2(min.x + size, min.y + size), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 3.0f);

            const float radius = std::max(1.5f, size * 0.07f);
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            for (int i = -1; i <= 1; ++i)
                drawList->AddCircleFilled(ImVec2(min.x + size * 0.5f, min.y + size * 0.5f + static_cast<float>(i) * size * 0.22f),
                                          radius, color);

            return pressed;
        }
    }

    // The component's Persist Between Levels "take from level copy" flag, in
    // the current level.
    template<typename Storage>
    static bool *LevelCopyFlagIn(Storage &storage, const std::uint64_t instanceID) {
        auto *component = storage.GetInstance(instanceID);
        return component != nullptr ? &component->takeFromLevelCopy : nullptr;
    }

    static bool *LevelCopyFlagIn(ScriptComponentStorage &storage, const std::uint64_t instanceID) {
        ComponentScript *script = storage.GetByID(instanceID);
        return script != nullptr ? &script->takeFromLevelCopy : nullptr;
    }

    static bool *TakeFromLevelCopyFlag(const int componentType, const std::uint64_t instanceID) {
        Level &level = LevelManager::CurrentLevel();

        switch (componentType) {
#define TILKY_LEVEL_COPY_FLAG(Type, Bit, Storage, LabelKey) case Bit: return LevelCopyFlagIn(level.Storage, instanceID);
            TILKY_COMPONENTS(TILKY_LEVEL_COPY_FLAG)
#undef TILKY_LEVEL_COPY_FLAG
            default: return nullptr;
        }
    }

    void DrawPersistenceFields(Entity &entity) {
        ImGui::PushID("persistence");

        {
            PrefabField prefabField("persistent");
            ImGui::Checkbox(Get("entity.persist_between_levels").c_str(), &entity.persistent);
            Tooltip(Get("editor.tooltip.entity.persist_between_levels").c_str());
        }

        if (entity.persistent) {
            PrefabField prefabField("persistenceKey");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##PersistenceKey", Get("entity.persistence_key_hint").c_str(), &entity.persistenceKey);
            Tooltip(Get("editor.tooltip.entity.persistence_key").c_str());
        }

        ImGui::PopID();
    }

    ComponentHeaderResult DrawComponentHeader(const Entity &entity, const ComponentHeader &header) {
        ComponentHeaderResult result;

        // Only a persistent entity has a level copy to take values from.
        bool *levelCopyFlag = entity.persistent ? TakeFromLevelCopyFlag(header.componentType, header.instanceID) : nullptr;

        // Remembered per type, so collapsing Transform once keeps every
        // entity's Transform collapsed. Each script file counts as its own type.
        std::string collapseKey = Prefab::ComponentKey(header.componentType);
        if (header.componentType == CMP_SCRIPT)
            if (const ComponentScript *script = LevelManager::CurrentLevel().scripts.GetByID(header.instanceID))
                collapseKey += ":" + script->fileName;
        std::set<std::string> &collapsed = MapEditorInternal::collapsedComponentTypes;
        const bool wasOpen = !collapsed.contains(collapseKey);

        ImGui::PushID(header.componentType);
        ImGui::PushID(std::to_string(header.instanceID).c_str());

        ImGui::SetNextItemOpen(wasOpen, ImGuiCond_Always);

        // Only the arrow and the hit area: the checkbox, the label and the
        // menu button are drawn over it below.
        result.open = ImGui::CollapsingHeader("##component_header", ImGuiTreeNodeFlags_AllowOverlap);

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const ImVec2 afterHeader = ImGui::GetCursorScreenPos();
        bool openMenu = ImGui::IsItemClicked(ImGuiMouseButton_Right);

        if (result.open != wasOpen) {
            if (result.open) collapsed.erase(collapseKey);
            else collapsed.insert(collapseKey);
            MapEditorInternal::SaveUserSettings();
        }

        DrawPrefabCardMarks(entity, header.componentType, header.instanceID);
        if (header.onHeaderItem) header.onHeaderItem();

        const ImGuiStyle &style = ImGui::GetStyle();
        const float height = headerMax.y - headerMin.y;
        const float menuSize = height;
        float labelX = headerMin.x + ImGui::GetTreeNodeToLabelSpacing();

        if (header.enabled != nullptr) {
            ImGui::SetCursorScreenPos(ImVec2(labelX, headerMin.y + (height - ImGui::GetFrameHeight()) * 0.5f));
            if (ImGui::Checkbox("##enabled", header.enabled)) result.enabledChanged = true;
            if (header.enabledTooltip != nullptr) Tooltip(header.enabledTooltip);
            labelX = ImGui::GetItemRectMax().x + style.ItemInnerSpacing.x;
        }

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(ImVec2(labelX, headerMin.y), ImVec2(headerMax.x - menuSize, headerMax.y), true);
        const ImVec2 labelPos(labelX, headerMin.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(labelPos, ImGui::GetColorU32(ImGuiCol_Text), header.label.c_str());

        if (levelCopyFlag != nullptr && *levelCopyFlag) {
            const std::string marker = "  " + Get("entity.component_menu.level_copy_marker");
            drawList->AddText(ImVec2(labelPos.x + ImGui::CalcTextSize(header.label.c_str()).x, labelPos.y),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), marker.c_str());
        }

        drawList->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(headerMax.x - menuSize, headerMin.y));
        if (MenuDotsButton("##component_menu_button", menuSize)) openMenu = true;
        Tooltip(Get("editor.tooltip.entity.component_menu").c_str());

        if (openMenu) ImGui::OpenPopup("component_menu");

        if (ImGui::BeginPopup("component_menu")) {
            ImGui::TextDisabled("%s", header.label.c_str());
            ImGui::Separator();

            if (ImGui::MenuItem(Get("entity.component_menu.move_up").c_str(), nullptr, false, header.canMoveUp))
                result.moveUpRequested = true;
            if (ImGui::MenuItem(Get("entity.component_menu.move_down").c_str(), nullptr, false, header.canMoveDown))
                result.moveDownRequested = true;

            if (entity.prefabLink.IsInstance() &&
                header.componentType != CMP_TRANSFORM && header.componentType != CMP_UI_TRANSFORM) {
                ImGui::Separator();
                DrawPrefabComponentMenuItems(entity, header.componentType, header.instanceID);
            }

            if (levelCopyFlag != nullptr) {
                ImGui::Separator();
                ImGui::MenuItem(Get("entity.component_menu.take_from_level_copy").c_str(), nullptr, levelCopyFlag);
                Tooltip(Get("editor.tooltip.entity.take_from_level_copy").c_str());
            }

            ImGui::Separator();
            if (ImGui::MenuItem(Get("entity.component_menu.remove").c_str(), nullptr, false, header.canRemove))
                result.removeRequested = true;

            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(afterHeader);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));

        ImGui::PopID();
        ImGui::PopID();
        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Entity Editor
    // ─────────────────────────────────────────────────────────────────────────
    bool DrawEntityEditor(Entity &entity, EntityInspectorState &state, bool *open, const bool draggable) {
        particlePreviewAllowed = state.allowParticlePreview;
        bool deleteRequested = false;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        const std::string entityWindowTitle = state.windowTitle.empty() ? Get("entity.title") : state.windowTitle;

        const bool windowVisible = ImGui::Begin(entityWindowTitle.c_str(), open, kInspectorFlags);

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
                    deleteRequested = true;
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

        DrawPersistenceFields(entity);

        // ── Components ───────────────────────────────────────────────────────
        // One foldout per component instance, Unity style. An entity can have
        // several of every type except Transform; headers of the same type
        // can be dragged onto each other to reorder them - the first one is
        // what entity.<component> returns in Lua. Removing and moving wait
        // until every section is drawn: they shift components around in
        // their storages.
        struct ComponentDragPayload {
            int componentType;
            std::uint64_t instanceID;
        };

        struct PendingComponentChange {
            enum class Kind { None, Remove, Move };
            Kind kind = Kind::None;
            int componentType = -1;
            std::uint64_t instanceID = 0;
            size_t newIndex = 0;
        };
        PendingComponentChange pendingChange;

        // The on/off flag the header's checkbox shows, for the components
        // that have one. Collider, Camera and Player Controller need more
        // than the write, so the checkbox edits a copy.
        auto DrawComponentFoldout = [&](
            const std::string &label,
            const int componentType,
            const std::uint64_t instanceID,
            const size_t indexInType,
            const size_t countInType
        ) {
            BeginPrefabFields(entity, componentType, instanceID);

            ComponentHeader header;
            header.label = PrefabCardLabel(entity, componentType, instanceID, label);
            header.componentType = componentType;
            header.instanceID = instanceID;
            header.canMoveUp = componentType != CMP_TRANSFORM && indexInType > 0;
            header.canMoveDown = componentType != CMP_TRANSFORM && indexInType + 1 < countInType;

            bool enabled = true;
            const std::string enabledTooltip = Get("editor.tooltip.entity.component_enabled");
            header.enabledTooltip = enabledTooltip.c_str();

            switch (componentType) {
                case CMP_SPRITE:
                    if (const auto *c = entity.GetComponentInstance<ComponentSprite>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                case CMP_SCRIPT:
                    if (const auto *c = entity.GetScript(instanceID)) { enabled = c->enabled; header.enabled = &enabled; }
                    break;
                case CMP_COLLIDER:
                    if (const auto *c = entity.GetComponentInstance<ComponentCollider>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                case CMP_CAMERA:
                    if (const auto *c = entity.GetComponentInstance<ComponentCamera>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                case CMP_PLAYER_CONTROLLER:
                    if (const auto *c = entity.GetComponentInstance<ComponentPlayerController>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                case CMP_PATHFINDER:
                    if (const auto *c = entity.GetComponentInstance<ComponentPathfinder>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                case CMP_MONSTER_AI:
                    if (const auto *c = entity.GetComponentInstance<ComponentMonsterAI>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                case CMP_PARTICLE_EMITTER:
                    if (const auto *c = entity.GetComponentInstance<ComponentParticleEmitter>(instanceID)) { enabled = c->isActive; header.enabled = &enabled; }
                    break;
                default: break;
            }

            if (componentType != CMP_TRANSFORM) {
                header.onHeaderItem = [&] {
                    if (ImGui::BeginDragDropSource()) {
                        const ComponentDragPayload payload{componentType, instanceID};
                        ImGui::SetDragDropPayload("TILKY_COMPONENT", &payload, sizeof(payload));
                        ImGui::TextUnformatted(label.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload *dropped = ImGui::AcceptDragDropPayload("TILKY_COMPONENT")) {
                            const auto *payload = static_cast<const ComponentDragPayload *>(dropped->Data);
                            if (payload->componentType == componentType && payload->instanceID != instanceID)
                                pendingChange = {PendingComponentChange::Kind::Move, componentType, payload->instanceID, indexInType};
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
                };
            }

            const ComponentHeaderResult result = DrawComponentHeader(entity, header);

            if (result.enabledChanged) {
                Level &level = LevelManager::CurrentLevel();

                switch (componentType) {
                    case CMP_SPRITE:
                        if (auto *c = entity.GetComponentInstance<ComponentSprite>(instanceID)) c->isActive = enabled;
                        break;
                    case CMP_SCRIPT:
                        if (auto *c = entity.GetScript(instanceID)) c->enabled = enabled;
                        break;
                    // Through SetActive, so physics moves it in or out of the active colliders.
                    case CMP_COLLIDER: level.colliders.SetActive(instanceID, enabled); break;
                    case CMP_CAMERA:
                        if (auto *c = entity.GetComponentInstance<ComponentCamera>(instanceID)) {
                            c->isActive = enabled;
                            if (enabled) level.ActivateCamera(*c);
                        }
                        break;
                    case CMP_PLAYER_CONTROLLER:
                        if (auto *c = entity.GetComponentInstance<ComponentPlayerController>(instanceID)) {
                            c->isActive = enabled;
                            if (enabled) level.ActivatePlayerController(*c);
                        }
                        break;
                    case CMP_PATHFINDER:
                        if (auto *c = entity.GetComponentInstance<ComponentPathfinder>(instanceID)) c->isActive = enabled;
                        break;
                    case CMP_PARTICLE_EMITTER:
                        if (auto *c = entity.GetComponentInstance<ComponentParticleEmitter>(instanceID)) c->isActive = enabled;
                        break;
                    case CMP_MONSTER_AI:
                        if (auto *c = entity.GetComponentInstance<ComponentMonsterAI>(instanceID)) c->isActive = enabled;
                        break;
                    default: break;
                }
            }

            if (result.removeRequested)
                pendingChange = {PendingComponentChange::Kind::Remove, componentType, instanceID};
            if (result.moveUpRequested)
                pendingChange = {PendingComponentChange::Kind::Move, componentType, instanceID, indexInType - 1};
            if (result.moveDownRequested)
                pendingChange = {PendingComponentChange::Kind::Move, componentType, instanceID, indexInType + 1};

            if (result.open) {
                ImGui::PushID(componentType);
                ImGui::PushID(std::to_string(instanceID).c_str());
                DrawComponentFields(entity, componentType, instanceID, draggable);
                ImGui::PopID();
                ImGui::PopID();
            }
        };

        // "Sprite", or "Sprite 1", "Sprite 2"... when there are several.
        auto DrawComponentFoldouts = [&](const int componentType, const std::string &name, const std::vector<std::uint64_t> &instances) {
            for (size_t i = 0; i < instances.size(); ++i) {
                const std::string label = instances.size() > 1 ? name + " " + std::to_string(i + 1) : name;
                DrawComponentFoldout(label, componentType, instances[i], i, instances.size());
            }
        };

#define DRAW_ENTITY_COMPONENT_FOLDOUTS(Type, Bit, Storage, LabelKey) \
        if constexpr (Bit != CMP_SCRIPT) DrawComponentFoldouts(Bit, Get(LabelKey), ComponentInstancesOf<Type>(entity));

        TILKY_NORMAL_COMPONENTS(DRAW_ENTITY_COMPONENT_FOLDOUTS)

#undef DRAW_ENTITY_COMPONENT_FOLDOUTS

        // Scripts are labelled by their file instead of the component name.
        {
            std::vector<std::uint64_t> scriptIDs;
            std::vector<std::string> scriptLabels;

            for (const ComponentScript *script : entity.GetScripts()) {
                scriptIDs.push_back(script->instanceID);
                scriptLabels.push_back(script->fileName.empty()
                    ? std::string("Script (unassigned)")
                    : std::filesystem::path(script->fileName).filename().string());
            }

            for (size_t i = 0; i < scriptIDs.size(); ++i)
                DrawComponentFoldout(scriptLabels[i], CMP_SCRIPT, scriptIDs[i], i, scriptIDs.size());
        }

        BeginPrefabFields(entity, -1, 0);

        switch (pendingChange.kind) {
            case PendingComponentChange::Kind::Remove:
                RemoveEditorComponentByType(entity, pendingChange.componentType, pendingChange.instanceID);
                break;
            case PendingComponentChange::Kind::Move:
                MoveComponentInstance(pendingChange.componentType, pendingChange.instanceID, pendingChange.newIndex);
                break;
            case PendingComponentChange::Kind::None: break;
        }

        // "Add component..." - picking a type adds it straight away.
        {
            ImGui::PushID("add_component_combo");

            std::array<std::string, CMP_COUNT> componentNames{};

#define FILL_COMPONENT_NAME(Type, Bit, Storage, LabelKey) \
            componentNames[Bit] = Get(LabelKey);
            TILKY_NORMAL_COMPONENTS(FILL_COMPONENT_NAME)
#undef FILL_COMPONENT_NAME

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            FieldWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##AddComponent", Get("entity.add_component_placeholder").c_str())) {
                for (int i = 0; i < CMP_COUNT; i++) {
                    if (i == CMP_TRANSFORM || componentNames[i].empty()) continue;
                    if (ImGui::Selectable(componentNames[i].c_str())) AddEditorComponentByType(entity, i);
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
        DrawEntityEventSections(entity, draggable);

        // Runs a Revert/Apply picked from the bar, a card or a field.
        EndPrefabFields();

        ImGui::End();
        return deleteRequested;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Component fields
    // ─────────────────────────────────────────────────────────────────────────
    void DrawComponentFields(Entity &entity, const int componentType, const std::uint64_t instanceID, const bool draggable) {
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

        // ════════════════════════════════════════════════════════════════════
        //  Transform
        // ════════════════════════════════════════════════════════════════════
        if (componentType == CMP_TRANSFORM) {
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
            }
            else ImGui::TextDisabled("Transform component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Sprite
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_SPRITE) {
            auto *c = entity.GetComponentInstance<ComponentSprite>(instanceID);

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

                DrawOffsetSection(c->offset);
            }
            else ImGui::TextDisabled("Sprite component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Audio Source
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_AUDIO_SOURCE) {
            auto *c = entity.GetComponentInstance<ComponentAudioSource>(instanceID);
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
                ImGui::BeginDisabled(c->soundFileName.empty());
                if (ImGui::Button(Get("component.audio_source.play_sound").c_str()))
                    SoundManager::PlayPreviewSound(c->soundFileName, c->pitch, c->gain);
                ImGui::EndDisabled();
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
            } else { ImGui::TextDisabled("Audio component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Script
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_SCRIPT) {
            // The specific instance, not the first-by-owner script - an
            // Entity may have several scripts attached.
            auto *c = entity.GetScript(instanceID);

            if (c) {
                DrawScriptAttachmentFields(*c, "entity " + std::to_string(entity.id), false);
            }
            else ImGui::TextDisabled("Script component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Player Controller
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_PLAYER_CONTROLLER) {
            auto *c = entity.GetComponentInstance<ComponentPlayerController>(instanceID);
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
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("sensitivityX");
                    ImGui::SliderFloat(Get("component.player_controller.sensitivity_x").c_str(),
                        &c->sensitivityX, 0.001f, 2.0f);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("sensitivityY");
                    ImGui::SliderFloat(Get("component.player_controller.sensitivity_y").c_str(),
                                       &c->sensitivityY, 0.001f, 2.0f);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("maxPitch");
                    ImGui::SliderFloat(Get("component.player_controller.max_pitch").c_str(),
                                       &c->maxPitch, 0.0f, 360.0f);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("minPitch");
                    ImGui::SliderFloat(Get("component.player_controller.min_pitch").c_str(),
                                       &c->minPitch, 0.0f, 360.0f);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("maxYaw");
                    ImGui::SliderFloat(Get("component.player_controller.max_yaw").c_str(),
                                       &c->maxYaw, 0.0f, 360.0f);
                }
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("minYaw");
                    ImGui::SliderFloat(Get("component.player_controller.min_yaw").c_str(),
                                       &c->minYaw, 0.0f, 360.0f);
                }
                if (c->minYaw < .0f) c->minYaw = 0.0f;

                EndSection();

                const auto KeyBindButton = [](const char* fieldName, SDL_Scancode* key, const std::string& label) {
                    PrefabField prefabField(fieldName);
                    DrawKeyBindButton(fieldName, key);

                    ImGui::SameLine();
                    ImGui::TextUnformatted(label.c_str());
                };

                BeginSection("Use");
                KeyBindButton("useKey", &c->useKey, Get("component.player_controller.use_key"));
                Tooltip(Get("editor.tooltip.component.player_controller.use_key").c_str());
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

                BeginSection("Keybinds");
                KeyBindButton("forwardKey", &c->forward, Get("component.player_controller.key_bind.forward"));
                KeyBindButton("backwardsKey", &c->backwards, Get("component.player_controller.key_bind.backward"));
                KeyBindButton("leftKey", &c->left, Get("component.player_controller.key_bind.left"));
                KeyBindButton("rightKey", &c->right, Get("component.player_controller.key_bind.right"));
                KeyBindButton("jumpKey", &c->jump, Get("component.player_controller.key_bind.jump"));
                KeyBindButton("runKey", &c->run, Get("component.player_controller.key_bind.run"));
                EndSection();
            } else { ImGui::TextDisabled("Player Controller component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Camera
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_CAMERA) {
            auto *c = entity.GetComponentInstance<ComponentCamera>(instanceID);
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
            } else { ImGui::TextDisabled("Camera component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Collider
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_COLLIDER) {
            auto *c = entity.GetComponentInstance<ComponentCollider>(instanceID);
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
                            instanceID, static_cast<ColliderType>(selectedColliderIndex));
                        c = entity.GetComponentInstance<ComponentCollider>(instanceID);
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
                {
                    PrefabField prefabField("stepSize");
                    FieldWidth(160.0f);
                    InputOrDrag(Get("component.collider.step_size").c_str(), &c->stepSize, draggable);
                    Tooltip(Get("editor.tooltip.component.collider.step_size").c_str());
                }
                {
                    PrefabField prefabField("maxSlope");
                    FieldWidth(160.0f);
                    InputOrDrag(Get("component.collider.max_slope").c_str(), &c->maxSlope, draggable);
                    Tooltip(Get("editor.tooltip.component.collider.max_slope").c_str());
                    c->maxSlope = std::clamp(c->maxSlope, 0.0f, 90.0f);
                }
                {
                    PrefabField prefabField("isTrigger");
                    ImGui::Checkbox(Get("component.collider.is_trigger").c_str(), &c->isTrigger);
                    Tooltip(Get("editor.tooltip.component.collider.is_trigger").c_str());
                }
                EndSection();

                DrawOffsetSection(c->offset, "editor.tooltip.component.collider.offset", 0.01f);
            } else ImGui::TextDisabled("Collider component missing");
        }

        // ════════════════════════════════════════════════════════════════════
        //  Rigidbody
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_RIGIDBODY) {
            auto *c = entity.GetComponentInstance<ComponentRigidbody>(instanceID);
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
            } else { ImGui::TextDisabled("Rigidbody component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Model
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_MODEL) {
            auto *c = entity.GetComponentInstance<ComponentModel>(instanceID);
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
            } else { ImGui::TextDisabled("Model component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Flipbook
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_FLIPBOOK) {
            auto *c = entity.GetComponentInstance<ComponentFlipbook>(instanceID);
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
            } else { ImGui::TextDisabled("Flipbook component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Pathfinder
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_PATHFINDER) {
            auto *c = entity.GetComponentInstance<ComponentPathfinder>(instanceID);
            if (c) {
                if (entity.GetComponent<ComponentPathfinder>() != c)
                    ImGui::TextWrapped("%s", Get("component.pathfinder.not_first").c_str());

                BeginSection(Get("component.pathfinder.destination").c_str());
                {
                    PrefabField prefabField("input");
                    const std::string inputLabels[] = {
                        Get("component.pathfinder.input.position"), Get("component.pathfinder.input.entity")
                    };
                    const int input = static_cast<int>(c->input);
                    FieldWidth(160.0f);
                    if (ImGui::BeginCombo(Get("component.pathfinder.input").c_str(), inputLabels[input].c_str())) {
                        for (int i = 0; i < 2; ++i)
                            if (ImGui::Selectable(inputLabels[i].c_str(), input == i)) c->input = static_cast<PathfinderInput>(i);
                        ImGui::EndCombo();
                    }
                    Tooltip(Get("editor.tooltip.component.pathfinder.input").c_str());
                }

                if (c->input == PathfinderInput::POSITION) {
                    PrefabField prefabField("destination");
                    ImGui::TextDisabled("X                Y               Z");
                    FieldWidth(220.0f);
                    InputOrDrag3("##destination", &c->destination.x, draggable);
                    Tooltip(Get("editor.tooltip.component.pathfinder.destination").c_str());
                    ResetFloat3Button("rst_destination", &c->destination.x);
                }
                else {
                    {
                        PrefabField prefabField("targetEntityID");
                        Level &level = LevelManager::CurrentLevel();
                        const Entity *target = c->targetEntityID == INVALID_ID ? nullptr : level.GetEntity(c->targetEntityID);
                        const ID selfID = entity.id;

                        DrawLevelObjectRefField(
                            Get("component.pathfinder.target").c_str(), c->targetEntityID,
                            target != nullptr ? DescribeEntity(*target) : "(None)",
                            MapEditorInternal::ENTITY_REF_PAYLOAD, "editor.tooltip.component.pathfinder.target",
                            [&level, selfID](ID &id, bool &changed) {
                                for (const Entity &candidate : level.entities) {
                                    if (candidate.id == selfID) continue;
                                    const bool isSelected = candidate.id == id;
                                    if (ImGui::Selectable(DescribeEntity(candidate).c_str(), isSelected)) {
                                        id = candidate.id;
                                        changed = true;
                                    }
                                    if (isSelected) ImGui::SetItemDefaultFocus();
                                }
                            }
                        );
                        // Dropped onto itself from the canvas.
                        if (c->targetEntityID == selfID) c->targetEntityID = INVALID_ID;
                    }
                    {
                        PrefabField prefabField("offset");
                        ImGui::TextUnformatted(Get("component.pathfinder.offset").c_str());
                        ImGui::TextDisabled("X                Y               Z");
                        FieldWidth(220.0f);
                        InputOrDrag3("##target_offset", &c->offset.x, draggable);
                        Tooltip(Get("editor.tooltip.component.pathfinder.offset").c_str());
                        ResetFloat3Button("rst_target_offset", &c->offset.x);
                    }
                }
                EndSection();

                BeginSection(Get("component.pathfinder.movement").c_str());
                FieldWidth(160.0f);
                {
                    PrefabField prefabField("speed");
                    InputOrDrag(Get("component.pathfinder.speed").c_str(), &c->speed, draggable);
                    Tooltip(Get("editor.tooltip.component.pathfinder.speed").c_str());
                    c->speed = std::max(c->speed, 0.0f);
                }
                {
                    PrefabField prefabField("metadata");
                    bool air = c->IsAirEntity();
                    if (ImGui::Checkbox(Get("component.pathfinder.air").c_str(), &air)) c->SetFlag(ComponentPathfinder::IS_AIR_ENTITY, air);
                    Tooltip(Get("editor.tooltip.component.pathfinder.air").c_str());

                    bool ranged = c->IsRangedEntity();
                    if (ImGui::Checkbox(Get("component.pathfinder.ranged").c_str(), &ranged)) c->SetFlag(ComponentPathfinder::IS_RANGED_ENTITY, ranged);
                    Tooltip(Get("editor.tooltip.component.pathfinder.ranged").c_str());
                }
                if (c->IsRangedEntity()) {
                    PrefabField prefabField("range");
                    FieldWidth(160.0f);
                    InputOrDrag(Get("component.pathfinder.range").c_str(), &c->range, draggable);
                    Tooltip(Get("editor.tooltip.component.pathfinder.range").c_str());
                    c->range = std::max(c->range, 0.0f);
                }
                {
                    PrefabField prefabField("metadata");
                    if (c->IsRangedEntity()) {
                        bool keepMoving = c->KeepsMovingInRange();
                        if (ImGui::Checkbox(Get("component.pathfinder.keep_moving").c_str(), &keepMoving))
                            c->SetFlag(ComponentPathfinder::KEEP_MOVING_IN_RANGE, keepMoving);
                        Tooltip(Get("editor.tooltip.component.pathfinder.keep_moving").c_str());
                    }

                    bool dropOff = c->CanDropOff();
                    if (ImGui::Checkbox(Get("component.pathfinder.can_drop_off").c_str(), &dropOff))
                        c->SetFlag(ComponentPathfinder::CAN_DROP_OFF, dropOff);
                    Tooltip(Get("editor.tooltip.component.pathfinder.can_drop_off").c_str());

                    bool doors = c->OpensDoors();
                    if (ImGui::Checkbox(Get("component.pathfinder.opens_doors").c_str(), &doors))
                        c->SetFlag(ComponentPathfinder::OPENS_DOORS, doors);
                    Tooltip(Get("editor.tooltip.component.pathfinder.opens_doors").c_str());
                }
                EndSection();

                BeginSection(Get("component.pathfinder.facing_section").c_str());
                {
                    PrefabField prefabField("facing");
                    const std::string labels[] = {
                        Get("component.pathfinder.facing.move_direction"), Get("component.pathfinder.facing.target"), Get("component.pathfinder.facing.none")
                    };
                    const int value = static_cast<int>(c->facing);
                    FieldWidth(160.0f);
                    if (ImGui::BeginCombo(Get("component.pathfinder.facing").c_str(), labels[value].c_str())) {
                        for (int i = 0; i < 3; ++i)
                            if (ImGui::Selectable(labels[i].c_str(), value == i)) c->facing = static_cast<PathfinderFacing>(i);
                        ImGui::EndCombo();
                    }
                    Tooltip(Get("editor.tooltip.component.pathfinder.facing").c_str());
                }
                if (c->facing == PathfinderFacing::MOVE_DIRECTION) {
                    PrefabField prefabField("turning");
                    const std::string labels[] = {Get("component.pathfinder.turning.steps_45"), Get("component.pathfinder.turning.snap")};
                    const int value = static_cast<int>(c->turning);
                    FieldWidth(160.0f);
                    if (ImGui::BeginCombo(Get("component.pathfinder.turning").c_str(), labels[value].c_str())) {
                        for (int i = 0; i < 2; ++i)
                            if (ImGui::Selectable(labels[i].c_str(), value == i)) c->turning = static_cast<PathfinderTurning>(i);
                        ImGui::EndCombo();
                    }
                    Tooltip(Get("editor.tooltip.component.pathfinder.turning").c_str());
                }
                EndSection();

                BeginSection(Get("component.pathfinder.doom_numbers").c_str());
                {
                    const auto number = [&](const char *field, const char *labelKey, const char *tooltipKey, float *value) {
                        PrefabField prefabField(field);
                        FieldWidth(160.0f);
                        InputOrDrag(Get(labelKey).c_str(), value, draggable);
                        Tooltip(Get(tooltipKey).c_str());
                        *value = std::max(*value, 0.0f);
                    };

                    number("chaseStep", "component.pathfinder.chase_step", "editor.tooltip.component.pathfinder.chase_step", &c->chaseStep);
                    number("holdMinSteps", "component.pathfinder.hold_min", "editor.tooltip.component.pathfinder.hold_min", &c->holdMinSteps);
                    number("holdMaxSteps", "component.pathfinder.hold_max", "editor.tooltip.component.pathfinder.hold_max", &c->holdMaxSteps);
                    number("deadzone", "component.pathfinder.deadzone", "editor.tooltip.component.pathfinder.deadzone", &c->deadzone);
                    number("swapChance", "component.pathfinder.swap_chance", "editor.tooltip.component.pathfinder.swap_chance", &c->swapChance);
                    if (c->IsAirEntity()) {
                        number("floatSpeed", "component.pathfinder.float_speed", "editor.tooltip.component.pathfinder.float_speed", &c->floatSpeed);
                        number("floatRatio", "component.pathfinder.float_ratio", "editor.tooltip.component.pathfinder.float_ratio", &c->floatRatio);
                    }
                    number("pushFriction", "component.pathfinder.push_friction", "editor.tooltip.component.pathfinder.push_friction", &c->pushFriction);
                    c->pushFriction = std::min(c->pushFriction, 1.0f);
                    number("stuckTime", "component.pathfinder.stuck_time", "editor.tooltip.component.pathfinder.stuck_time", &c->stuckTime);
                }
                EndSection();

                if (!entity.HasComponent<ComponentRigidbody>())
                    ImGui::TextWrapped("%s", Get("component.pathfinder.no_rigidbody").c_str());
            } else { ImGui::TextDisabled("Pathfinder component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Particle Emitter
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_PARTICLE_EMITTER) {
            auto *c = entity.GetComponentInstance<ComponentParticleEmitter>(instanceID);
            if (c) {
                const auto number = [&](const char *field, const char *key, float *value, const float low = 0.0f) {
                    PrefabField prefabField(field);
                    FieldWidth(160.0f);
                    InputOrDrag(Get(std::string("component.particle_emitter.") + key).c_str(), value, draggable);
                    Tooltip(Get(std::string("editor.tooltip.component.particle_emitter.") + key).c_str());
                    *value = std::max(*value, low);
                };
                const auto whole = [&](const char *field, const char *key, int *value) {
                    PrefabField prefabField(field);
                    FieldWidth(160.0f);
                    ImGui::InputInt(Get(std::string("component.particle_emitter.") + key).c_str(), value);
                    Tooltip(Get(std::string("editor.tooltip.component.particle_emitter.") + key).c_str());
                    *value = std::max(*value, 0);
                };
                const auto flag = [&](const char *field, const char *key, bool *value) {
                    PrefabField prefabField(field);
                    ImGui::Checkbox(Get(std::string("component.particle_emitter.") + key).c_str(), value);
                    Tooltip(Get(std::string("editor.tooltip.component.particle_emitter.") + key).c_str());
                };
                const auto colorField = [&](const char *field, const char *key, Vector4 *value) {
                    PrefabField prefabField(field);
                    FieldWidth(220.0f);
                    ImGui::ColorEdit4(Get(std::string("component.particle_emitter.") + key).c_str(), &value->x);
                    Tooltip(Get(std::string("editor.tooltip.component.particle_emitter.") + key).c_str());
                };
                const auto textureList = [&](const char *field, const char *key, std::vector<std::string> &list) {
                    PrefabField prefabField(field);
                    ImGui::PushID(field);
                    ImGui::TextUnformatted(Get(std::string("component.particle_emitter.") + key).c_str());
                    Tooltip(Get(std::string("editor.tooltip.component.particle_emitter.") + key).c_str());
                    for (std::size_t i = 0; i < list.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        MapEditorInternal::DrawAssetField("", list[i], AssetKind::Texture, 40.0f);
                        ImGui::SameLine();
                        const bool removed = ImGui::SmallButton("X");
                        ImGui::PopID();
                        if (removed) {
                            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                            break;
                        }
                    }
                    if (ImGui::SmallButton(Get("component.particle_emitter.add_texture").c_str())) list.emplace_back();
                    ImGui::PopID();
                };

                if (particlePreviewAllowed) {
                    ImGui::PushID("ParticlePreview");
                    if (ImGui::Button(Get("component.particle_emitter.preview_play").c_str())) ParticleSystem::PreviewPlay(*c);
                    Tooltip(Get("editor.tooltip.component.particle_emitter.preview_play").c_str());
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!ParticleSystem::IsPreviewing(c->instanceID));
                    if (ImGui::Button(Get("component.particle_emitter.preview_stop").c_str())) ParticleSystem::PreviewStop(c->instanceID);
                    ImGui::EndDisabled();
                    Tooltip(Get("editor.tooltip.component.particle_emitter.preview_stop").c_str());
                    ImGui::PopID();
                }

                BeginSection(Get("component.particle_emitter.emission").c_str());
                flag("playOnStart", "play_on_start", &c->playOnStart);
                flag("loop", "loop", &c->loop);
                number("duration", "duration", &c->duration);
                number("rate", "rate", &c->rate);
                whole("burstMin", "burst_min", &c->burstMin);
                whole("burstMax", "burst_max", &c->burstMax);
                c->burstMax = std::max(c->burstMax, c->burstMin);
                textureList("textures", "textures", c->textures);
                flag("animate", "animate", &c->animate);
                EndSection();

                BeginSection(Get("component.particle_emitter.motion").c_str());
                {
                    PrefabField prefabField("offset");
                    FieldWidth(220.0f);
                    InputOrDrag3(Get("component.particle_emitter.offset").c_str(), &c->offset.x, draggable);
                    Tooltip(Get("editor.tooltip.component.particle_emitter.offset").c_str());
                }
                {
                    PrefabField prefabField("direction");
                    FieldWidth(220.0f);
                    InputOrDrag3(Get("component.particle_emitter.direction").c_str(), &c->direction.x, draggable);
                    Tooltip(Get("editor.tooltip.component.particle_emitter.direction").c_str());
                }
                number("spread", "spread", &c->spread);
                c->spread = std::min(c->spread, 180.0f);
                number("speedMin", "speed_min", &c->speedMin);
                number("speedMax", "speed_max", &c->speedMax);
                c->speedMax = std::max(c->speedMax, c->speedMin);
                number("gravity", "gravity", &c->gravity, -100000.0f);
                number("drag", "drag", &c->drag);
                EndSection();

                BeginSection(Get("component.particle_emitter.look").c_str());
                number("lifetimeMin", "lifetime_min", &c->lifetimeMin);
                number("lifetimeMax", "lifetime_max", &c->lifetimeMax);
                c->lifetimeMax = std::max(c->lifetimeMax, c->lifetimeMin);
                number("sizeMin", "size_min", &c->sizeMin);
                number("sizeMax", "size_max", &c->sizeMax);
                c->sizeMax = std::max(c->sizeMax, c->sizeMin);
                number("sizeEnd", "size_end", &c->sizeEnd);
                colorField("colorStart", "color_start", &c->colorStart);
                colorField("colorEnd", "color_end", &c->colorEnd);
                EndSection();

                BeginSection(Get("component.particle_emitter.surfaces").c_str());
                {
                    PrefabField prefabField("collision");
                    const std::string labels[] = {
                        Get("component.particle_emitter.collision.none"), Get("component.particle_emitter.collision.die"), Get("component.particle_emitter.collision.bounce")
                    };
                    const int value = static_cast<int>(c->collision);
                    FieldWidth(160.0f);
                    if (ImGui::BeginCombo(Get("component.particle_emitter.collision").c_str(), labels[value].c_str())) {
                        for (int i = 0; i < 3; ++i)
                            if (ImGui::Selectable(labels[i].c_str(), value == i)) c->collision = static_cast<ParticleCollision>(i);
                        ImGui::EndCombo();
                    }
                    Tooltip(Get("editor.tooltip.component.particle_emitter.collision").c_str());
                }
                if (c->collision == ParticleCollision::BOUNCE) number("bounciness", "bounciness", &c->bounciness);
                if (c->collision != ParticleCollision::NONE) {
                    textureList("stainTextures", "stain_textures", c->stainTextures);
                    if (!c->stainTextures.empty()) {
                        number("stainChance", "stain_chance", &c->stainChance);
                        c->stainChance = std::min(c->stainChance, 1.0f);
                        number("stainSizeMin", "stain_size_min", &c->stainSizeMin);
                        number("stainSizeMax", "stain_size_max", &c->stainSizeMax);
                        c->stainSizeMax = std::max(c->stainSizeMax, c->stainSizeMin);
                        number("stainLifetime", "stain_lifetime", &c->stainLifetime);
                        colorField("stainColor", "stain_color", &c->stainColor);
                        {
                            PrefabField prefabField("noStainTag");
                            FieldWidth(160.0f);
                            bool useTag = c->noStainTag != NO_TAG;
                            TagId tag = useTag ? c->noStainTag : TagId{0};
                            if (DrawTagCombo(Get("component.particle_emitter.no_stain_tag").c_str(), tag, TagRegistry::TagKind::NORMAL, &useTag))
                                c->noStainTag = useTag ? tag : NO_TAG;
                            Tooltip(Get("editor.tooltip.component.particle_emitter.no_stain_tag").c_str());
                        }
                    }
                }
                EndSection();
            } else { ImGui::TextDisabled("Particle Emitter component missing"); }
        }

        // ════════════════════════════════════════════════════════════════════
        //  Monster AI
        // ════════════════════════════════════════════════════════════════════
        else if (componentType == CMP_MONSTER_AI) {
            auto *c = entity.GetComponentInstance<ComponentMonsterAI>(instanceID);
            if (c) {
                if (entity.GetComponent<ComponentMonsterAI>() != c)
                    ImGui::TextWrapped("%s", Get("component.monster_ai.not_first").c_str());

                const auto number = [&](const char *field, const char *labelKey, const char *tooltipKey, float *value) {
                    PrefabField prefabField(field);
                    FieldWidth(160.0f);
                    InputOrDrag(Get(labelKey).c_str(), value, draggable);
                    Tooltip(Get(tooltipKey).c_str());
                    *value = std::max(*value, 0.0f);
                };
                const auto flag = [&](const char *field, const char *labelKey, const char *tooltipKey, bool *value) {
                    PrefabField prefabField(field);
                    ImGui::Checkbox(Get(labelKey).c_str(), value);
                    Tooltip(Get(tooltipKey).c_str());
                };

                BeginSection(Get("component.monster_ai.body").c_str());
                number("health", "component.monster_ai.health", "editor.tooltip.component.monster_ai.health", &c->health);

                const auto flipbookField = [&](const char *field, const char *labelKey, const char *tooltipKey, std::string &value) {
                    PrefabField prefabField(field);
                    MapEditorInternal::DrawAssetField(Get(labelKey).c_str(), value, AssetKind::Flipbook);
                    Tooltip(Get(tooltipKey).c_str());
                };
                flipbookField("walkFlipbook", "component.monster_ai.walk_flipbook", "editor.tooltip.component.monster_ai.walk_flipbook", c->walkFlipbook);
                flipbookField("attackFlipbook", "component.monster_ai.attack_flipbook", "editor.tooltip.component.monster_ai.attack_flipbook", c->attackFlipbook);
                flipbookField("dieFlipbook", "component.monster_ai.die_flipbook", "editor.tooltip.component.monster_ai.die_flipbook", c->dieFlipbook);

                if ((!c->walkFlipbook.empty() || !c->attackFlipbook.empty() || !c->dieFlipbook.empty()) && !entity.HasComponent<ComponentFlipbook>())
                    ImGui::TextWrapped("%s", Get("component.monster_ai.no_flipbook").c_str());
                EndSection();

                BeginSection(Get("component.monster_ai.senses").c_str());
                {
                    PrefabField prefabField("targetTags");
                    ImGui::TextUnformatted(Get("component.monster_ai.target_tags").c_str());
                    Tooltip(Get("editor.tooltip.component.monster_ai.target_tags").c_str());
                    ImGui::PushID("MonsterTargetTags");
                    DrawTagsEditor(c->targetTags);
                    ImGui::PopID();
                }
                number("sightFov", "component.monster_ai.sight_fov", "editor.tooltip.component.monster_ai.sight_fov", &c->sightFov);
                number("sightRange", "component.monster_ai.sight_range", "editor.tooltip.component.monster_ai.sight_range", &c->sightRange);
                number("nearRange", "component.monster_ai.near_range", "editor.tooltip.component.monster_ai.near_range", &c->nearRange);
                number("lookInterval", "component.monster_ai.look_interval", "editor.tooltip.component.monster_ai.look_interval", &c->lookInterval);
                flag("hearing", "component.monster_ai.hearing", "editor.tooltip.component.monster_ai.hearing", &c->hearing);
                flag("ambush", "component.monster_ai.ambush", "editor.tooltip.component.monster_ai.ambush", &c->ambush);
                flag("startAwake", "component.monster_ai.start_awake", "editor.tooltip.component.monster_ai.start_awake", &c->startAwake);
                EndSection();

                BeginSection(Get("component.monster_ai.attacks").c_str());
                flag("hasMelee", "component.monster_ai.has_melee", "editor.tooltip.component.monster_ai.has_melee", &c->hasMelee);
                if (c->hasMelee) number("meleeRange", "component.monster_ai.melee_range", "editor.tooltip.component.monster_ai.melee_range", &c->meleeRange);
                flag("hasMissile", "component.monster_ai.has_missile", "editor.tooltip.component.monster_ai.has_missile", &c->hasMissile);
                if (c->hasMissile) {
                    number("reactionTime", "component.monster_ai.reaction_time", "editor.tooltip.component.monster_ai.reaction_time", &c->reactionTime);
                    number("missileMinRange", "component.monster_ai.missile_min_range", "editor.tooltip.component.monster_ai.missile_min_range", &c->missileMinRange);
                    number("missileMaxRange", "component.monster_ai.missile_max_range", "editor.tooltip.component.monster_ai.missile_max_range", &c->missileMaxRange);
                    number("missileDistanceOffset", "component.monster_ai.missile_distance_offset", "editor.tooltip.component.monster_ai.missile_distance_offset", &c->missileDistanceOffset);
                    number("missileDistanceScale", "component.monster_ai.missile_distance_scale", "editor.tooltip.component.monster_ai.missile_distance_scale", &c->missileDistanceScale);
                    number("missileDistanceCap", "component.monster_ai.missile_distance_cap", "editor.tooltip.component.monster_ai.missile_distance_cap", &c->missileDistanceCap);
                }
                number("attackPause", "component.monster_ai.attack_pause", "editor.tooltip.component.monster_ai.attack_pause", &c->attackPause);
                number("chaseStep", "component.monster_ai.chase_step", "editor.tooltip.component.monster_ai.chase_step", &c->chaseStep);
                flag("aggressive", "component.monster_ai.aggressive", "editor.tooltip.component.monster_ai.aggressive", &c->aggressive);
                flag("faceTargetOnAttack", "component.monster_ai.face_target", "editor.tooltip.component.monster_ai.face_target", &c->faceTargetOnAttack);
                EndSection();

                BeginSection(Get("component.monster_ai.pain").c_str());
                number("painChance", "component.monster_ai.pain_chance", "editor.tooltip.component.monster_ai.pain_chance", &c->painChance);
                number("painPause", "component.monster_ai.pain_pause", "editor.tooltip.component.monster_ai.pain_pause", &c->painPause);
                flag("infights", "component.monster_ai.infights", "editor.tooltip.component.monster_ai.infights", &c->infights);
                if (c->infights) number("targetLockTime", "component.monster_ai.target_lock_time", "editor.tooltip.component.monster_ai.target_lock_time", &c->targetLockTime);
                EndSection();

                BeginSection(Get("component.monster_ai.other").c_str());
                number("activeChance", "component.monster_ai.active_chance", "editor.tooltip.component.monster_ai.active_chance", &c->activeChance);
                flag("drivePathfinder", "component.monster_ai.drive_pathfinder", "editor.tooltip.component.monster_ai.drive_pathfinder", &c->drivePathfinder);
                EndSection();

                if (c->drivePathfinder && !entity.HasComponent<ComponentPathfinder>())
                    ImGui::TextWrapped("%s", Get("component.monster_ai.no_pathfinder").c_str());
                if (!c->hasMelee && !c->hasMissile)
                    ImGui::TextWrapped("%s", Get("component.monster_ai.no_attacks").c_str());
            } else { ImGui::TextDisabled("Monster AI component missing"); }
        }
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
    bool DrawEventVariablePicker(const char *label, EventVariableID &id, const float width) {
        return DrawEventVariableCombo(label, id, width, true);
    }

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
