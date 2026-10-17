#ifndef TILKY_ENGINE_PREFABEDITOR_HPP
#define TILKY_ENGINE_PREFABEDITOR_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Objects/Level.hpp"

// The .prefab editor, opened by double-clicking a prefab in the Asset
// Browser. Each open prefab gets the normal entity inspector (the UI one for
// a UI prefab) in its own window, editing the prefab's entity. Saving
// (Ctrl+S or the Save button) writes the file and updates every instance in
// the open level. Owned by AssetBrowser, like FlipbookEditor.
class PrefabEditor {
public:
    // Opens `absolutePath`, or focuses its window if it's already open.
    void Open(const std::filesystem::path& absolutePath);

    // Draws every open prefab's windows. Call once per frame, outside any
    // other ImGui window.
    void Draw();

    // The Asset Browser moved or renamed a .prefab: keep editing it at its new path.
    void OnFileMoved(const std::filesystem::path& oldAbsolutePath, const std::filesystem::path& newAbsolutePath);

private:
    struct Document {
        int id = 0; // keeps ImGui window IDs apart
        std::filesystem::path path;
        Level prefab;
        nlohmann::json saved; // the prefab as last loaded/saved, for the unsaved marker

        std::string loadError; // non-empty: the file couldn't be read, nothing to edit
        std::string saveError;

        bool open = true;
        bool focusRequested = true;
        bool closePromptRequested = false;
        bool saveRequested = false;

        ImGuiDrawFunctions::EntityInspectorState inspector;
    };

    void DrawDocument(Document& document);
    void DrawExtras(Document& document);
    void DrawClosePrompt(Document& document);
    bool Save(Document& document);

    [[nodiscard]] static bool IsDirty(const Document& document);
    [[nodiscard]] static std::string ReferenceOf(const Document& document);

    std::vector<std::unique_ptr<Document>> documents;
    int nextDocumentId = 1;
};

#endif //TILKY_ENGINE_PREFABEDITOR_HPP
