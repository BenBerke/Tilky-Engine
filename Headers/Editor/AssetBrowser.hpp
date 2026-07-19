#pragma once

#include <filesystem>
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

// What an asset is FOR, driving how it's referenced, thumbnailed, and
// which drag-and-drop payload type it uses.
//  - Texture: referenced by filename WITH extension (relative to the
//    project's Textures folder), e.g. "brick01.png" or "ui/icon.png".
//  - Sound / Script: referenced by name WITHOUT extension (relative to
//    the Sounds / Scripts folder), matching how the engine already names
//    scripts - e.g. "click" or "ui/click", extension implied.
enum class AssetKind {
    Folder,
    Texture,
    Sound,
    Script,
    Other // shown for transparency, but not draggable/thumbnailed
};

// A single folder/file entry inside the asset browser's CURRENT directory.
// Deliberately holds only plain path/string data - never a live
// std::filesystem::directory_entry or other filesystem handle - so an
// entry can never dangle if the underlying file disappears between a scan
// and the next frame.
struct AssetBrowserEntry {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath; // relative to the browser's root directory
    std::string displayName;
    bool isDirectory = false;
    AssetKind kind = AssetKind::Other;
};

// Self-contained, reusable ImGui asset browser locked to a single root
// directory (intended to be the current project's Assets folder).
//
// Filesystem scanning is kept separate from drawing: ScanCurrentDirectory()
// only runs when the browser navigates or Refresh() is called explicitly -
// never on every frame - and it only ever lists the CURRENT directory's
// direct children, never the whole asset tree.
//
// There is no permanent index of any kind here (see EditorTextureCache for
// the equally index-free texture preview cache). Assets are referenced by
// name, resolved on demand.
class AssetBrowser {
public:
    using ThumbnailProvider = std::function<ImTextureID(const std::string&)>;

    AssetBrowser() = default;
    ~AssetBrowser() = default;

    AssetBrowser(const AssetBrowser&) = delete;
    AssetBrowser& operator=(const AssetBrowser&) = delete;

    // Locks the browser to `root` (creating it, and the standard Textures/
    // Sounds/Scripts subfolders, if missing) and navigates back to it.
    void SetRootDirectory(const std::filesystem::path& root);

    // Re-scans the CURRENT directory only. Safe to call any time - e.g.
    // from a user-facing "Refresh" button after files changed on disk.
    void Refresh();

    // Draws breadcrumbs, a search field, and the folder/asset grid. Call
    // this between the host window's ImGui::Begin()/End().
    // `renderer` is only used to lazily load texture thumbnails via
    // EditorTextureCache.
    void Draw(const ThumbnailProvider& thumbnailProvider);

    [[nodiscard]] bool HasSelection() const;
    [[nodiscard]] const std::filesystem::path& GetSelectedFile() const;

    // Type-aware consumption of the last double-clicked asset: returns
    // true (and clears the pending selection) only if it matches
    // `expectedKind`. A click on a field of the wrong kind harmlessly
    // no-ops rather than discarding the pending pick, so a matching field
    // clicked afterwards can still apply it.
    bool ConsumePendingConfirmedSelection(AssetKind expectedKind, std::filesystem::path& outAbsolutePath);

    // Imports a file from OUTSIDE the project (e.g. dropped from the OS
    // file manager) into the folder currently being browsed. Handles name
    // collisions by appending " (2)", " (3)", etc. Never throws; returns
    // false (and logs why) on any failure.
    bool ImportExternalFile(const std::filesystem::path& sourceAbsolutePath);

    // Hit-tests a SCREEN-space point (e.g. from an SDL_EVENT_DROP_FILE,
    // whose x/y are window-relative - equivalent to ImGui screen space
    // here since this editor never enables multi-viewport ImGui) against
    // this panel's on-screen rectangle as of the last Draw() call.
    [[nodiscard]] bool IsScreenPointInside(float screenX, float screenY) const;

    // Drag-and-drop payload type for a given asset kind, so field widgets
    // can register a matching ImGui::AcceptDragDropPayload target. The
    // payload data is the absolute source path as a null-terminated UTF-8
    // string.
    static const char* DragDropPayloadTypeFor(AssetKind kind);

    // Converts an absolute path into the reference string a field of the
    // given kind should store (relative-with-extension for textures;
    // relative-without-extension for sounds/scripts).
    static std::string ToAssetReference(const std::filesystem::path& absolutePath, AssetKind kind);

private:
    void NavigateTo(const std::filesystem::path& absoluteDirectory);
    void NavigateToParent();
    [[nodiscard]] bool IsPathWithinRoot(const std::filesystem::path& absolutePath) const;

    void ScanCurrentDirectory();

    void DrawBreadcrumbs();
    void DrawSearchBar();
    void DrawEntries(const ThumbnailProvider& thumbnailProvider);
    void DrawFileTile(const AssetBrowserEntry& entry, float tileSize, const ThumbnailProvider& thumbnailProvider);
    void DrawFolderTile(const AssetBrowserEntry& entry, float tileSize);

    std::filesystem::path rootDirectory;
    std::filesystem::path currentDirectory;

    std::vector<AssetBrowserEntry> entries;
    bool scanFailed = false;

    char searchBuffer[128] = "";

    std::filesystem::path selectedFile;

    std::optional<std::filesystem::path> pendingConfirmedPath;
    AssetKind pendingConfirmedKind = AssetKind::Other;

    // Last known on-screen rectangle of the host ImGui window, captured at
    // the top of Draw(). Used to scope OS-level file drops to this panel
    // instead of importing a drop that landed somewhere else entirely.
    float lastWindowScreenMinX = 0.0f;
    float lastWindowScreenMinY = 0.0f;
    float lastWindowScreenMaxX = 0.0f;
    float lastWindowScreenMaxY = 0.0f;
};