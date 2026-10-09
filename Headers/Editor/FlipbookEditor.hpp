//
// Created by berke on 10/9/2026.
//

#ifndef TILKY_ENGINE_FLIPBOOKEDITOR_HPP
#define TILKY_ENGINE_FLIPBOOKEDITOR_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Headers/Objects/FlipbookAsset.hpp"

// The .fpk editor, opened by double-clicking a flipbook in the Asset Browser.
// Each open file gets its own window: playback settings, a Play/Stop
// preview, and the frame list (add, rename, reorder by dragging, duplicate,
// delete). A frame's Edit button opens its 8 texture slots and event
// function in a second window. Owned by AssetBrowser, so every editor that
// hosts the browser can show it.
class FlipbookEditor {
public:
    // Opens `absolutePath`, or focuses its window if it's already open.
    void Open(const std::filesystem::path& absolutePath);

    // Draws every open flipbook's windows. Call once per frame, outside any
    // other ImGui window.
    void Draw();

    // The Asset Browser moved or renamed a .fpk: keep editing it at its new path.
    void OnFileMoved(const std::filesystem::path& oldAbsolutePath, const std::filesystem::path& newAbsolutePath);

    // The Asset Browser moved or renamed a texture: update open flipbooks
    // too, including unsaved ones.
    void RenameTextureReference(const std::string& oldReference, const std::string& newReference);

private:
    struct Document {
        int id = 0; // keeps ImGui window IDs apart
        std::filesystem::path path;
        FlipbookAsset asset;

        std::string loadError; // non-empty: the file couldn't be read, nothing to edit
        std::string saveError;

        bool dirty = false;
        bool open = true;
        bool focusRequested = true;
        bool closePromptRequested = false;

        // Frames are tracked by name (unique within the flipbook), so
        // reordering or deleting other frames doesn't change which one these
        // point at.
        std::string editingFrame;  // frame shown in the Edit window, empty = none
        std::string renamingFrame; // frame whose name is being typed in the list
        std::string renameBuffer;
        std::string renameError;

        bool previewPlaying = false;
        int previewFrame = 0;
        int previewDirection = 1;
        float previewTime = 0.0f;
        int previewSlot = 0; // which of the 8 textures the preview shows
    };

    // A frame operation picked while drawing the list, applied once the list
    // is finished so the frames vector never changes under the loop.
    struct FrameAction {
        enum class Kind { None, Add, Duplicate, InsertBefore, InsertAfter, Delete, Move };
        Kind kind = Kind::None;
        int index = -1;
        int target = -1; // Move only: where the frame ends up
    };

    void DrawDocumentWindow(Document& document);
    void DrawSettings(Document& document);
    void DrawPreview(Document& document);
    void DrawFrameList(Document& document);
    void DrawFrameEditorWindow(Document& document);
    void DrawClosePrompt(Document& document);

    void ApplyFrameAction(Document& document, const FrameAction& action);
    void CommitRename(Document& document);
    bool Save(Document& document);

    [[nodiscard]] static std::string ReferenceOf(const Document& document);

    std::vector<std::unique_ptr<Document>> documents;
    int nextDocumentId = 1;
};

#endif //TILKY_ENGINE_FLIPBOOKEDITOR_HPP
