//
// Created by berke on 10/9/2026.
//

#include "Headers/Editor/FlipbookEditor.hpp"

#include <algorithm>
#include <array>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "EditorInternal.hpp"
#include "Headers/Editor/AssetBrowser.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Engine/Local/Local.hpp"

namespace fs = std::filesystem;

namespace {
    using Localisation::Get;

    // Drag-and-drop payload for reordering frames. Carries the document id so
    // a frame can't be dropped into another flipbook's list.
    constexpr const char* FLIPBOOK_FRAME_PAYLOAD = "TILKY_FLIPBOOK_FRAME";

    struct FlipbookFramePayload {
        int documentId;
        int index;
    };

    const ImVec4 FLIPBOOK_ERROR_COLOR = {0.95f, 0.35f, 0.30f, 1.0f};
    const ImVec4 FLIPBOOK_UNSAVED_COLOR = {0.95f, 0.80f, 0.35f, 1.0f};

    // In ComponentSprite::textureFileNames order.
    constexpr std::array<const char*, 8> FLIPBOOK_SIDE_NAMES = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

    // Same safety cap as the game (FlipbookSystem) for a stalled frame.
    constexpr int MAX_PREVIEW_STEPS_PER_FRAME = 256;

    bool CtrlSPressed() {
        return ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);
    }
}

void FlipbookEditor::Open(const fs::path& absolutePath) {
    const fs::path path = absolutePath.lexically_normal();

    for (const std::unique_ptr<Document>& document : documents) {
        if (document->path != path) continue;

        document->focusRequested = true;
        return;
    }

    auto document = std::make_unique<Document>();
    document->id = nextDocumentId++;
    document->path = path;

    FlipbookIO::Load(path, document->asset, &document->loadError);

    documents.push_back(std::move(document));
}

void FlipbookEditor::Draw() {
    for (const std::unique_ptr<Document>& document : documents) DrawDocumentWindow(*document);

    std::erase_if(documents, [](const std::unique_ptr<Document>& document) { return !document->open; });
}

void FlipbookEditor::OnFileMoved(const fs::path& oldAbsolutePath, const fs::path& newAbsolutePath) {
    const fs::path oldPath = oldAbsolutePath.lexically_normal();

    for (const std::unique_ptr<Document>& document : documents)
        if (document->path == oldPath) document->path = newAbsolutePath.lexically_normal();
}

void FlipbookEditor::RenameTextureReference(const std::string& oldReference, const std::string& newReference) {
    for (const std::unique_ptr<Document>& document : documents)
        for (FlipbookFrame& frame : document->asset.frames)
            for (std::string& texture : frame.textures)
                if (texture == oldReference) texture = newReference;
}

std::string FlipbookEditor::ReferenceOf(const Document& document) {
    return AssetBrowser::ToAssetReference(document.path, AssetKind::Flipbook);
}

void FlipbookEditor::DrawDocumentWindow(Document& document) {
    ImGui::SetNextWindowSize(ImVec2(440.0f, 600.0f), ImGuiCond_FirstUseEver);

    if (document.focusRequested) {
        ImGui::SetNextWindowFocus();
        document.focusRequested = false;
    }

    // The part after ### is the window's identity, so the title can change
    // (unsaved marker, rename) without ImGui treating it as a new window.
    const std::string title = Get("flipbook_editor.title") + ": " + document.path.filename().string() +
                              (document.dirty ? "*" : "") + "###FlipbookEditor" + std::to_string(document.id);

    bool windowOpen = true;

    if (ImGui::Begin(title.c_str(), &windowOpen)) {
        if (!document.loadError.empty()) {
            ImGui::TextColored(FLIPBOOK_ERROR_COLOR, "%s", Get("flipbook_editor.load_failed").c_str());
            ImGui::TextWrapped("%s", document.loadError.c_str());

            if (ImGui::Button(Get("common.close").c_str())) document.open = false;
        }
        else {
            if (ImGui::Button(Get("common.save").c_str())) Save(document);

            ImGui::SameLine();
            ImGui::TextDisabled("%s", ReferenceOf(document).c_str());

            if (document.dirty) {
                ImGui::SameLine();
                ImGui::TextColored(FLIPBOOK_UNSAVED_COLOR, "%s", Get("flipbook_editor.unsaved").c_str());
            }

            if (!document.saveError.empty()) ImGui::TextColored(FLIPBOOK_ERROR_COLOR, "%s", document.saveError.c_str());

            ImGui::Separator();

            DrawSettings(document);
            DrawPreview(document);
            DrawFrameList(document);

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && CtrlSPressed()) Save(document);
        }
    }

    ImGui::End();

    if (!windowOpen) {
        if (document.dirty && document.loadError.empty()) document.closePromptRequested = true;
        else document.open = false;
    }

    if (document.open && document.loadError.empty()) DrawFrameEditorWindow(document);

    DrawClosePrompt(document);
}

void FlipbookEditor::DrawSettings(Document& document) {
    if (!ImGui::CollapsingHeader(Get("flipbook_editor.settings").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;

    FlipbookAsset& asset = document.asset;

    float fps = asset.fps;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputFloat(Get("flipbook_editor.fps").c_str(), &fps, 1.0f, 5.0f, "%.2f")) {
        asset.fps = std::max(fps, 0.1f);
        document.dirty = true;
    }
    ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.fps").c_str());

    const std::string onceText = Get("flipbook_editor.loop_mode.once");
    const std::string loopText = Get("flipbook_editor.loop_mode.loop");
    const std::string pingPongText = Get("flipbook_editor.loop_mode.ping_pong");
    const char* loopModes[] = {onceText.c_str(), loopText.c_str(), pingPongText.c_str()};

    int loopMode = static_cast<int>(asset.loopMode);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo(Get("flipbook_editor.loop_mode").c_str(), &loopMode, loopModes, 3)) {
        asset.loopMode = static_cast<FlipbookLoopMode>(loopMode);
        document.dirty = true;
    }
    ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.loop_mode").c_str());

    if (ImGui::Checkbox(Get("flipbook_editor.play_on_start").c_str(), &asset.playOnStart)) document.dirty = true;
    ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.play_on_start").c_str());

    ImGui::Spacing();
}

void FlipbookEditor::DrawPreview(Document& document) {
    if (!ImGui::CollapsingHeader(Get("flipbook_editor.preview").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;

    const FlipbookAsset& asset = document.asset;
    const int frameCount = static_cast<int>(asset.frames.size());

    if (frameCount == 0) {
        document.previewPlaying = false;
        ImGui::TextDisabled("%s", Get("flipbook_editor.no_frames").c_str());
        ImGui::Spacing();
        return;
    }

    document.previewFrame = std::clamp(document.previewFrame, 0, frameCount - 1);

    // Advanced here rather than in a separate update, since this only has
    // to run while the window is visible.
    if (document.previewPlaying) {
        document.previewTime += ImGui::GetIO().DeltaTime;

        for (int steps = 0; document.previewTime >= asset.FrameDuration(document.previewFrame); ++steps) {
            if (steps >= MAX_PREVIEW_STEPS_PER_FRAME) {
                document.previewTime = 0.0f;
                break;
            }

            document.previewTime -= asset.FrameDuration(document.previewFrame);

            if (!asset.StepFrame(document.previewFrame, document.previewDirection)) {
                document.previewPlaying = false;
                document.previewTime = 0.0f;
                break;
            }
        }
    }

    const FlipbookFrame& frame = asset.frames[document.previewFrame];

    MapEditorInternal::DrawTextureThumbnailBox(frame.textures[document.previewSlot], 128.0f);

    ImGui::SameLine();
    ImGui::BeginGroup();

    if (document.previewPlaying) {
        if (ImGui::Button(Get("flipbook_editor.stop").c_str())) {
            document.previewPlaying = false;
            document.previewFrame = 0;
            document.previewDirection = 1;
            document.previewTime = 0.0f;
        }
    }
    else if (ImGui::Button(Get("flipbook_editor.play").c_str())) {
        // A finished Once animation plays again from the start, same as in game.
        if (asset.loopMode == FlipbookLoopMode::Once && document.previewFrame >= frameCount - 1) {
            document.previewFrame = 0;
            document.previewDirection = 1;
        }

        document.previewTime = 0.0f;
        document.previewPlaying = true;
    }

    ImGui::Text("%s %d / %d", Get("flipbook_editor.frame").c_str(), document.previewFrame + 1, frameCount);
    ImGui::TextUnformatted(frame.name.c_str());

    ImGui::SetNextItemWidth(80.0f);
    ImGui::Combo(Get("flipbook_editor.preview_side").c_str(), &document.previewSlot, FLIPBOOK_SIDE_NAMES.data(),
                 static_cast<int>(FLIPBOOK_SIDE_NAMES.size()));
    ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.preview_side").c_str());

    ImGui::EndGroup();
    ImGui::Spacing();
}

void FlipbookEditor::DrawFrameList(Document& document) {
    if (!ImGui::CollapsingHeader(Get("flipbook_editor.frames").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;

    FrameAction action;
    std::vector<FlipbookFrame>& frames = document.asset.frames;

    if (ImGui::Button(Get("flipbook_editor.add_frame").c_str())) action = {FrameAction::Kind::Add};

    if (frames.empty()) ImGui::TextDisabled("%s", Get("flipbook_editor.no_frames").c_str());

    for (int i = 0; i < static_cast<int>(frames.size()); ++i) {
        FlipbookFrame& frame = frames[i];

        ImGui::PushID(i);
        ImGui::BeginGroup();

        if (document.renamingFrame == frame.name) {
            if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();

            ImGui::SetNextItemWidth(160.0f);
            const bool entered = ImGui::InputText("##rename", &document.renameBuffer,
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            // Enter or clicking away keeps the name; Escape cancels.
            if (entered || ImGui::IsItemDeactivatedAfterEdit()) CommitRename(document);
            else if (ImGui::IsItemDeactivated()) {
                document.renamingFrame.clear();
                document.renameError.clear();
            }

            if (!document.renameError.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(FLIPBOOK_ERROR_COLOR, "%s", document.renameError.c_str());
            }
        }
        else {
            const bool selected = document.editingFrame == frame.name;
            ImGui::Selectable(frame.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(160.0f, 0.0f));

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                document.renamingFrame = frame.name;
                document.renameBuffer = frame.name;
                document.renameError.clear();
            }

            if (ImGui::BeginDragDropSource()) {
                const FlipbookFramePayload payload{document.id, i};
                ImGui::SetDragDropPayload(FLIPBOOK_FRAME_PAYLOAD, &payload, sizeof(payload));
                ImGui::TextUnformatted(frame.name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.frame_row").c_str());
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(Get("common.edit").c_str())) document.editingFrame = frame.name;

        if (!frame.eventFunction.empty()) {
            ImGui::SameLine();

            if (FlipbookIO::IsValidEventFunction(frame.eventFunction))
                ImGui::TextDisabled("%s()", frame.eventFunction.c_str());
            else
                ImGui::TextColored(FLIPBOOK_ERROR_COLOR, "%s()", frame.eventFunction.c_str());
        }

        ImGui::EndGroup();

        // The whole row is the drop spot and the right-click target.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(FLIPBOOK_FRAME_PAYLOAD)) {
                const auto* dropped = static_cast<const FlipbookFramePayload*>(payload->Data);
                if (dropped->documentId == document.id) action = {FrameAction::Kind::Move, dropped->index, i};
            }

            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##frame_menu")) {
            if (ImGui::MenuItem(Get("flipbook_editor.rename").c_str())) {
                document.renamingFrame = frame.name;
                document.renameBuffer = frame.name;
                document.renameError.clear();
            }

            if (ImGui::MenuItem(Get("flipbook_editor.duplicate").c_str())) action = {FrameAction::Kind::Duplicate, i};
            if (ImGui::MenuItem(Get("flipbook_editor.insert_before").c_str())) action = {FrameAction::Kind::InsertBefore, i};
            if (ImGui::MenuItem(Get("flipbook_editor.insert_after").c_str())) action = {FrameAction::Kind::InsertAfter, i};

            ImGui::Separator();

            if (ImGui::MenuItem(Get("common.delete").c_str())) action = {FrameAction::Kind::Delete, i};

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ApplyFrameAction(document, action);
}

void FlipbookEditor::ApplyFrameAction(Document& document, const FrameAction& action) {
    std::vector<FlipbookFrame>& frames = document.asset.frames;
    const int frameCount = static_cast<int>(frames.size());

    const auto newFrame = [&] {
        FlipbookFrame frame;
        frame.name = document.asset.MakeUniqueFrameName();
        return frame;
    };

    switch (action.kind) {
        case FrameAction::Kind::None:
            return;

        case FrameAction::Kind::Add:
            frames.push_back(newFrame());
            break;

        case FrameAction::Kind::Duplicate: {
            if (action.index < 0 || action.index >= frameCount) return;

            FlipbookFrame copy = frames[action.index];
            copy.name = document.asset.MakeUniqueFrameName();
            frames.insert(frames.begin() + action.index + 1, std::move(copy));
            break;
        }

        case FrameAction::Kind::InsertBefore:
            if (action.index < 0 || action.index >= frameCount) return;
            frames.insert(frames.begin() + action.index, newFrame());
            break;

        case FrameAction::Kind::InsertAfter:
            if (action.index < 0 || action.index >= frameCount) return;
            frames.insert(frames.begin() + action.index + 1, newFrame());
            break;

        case FrameAction::Kind::Delete:
            if (action.index < 0 || action.index >= frameCount) return;
            if (document.editingFrame == frames[action.index].name) document.editingFrame.clear();
            if (document.renamingFrame == frames[action.index].name) document.renamingFrame.clear();
            frames.erase(frames.begin() + action.index);
            break;

        case FrameAction::Kind::Move: {
            if (action.index < 0 || action.index >= frameCount) return;
            if (action.target < 0 || action.target >= frameCount || action.target == action.index) return;

            FlipbookFrame moved = std::move(frames[action.index]);
            frames.erase(frames.begin() + action.index);
            frames.insert(frames.begin() + action.target, std::move(moved));
            break;
        }
    }

    document.dirty = true;
}

void FlipbookEditor::CommitRename(Document& document) {
    const int index = document.asset.FindFrame(document.renamingFrame);

    if (index == -1) {
        document.renamingFrame.clear();
        return;
    }

    const std::string& newName = document.renameBuffer;

    if (newName.empty()) {
        document.renameError = Get("flipbook_editor.name_empty");
        return;
    }

    if (newName != document.renamingFrame && document.asset.FindFrame(newName) != -1) {
        document.renameError = Get("flipbook_editor.name_taken");
        return;
    }

    if (newName != document.renamingFrame) {
        if (document.editingFrame == document.renamingFrame) document.editingFrame = newName;

        document.asset.frames[index].name = newName;
        document.dirty = true;
    }

    document.renamingFrame.clear();
    document.renameError.clear();
}

void FlipbookEditor::DrawFrameEditorWindow(Document& document) {
    if (document.editingFrame.empty()) return;

    const int index = document.asset.FindFrame(document.editingFrame);

    if (index == -1) {
        document.editingFrame.clear();
        return;
    }

    FlipbookFrame& frame = document.asset.frames[index];

    ImGui::SetNextWindowSize(ImVec2(320.0f, 600.0f), ImGuiCond_FirstUseEver);

    const std::string title = Get("flipbook_editor.frame") + ": " + frame.name + " (" + document.path.filename().string() +
                              ")###FlipbookFrame" + std::to_string(document.id);

    bool windowOpen = true;

    if (ImGui::Begin(title.c_str(), &windowOpen)) {
        float duration = frame.duration;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputFloat(Get("flipbook_editor.duration").c_str(), &duration, 0.01f, 0.1f, "%.3f")) {
            frame.duration = std::max(duration, 0.0f);
            document.dirty = true;
        }
        ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.duration").c_str());

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText(Get("flipbook_editor.event").c_str(), &frame.eventFunction)) document.dirty = true;
        ImGuiDrawFunctions::Tooltip(Get("editor.tooltip.flipbook_editor.event").c_str());

        if (std::string reason; !FlipbookIO::IsValidEventFunction(frame.eventFunction, &reason))
            ImGui::TextColored(FLIPBOOK_ERROR_COLOR, "%s", reason.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::SmallButton(Get("flipbook_editor.clear_textures").c_str())) {
            frame.textures.fill("");
            document.dirty = true;
        }

        ImGui::Spacing();

        // Always all 8 slots: a flipbook takes its side count from whichever
        // sprite plays it, so it can't know which ones will be used.
        const std::array<std::string, 8> texturesBefore = frame.textures;
        ImGuiDrawFunctions::DrawDirectionalTextureSlots(frame.textures, SIDECOUNT_45);
        if (frame.textures != texturesBefore) document.dirty = true;

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && CtrlSPressed()) Save(document);
    }

    ImGui::End();

    if (!windowOpen) document.editingFrame.clear();
}

void FlipbookEditor::DrawClosePrompt(Document& document) {
    const std::string popupId = Get("flipbook_editor.close_title") + "##FlipbookClose" + std::to_string(document.id);

    if (document.closePromptRequested) {
        ImGui::OpenPopup(popupId.c_str());
        document.closePromptRequested = false;
    }

    if (!ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted(Get("flipbook_editor.close_prompt").c_str());
    ImGui::TextDisabled("%s", ReferenceOf(document).c_str());
    ImGui::Spacing();

    if (ImGui::Button(Get("common.save").c_str())) {
        // A failed save keeps the window open so the error can be read.
        if (Save(document)) document.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button(Get("flipbook_editor.discard").c_str())) {
        document.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button(Get("common.cancel").c_str())) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool FlipbookEditor::Save(Document& document) {
    for (const FlipbookFrame& frame : document.asset.frames) {
        if (FlipbookIO::IsValidEventFunction(frame.eventFunction)) continue;

        document.saveError = Get("flipbook_editor.invalid_events");
        return false;
    }

    if (std::string error; !FlipbookIO::Save(document.path, document.asset, &error)) {
        document.saveError = error;
        return false;
    }

    document.saveError.clear();
    document.dirty = false;

    // Anything already using this file (the Flipbook inspector) reads the new version.
    FlipbookLibrary::Invalidate(ReferenceOf(document));

    return true;
}
