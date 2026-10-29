#include "Headers/Editor/PrefabEditor.hpp"

#include <algorithm>

#include "imgui.h"

#include "EditorInternal.hpp"
#include "Headers/Editor/AssetBrowser.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Prefab.hpp"

namespace fs = std::filesystem;

namespace {
    using Localisation::Get;

    constexpr ImVec4 PREFAB_UNSAVED_COLOR = {1.0f, 0.75f, 0.3f, 1.0f};
    constexpr ImVec4 PREFAB_ERROR_COLOR = {1.0f, 0.4f, 0.4f, 1.0f};

    bool CtrlSPressed() {
        return ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);
    }

    nlohmann::json Snapshot(const Level& prefab) {
        return LevelSerialization::EntitiesToJson(prefab, {Prefab::RootOf(prefab).id});
    }

    // A prefab only has its own entity, so a script field can only point at
    // that entity or its components. Anything else (an entity, wall or
    // sector dropped in from the open level) would mean nothing - or the
    // wrong thing - in the level an instance ends up in, so it's cleared.
    void ClearOutsideReferences(Level& prefab) {
        const ID rootID = Prefab::RootOf(prefab).id;

        for (ComponentScript& script : prefab.scripts.components) {
            for (auto& [name, value] : script.publicValues) {
                if (auto* entityRef = std::get_if<EntityRefValue>(&value)) {
                    if (entityRef->entityId != rootID) entityRef->entityId = INVALID_ID;
                }
                else if (auto* componentRef = std::get_if<ComponentRefValue>(&value)) {
                    if (componentRef->entityId != rootID) *componentRef = ComponentRefValue{INVALID_ID, componentRef->componentType};
                }
                else if (auto* behaviourRef = std::get_if<BehaviourRefValue>(&value)) {
                    if (behaviourRef->entityId != rootID) *behaviourRef = BehaviourRefValue{};
                }
                else if (auto* wallRef = std::get_if<WallRefValue>(&value)) *wallRef = WallRefValue{};
                else if (auto* sectorRef = std::get_if<SectorRefValue>(&value)) *sectorRef = SectorRefValue{};
            }
        }
    }
}

void PrefabEditor::Open(const fs::path& absolutePath) {
    const fs::path path = absolutePath.lexically_normal();

    for (const std::unique_ptr<Document>& document : documents) {
        if (document->path != path) continue;
        document->focusRequested = true;
        return;
    }

    auto document = std::make_unique<Document>();
    document->id = nextDocumentId++;
    document->path = path;

    if (LevelSerialization::LoadPrefabFromFile(path, document->prefab, &document->loadError))
        document->saved = Snapshot(document->prefab);

    documents.push_back(std::move(document));
}

void PrefabEditor::Draw() {
    for (const std::unique_ptr<Document>& document : documents) DrawDocument(*document);

    std::erase_if(documents, [](const std::unique_ptr<Document>& document) { return !document->open; });
}

void PrefabEditor::OnFileMoved(const fs::path& oldAbsolutePath, const fs::path& newAbsolutePath) {
    const fs::path oldPath = oldAbsolutePath.lexically_normal();

    for (const std::unique_ptr<Document>& document : documents)
        if (document->path == oldPath) document->path = newAbsolutePath.lexically_normal();
}

bool PrefabEditor::IsDirty(const Document& document) {
    return document.loadError.empty() && Snapshot(document.prefab) != document.saved;
}

std::string PrefabEditor::ReferenceOf(const Document& document) {
    return AssetBrowser::ToAssetReference(document.path, AssetKind::Prefab);
}

void PrefabEditor::DrawDocument(Document& document) {
    const std::string idSuffix = "##Prefab" + std::to_string(document.id);
    const std::string title = Get("prefab_editor.title") + ": " + document.path.filename().string() +
                              (IsDirty(document) ? "*" : "") + "###PrefabEditor" + std::to_string(document.id);

    if (document.focusRequested) {
        ImGui::SetNextWindowFocus();
        document.focusRequested = false;
    }

    bool windowOpen = true;

    if (!document.loadError.empty()) {
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &windowOpen)) {
            ImGui::TextColored(PREFAB_ERROR_COLOR, "%s", Get("prefab_editor.load_failed").c_str());
            ImGui::TextWrapped("%s", document.loadError.c_str());
            if (ImGui::Button(Get("common.close").c_str())) windowOpen = false;
        }
        ImGui::End();

        if (!windowOpen) document.open = false;
        return;
    }

    {
        // The inspector works on "the current level": make that the prefab
        // while it draws, so every component editor edits the prefab's entity.
        LevelManager::ScopedCurrentLevel prefabScope(document.prefab);
        Entity& root = document.prefab.entities.front();

        const auto drawExtras = [this, &document] { DrawExtras(document); };

        if (document.prefab.ui_transforms.Has(root.id)) {
            MapEditorInternal::DrawUIEntityInspectorFor(root, title, idSuffix, &windowOpen, drawExtras);
        }
        else {
            ImGuiDrawFunctions::EntityInspectorState& inspector = document.inspector;
            inspector.windowTitle = title;
            inspector.allowDelete = false;
            inspector.allowSnapToFloor = false;
            inspector.allowSnapToWall = false;
            inspector.drawExtras = drawExtras;

            constexpr bool draggable = true;
            ImGuiDrawFunctions::DrawEntityEditor(root, inspector, &windowOpen, draggable);
        }
    }

    if (document.saveRequested) {
        document.saveRequested = false;
        Save(document);
    }

    if (!windowOpen) {
        if (IsDirty(document)) document.closePromptRequested = true;
        else document.open = false;
    }

    DrawClosePrompt(document);
}

void PrefabEditor::DrawExtras(Document& document) {
    // Saved after the inspector is drawn: right now "the current level" is
    // the prefab itself, and saving updates the real level's instances.
    if (ImGui::Button(Get("common.save").c_str())) document.saveRequested = true;

    ImGui::SameLine();
    ImGui::TextDisabled("%s", ReferenceOf(document).c_str());

    if (IsDirty(document)) {
        ImGui::SameLine();
        ImGui::TextColored(PREFAB_UNSAVED_COLOR, "%s", Get("prefab_editor.unsaved").c_str());
    }

    if (!document.saveError.empty()) ImGui::TextColored(PREFAB_ERROR_COLOR, "%s", document.saveError.c_str());

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && CtrlSPressed()) document.saveRequested = true;

    ImGui::Separator();
}

void PrefabEditor::DrawClosePrompt(Document& document) {
    const std::string popupId = Get("prefab_editor.close_title") + "##PrefabClose" + std::to_string(document.id);

    if (document.closePromptRequested) {
        ImGui::OpenPopup(popupId.c_str());
        document.closePromptRequested = false;
    }

    if (!ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted(Get("prefab_editor.close_prompt").c_str());
    ImGui::TextDisabled("%s", ReferenceOf(document).c_str());
    ImGui::Spacing();

    if (ImGui::Button(Get("common.save").c_str())) {
        // A failed save keeps the window open so the error can be read.
        if (Save(document)) document.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button(Get("prefab_editor.discard").c_str())) {
        document.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button(Get("common.cancel").c_str())) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool PrefabEditor::Save(Document& document) {
    ClearOutsideReferences(document.prefab);

    if (std::string error; !LevelSerialization::SavePrefabToFile(document.path, document.prefab, &error)) {
        document.saveError = error;
        return false;
    }

    document.saveError.clear();
    document.saved = Snapshot(document.prefab);

    // Instances in the open level follow the new version straight away;
    // other levels pick it up when they're next loaded.
    if (LevelManager::HasCurrentLevel())
        Prefab::Replace(ReferenceOf(document), document.prefab, LevelManager::CurrentLevel());
    else Prefab::Invalidate(ReferenceOf(document));

    return true;
}
