#pragma once

#include <filesystem>
#include <functional>
#include <imgui.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <TextEditor.h>

// What an asset is FOR, driving how it's referenced, thumbnailed, and
// which drag-and-drop payload type it uses.
//  - Texture: referenced by filename WITH extension (relative to the
//    project's Textures folder), e.g. "brick01.png" or "ui/icon.png".
//  - Sound / Script: referenced by name WITHOUT extension (relative to
//    the Sounds / Scripts folder), matching how the engine already names
//    scripts - e.g. "click" or "ui/click", extension implied.
//
// This is a different axis to AssetEntryType below: AssetKind is about
// what a field widget should do with the asset (drag-drop payload type,
// reference-string format); AssetEntryType is about which AssetEntry
// subclass - and therefore which browser behavior - an entry gets. A
// level file has its own AssetEntryType but is not a field-reference kind
// at all, so it simply has no AssetKind of its own (defaults to Other).

enum class AssetKind {
    Folder,
    Texture,
    Sound,
    Script,
    Other // shown for transparency, but not draggable/thumbnailed
};

class AssetBrowser; // forward declaration; AssetEntry only ever needs a reference to it.

// Which AssetEntry subclass an entry is. Used for sort order (directories
// first) and tile styling without unsafe downcasts - see
// AssetEntry::GetType(). Extend this alongside CreateAssetEntry() and the
// extension registry in AssetBrowser.cpp when a new extension-driven asset
// type needs its own dedicated behavior.
enum class AssetEntryType {
    Directory,
    GenericFile,
    Texture,
    Level
};

// Base of the polymorphic asset-entry hierarchy. One instance represents
// one direct child (file or folder) of the directory currently being
// browsed. Like the old AssetBrowserEntry it replaces, an entry
// deliberately holds only plain path/string data - never a live
// std::filesystem::directory_entry or other filesystem handle - so it can
// never dangle if the underlying file disappears between a scan and the
// next frame. Entries are owned exclusively via std::unique_ptr<AssetEntry>
// so a refresh can safely replace them without anything else retaining a
// raw pointer into the old set.
//
// Visual tile drawing (layout, thumbnails, tinting) deliberately stays in
// AssetBrowser rather than becoming virtual here, so the GPU-facing
// thumbnail cache (EditorTextureCache, reached only through the opaque
// ThumbnailProvider callback) is never duplicated per entry type; an entry
// only ever exposes the string a thumbnail should be looked up under, via
// GetThumbnailReference().
class AssetEntry {
public:
    virtual ~AssetEntry() = default;

    AssetEntry(const AssetEntry&) = delete;
    AssetEntry& operator=(const AssetEntry&) = delete;

    // Invoked when this entry is double-clicked (or activated with Enter).
    // Default is a no-op. DirectoryEntry navigates into itself,
    // GenericFileEntry offers itself to a matching field widget, LevelEntry
    // opens the level.
    virtual void OnDoubleClick(AssetBrowser& browser);

    // Draws this entry's right-click context-menu content. The caller
    // (AssetBrowser) has already opened the popup via
    // ImGui::BeginPopupContextItem() and will call ImGui::EndPopup()
    // afterwards; this only needs to emit MenuItem()s. Default is empty.
    virtual void DrawContextMenu(AssetBrowser& browser);

    [[nodiscard]] const std::filesystem::path& GetPath() const { return absolutePath; }
    [[nodiscard]] const std::filesystem::path& GetRelativePath() const { return relativePath; }
    [[nodiscard]] const std::string& GetDisplayName() const { return displayName; }

    [[nodiscard]] virtual AssetEntryType GetType() const = 0;

    // Default Other. GenericFileEntry (and its TextureEntry/LevelEntry
    // subclasses) override this to return the AssetKind derived from
    // their extension; see the comment on AssetKind above for why this is
    // tracked separately from AssetEntryType.
    [[nodiscard]] virtual AssetKind GetAssetKind() const { return AssetKind::Other; }

    // Non-nullopt only for entries the tile grid can show a live
    // thumbnail for (currently just TextureEntry). Precomputed once per
    // entry - see TextureEntry's constructor - rather than recomputed
    // every frame.
    [[nodiscard]] virtual std::optional<std::string> GetThumbnailReference() const { return std::nullopt; }

protected:
    AssetEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath, std::string displayName)
        : absolutePath(std::move(absolutePath))
        , relativePath(std::move(relativePath))
        , displayName(std::move(displayName)) {}

    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    std::string displayName;
};

// Folder-specific behavior. Double-click navigates into it; its context
// menu fully replaces the generic file one (Open / Rename / Delete /
// Create...) rather than sharing GenericFileEntry's Rename+Delete base,
// since folders offer a genuinely different set of actions than files.
class DirectoryEntry final : public AssetEntry {
public:
    DirectoryEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath, std::string displayName)
        : AssetEntry(std::move(absolutePath), std::move(relativePath), std::move(displayName)) {}

    void OnDoubleClick(AssetBrowser& browser) override;
    void DrawContextMenu(AssetBrowser& browser) override;
    [[nodiscard]] AssetEntryType GetType() const override { return AssetEntryType::Directory; }
};

// Shared behavior for every non-directory entry: knows its AssetKind
// (Texture / Sound / Script / Other, assigned by the extension registry in
// AssetBrowser.cpp), offers itself to a matching field widget on
// double-click, and draws the Rename/Delete items every file shares.
// TextureEntry and LevelEntry both build on this for their type-specific
// extras rather than duplicating Rename/Delete.
class GenericFileEntry : public AssetEntry {
public:
    GenericFileEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath,
                      std::string displayName, AssetKind kind)
        : AssetEntry(std::move(absolutePath), std::move(relativePath), std::move(displayName))
        , kind(kind) {}

    void OnDoubleClick(AssetBrowser& browser) override;
    void DrawContextMenu(AssetBrowser& browser) override;
    [[nodiscard]] AssetEntryType GetType() const override { return AssetEntryType::GenericFile; }
    [[nodiscard]] AssetKind GetAssetKind() const override { return kind; }

protected:
    // Shared by subclasses that extend the context menu (TextureEntry,
    // LevelEntry) so Rename/Delete are defined exactly once.
    void DrawRenameAndDeleteMenuItems(AssetBrowser& browser) const;

private:
    AssetKind kind;
};

// Texture-kind files - .png/.jpg/.jpeg, see the extension registry in
// AssetBrowser.cpp. Named for the asset's purpose (matching
// AssetKind::Texture) rather than "PngEntry": the existing engine already
// treats all three extensions identically as textures, and giving jpg/jpeg
// files their own near-duplicate class would just be repetition.
class TextureEntry final : public GenericFileEntry {
public:
    TextureEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath, std::string displayName);

    void DrawContextMenu(AssetBrowser& browser) override;
    [[nodiscard]] AssetEntryType GetType() const override { return AssetEntryType::Texture; }
    [[nodiscard]] std::optional<std::string> GetThumbnailReference() const override { return textureReference; }

private:
    // AssetBrowser::ToAssetReference(absolutePath, AssetKind::Texture),
    // precomputed once here (a real filesystem round-trip) rather than
    // every frame - this is the same one-time-canonicalization the old
    // AssetBrowserEntry::textureReference field used to provide.
    std::string textureReference;
};

// Level files - AssetBrowser::kLevelFileExtension (".bson"). Double-
// clicking one opens it through LevelManager rather than treating it as a
// field-reference asset; see LevelEntry::OnDoubleClick in AssetBrowser.cpp.
class LevelEntry final : public GenericFileEntry {
public:
    LevelEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath, std::string displayName)
        : GenericFileEntry(std::move(absolutePath), std::move(relativePath), std::move(displayName), AssetKind::Other) {}

    void OnDoubleClick(AssetBrowser& browser) override;
    void DrawContextMenu(AssetBrowser& browser) override;
    [[nodiscard]] AssetEntryType GetType() const override { return AssetEntryType::Level; }
};

// Extensible factory: classifies `absolutePath` by extension (case-
// insensitive - see the registry in AssetBrowser.cpp) and returns the
// matching AssetEntry subclass. Unregistered extensions still come back
// as a GenericFileEntry(AssetKind::Other), so every file is representable
// - nothing is ever hidden purely because its extension is unfamiliar.
// `rootDirectory` is only used to precompute the entry's relative path
// once; `isDirectory` reuses what the caller's directory_iterator entry
// already determined instead of re-querying the filesystem.
std::unique_ptr<AssetEntry> CreateAssetEntry(
    const std::filesystem::path& absolutePath,
    const std::filesystem::path& rootDirectory,
    bool isDirectory
);

// Which single modal (if any) AssetBrowser currently has open. Only one of
// these can be active at a time - see AssetBrowser::RequestRename() and
// its siblings, which each refuse to start a new modal while another one
// is already active.
enum class AssetBrowserModalKind {
    None,
    CreateFolder,
    CreateLevel,
    CreateFile,
    Rename,
    DeleteConfirm
};

// Which button the shared name-entry modal body was closed with this
// frame, if any.
enum class AssetBrowserModalAction {
    None,
    Confirmed,
    Cancelled
};

// Everything needed to draw whichever single modal is currently active.
// Tagged by `kind` rather than using one member per modal type, since only
// one is ever in use at once.
struct AssetBrowserModalState {
    AssetBrowserModalKind kind = AssetBrowserModalKind::None;

    std::filesystem::path destinationDirectory; // CreateFolder / CreateLevel / CreateFile
    std::filesystem::path targetPath;           // Rename / DeleteConfirm
    bool targetIsDirectory = false;             // Rename / DeleteConfirm
    std::string forcedExtension;                // CreateFile only: appended automatically if non-empty

    // Rename only: the target's AssetKind at the moment RequestRename() was
    // called, captured before anything can change - see
    // AssetBrowser::NotifyAssetReferenceRenamed(), called from
    // DrawRenameModal()'s success branch once the rename actually happens.
    // AssetKind::Other (its default, and DirectoryEntry's fixed value) means
    // "not a reference-tracked kind" - see NotifyAssetReferenceRenamed().
    AssetKind targetAssetKind = AssetKind::Other;

    char nameBuffer[256] = "";
    std::string errorMessage;
    bool justOpened = false; // true for exactly one frame, so ImGui::OpenPopup() fires once
};

// A drag-and-drop move accepted by AssetBrowser::DrawMoveDropTarget(),
// queued for AssetBrowser::PerformPendingMove() to actually carry out once
// the entries grid has fully finished drawing for this frame. See
// AssetBrowser::HandleDroppedMove()'s comment for why performing the move
// immediately, from inside the grid's own draw pass, isn't safe.
struct AssetBrowserPendingMove {
    std::filesystem::path source;
    std::filesystem::path destinationDirectory;

    // Which payload type this drag was actually offering - see
    // DrawEntryTile()'s drag source and DrawMoveDropTarget()'s probing
    // loop. AssetKind::Other covers both folders and non-reference-tracked
    // files; AssetBrowser::NotifyAssetReferenceRenamed() treats it as
    // "nothing to propagate".
    AssetKind kind = AssetKind::Other;
};

// Self-contained, reusable ImGui asset browser locked to a single root
// directory (intended to be the current project's Assets folder). A
// general-purpose hierarchical filesystem browser: any file type is
// allowed in any folder under the root, at any nesting depth. Asset type
// is always derived from extension (see CreateAssetEntry / AssetEntry::
// GetType), never from which folder a file happens to sit in.
//
// Filesystem scanning is kept separate from drawing: ScanCurrentDirectory()
// only runs when the browser navigates, or after a mutating operation
// (create/rename/delete/import), or Refresh() is called explicitly - never
// on every frame - and it only ever lists the CURRENT directory's direct
// children, never the whole asset tree.
//
// There is no permanent index of any kind here (see EditorTextureCache for
// the equally index-free texture preview cache). Assets are referenced by
// name, resolved on demand.
class AssetBrowser {
public:
    using ThumbnailProvider = std::function<ImTextureID(const std::string&)>;

    void RequestOpenScript(const std::filesystem::path& absolutePath);
    void DrawTextEditorWindow(ImFont* scriptEditorFont);

    // The one place the expected level-file extension is spelled out, per
    // the "make it configurable in one obvious constant" requirement.
    // LevelEntry's registration, the Create Level modal, and
    // BuildLevelFileName() (AssetBrowser.cpp) all read it from here -
    // change this single line if the engine's level format ever adopts a
    // different extension.
    static constexpr std::string_view kLevelFileExtension = ".bson";

    AssetBrowser() = default;
    ~AssetBrowser() = default;

    AssetBrowser(const AssetBrowser&) = delete;
    AssetBrowser& operator=(const AssetBrowser&) = delete;

    // Locks the browser to `root` (creating it if missing) and navigates
    // back to it. Unlike the previous version, this no longer creates any
    // fixed Textures/Sounds/Scripts subfolders - assets are allowed in any
    // folder under the root, so nothing is assumed to live at a
    // predetermined path.
    void SetRootDirectory(const std::filesystem::path& root);

    // Re-scans the CURRENT directory only. Safe to call any time from
    // outside the entries grid's own draw pass - e.g. from a user-facing
    // "Refresh" button after files changed on disk, or after this
    // browser's own create/rename/delete/import operations. NOT safe to
    // call synchronously from inside DrawEntries()'s loop over `entries`
    // (e.g. from DrawEntryTile()): it destroys every current AssetEntry,
    // including the one whose Draw call may still be on the stack.
    // Anything triggered from there must defer through a pending-state
    // field instead (see pendingNavigation, pendingMove) and be carried
    // out only after DrawEntries() has returned.
    void Refresh();

    // Draws the error banner (if any), breadcrumbs, search field, the
    // folder/asset grid, every modal, and handles keyboard shortcuts. Call
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

    // Imports a file or directory from OUTSIDE the project (e.g. dropped
    // from the OS file manager) into the folder currently being browsed.
    // Handles name collisions by appending " (2)", " (3)", etc. Never
    // throws; returns false (and reports why, via spdlog and the
    // in-browser error banner) on any failure.
    bool ImportExternalFile(const std::filesystem::path& sourceAbsolutePath);

    // Hit-tests a SCREEN-space point (e.g. from an SDL_EVENT_DROP_FILE or
    // SDL_EVENT_DROP_POSITION, whose x/y are window-relative - equivalent
    // to ImGui screen space here since this editor never enables
    // multi-viewport ImGui) against this panel's on-screen rectangle as of
    // the last Draw() call.
    [[nodiscard]] bool IsScreenPointInside(float screenX, float screenY) const;

    // Call from the host application's SDL event loop while an OS-level
    // drag is in progress (SDL_EVENT_DROP_POSITION -> IsScreenPointInside
    // for that event's x/y; SDL_EVENT_DROP_COMPLETE, or the drag leaving
    // this panel, -> false) so Draw() can outline this panel while it is
    // the active drop target. Purely cosmetic - has no effect on whether a
    // subsequent ImportExternalFile() call succeeds.
    void SetExternalDragHovering(bool isHovering);

    // Drag-and-drop payload type for a given asset kind, so field widgets
    // can register a matching ImGui::AcceptDragDropPayload target. The
    // payload data is the absolute source path as a null-terminated UTF-8
    // string.
    static const char* DragDropPayloadTypeFor(AssetKind kind);

    // Converts an absolute path into the reference string a field of the
    // given kind should store (relative-with-extension for textures;
    // relative-without-extension for sounds/scripts).
    static std::string ToAssetReference(const std::filesystem::path& absolutePath, AssetKind kind);

    // --- Entry-facing operations -----------------------------------------
    // Called by AssetEntry subclasses (and by AssetBrowser's own keyboard-
    // shortcut handling) to request browser-level actions. Kept separate
    // from the private Navigate/Scan/filesystem workhorses below so entry
    // objects only ever see this intent-level API, never the internals -
    // the same reason ConsumePendingConfirmedSelection exists for field
    // widgets rather than exposing the pending-selection fields directly.
    void RequestNavigate(const std::filesystem::path& absoluteDirectory);
    void RequestRename(const std::filesystem::path& targetAbsolutePath, bool targetIsDirectory);
    void RequestDelete(const std::filesystem::path& targetAbsolutePath, bool targetIsDirectory);
    void RequestCreateFolder(const std::filesystem::path& destinationDirectory);
    void RequestCreateLevel(const std::filesystem::path& destinationDirectory);
    void RequestCreateFile(const std::filesystem::path& destinationDirectory, const std::string& forcedExtension);
    void RequestOpenLevel(const std::filesystem::path& absolutePath);
    void RequestConsumeAsFieldReference(AssetKind kind, const std::filesystem::path& absolutePath);

    // Extensible "Create File" submenu content, shared by DirectoryEntry's
    // context menu and the empty-space context menu. Add another
    // ImGui::MenuItem() + RequestCreateFile() pair here to register
    // another quick-create file type.
    void DrawCreateFileSubmenuItems(const std::filesystem::path& destinationDirectory);

private:
    void SaveOpenScript();

    TextEditor scriptEditor;
    std::filesystem::path openScriptPath;

    bool scriptEditorOpen = false;
    bool scriptEditorDirty = false;

    void NavigateTo(const std::filesystem::path& absoluteDirectory);
    void NavigateToParent();
    [[nodiscard]] bool IsPathWithinRoot(const std::filesystem::path& absolutePath) const;
    [[nodiscard]] std::string DisplayRelative(const std::filesystem::path& absolutePath) const;
    [[nodiscard]] AssetEntry* FindEntry(const std::filesystem::path& absolutePath) const;
    void ClearSelectionUnder(const std::filesystem::path& removedPath);

    void ScanCurrentDirectory();

    void DrawBreadcrumbs();
    void DrawSearchBar();
    void DrawEntries(const ThumbnailProvider& thumbnailProvider);
    void DrawEntryTile(AssetEntry& entry, float tileSize, const ThumbnailProvider& thumbnailProvider);

    // Shared by DrawEntryTile() (folder tiles) and DrawBreadcrumbs() (the
    // "^ Up" button and each breadcrumb segment): call right after this
    // element is drawn. Accepts a drag currently hovering over it as a
    // request to move whatever's being dragged into `destinationDirectory`,
    // queuing the move via HandleDroppedMove() rather than performing it
    // here - see pendingMove for why. Returns true whenever a compatible
    // drag is hovering - even before release - so the caller can draw a
    // highlight.
    bool DrawMoveDropTarget(const std::filesystem::path& destinationDirectory);

    // Queues a drop accepted by DrawMoveDropTarget() into pendingMove
    // instead of performing the move immediately. This runs from inside
    // DrawEntries()'s loop over `entries` for a folder-tile drop - a move
    // can delete and recreate every AssetEntry via Refresh(), including
    // the very folder entry whose DrawEntryTile() call is still executing
    // higher up the call stack, so doing it here would free that entry
    // (and invalidate `entries` itself) out from under code still using
    // it. PerformPendingMove() does the actual move once Draw() has
    // returned from DrawEntries() entirely. `kind` is whichever payload
    // type DrawMoveDropTarget() actually matched (AssetKind::Other for a
    // folder or a non-reference-tracked file) - see
    // NotifyAssetReferenceRenamed().
    void HandleDroppedMove(AssetKind kind, const ImGuiPayload& payload, const std::filesystem::path& destinationDirectory);

    // Performs a move queued by HandleDroppedMove(), if any is pending.
    // Called once per frame from Draw(), after DrawEntries() has fully
    // returned - see HandleDroppedMove()'s comment for why the move can't
    // happen any earlier than that.
    void PerformPendingMove();

    // Called after a Texture/Sound/Script entry is successfully moved
    // (PerformPendingMove()) or renamed (DrawRenameModal()) to whichever
    // absolute path it's now at. Textures, sounds, and scripts are all
    // referenced elsewhere (wall/sector/sprite textures; sound and script
    // components) by the exact string ToAssetReference() computes for
    // their kind - moving or renaming the underlying file without telling
    // whoever holds that string leaves it pointing at a name nothing
    // resolves to anymore, which is why "moving a sprite's texture breaks
    // it" in the first place. This looks up the old and new reference
    // strings and asks LevelManager to rewrite any match in the currently
    // loaded level. A no-op for AssetKind::Other (folders, Levels, and
    // anything else with no tracked reference) or when the computed
    // reference string doesn't actually change.
    //
    // Requires LevelManager to expose RenameTextureReference(),
    // RenameSoundReference(), and RenameScriptReference() (each taking the
    // old and new reference strings and rewriting every matching field in
    // the current level) - these do not exist yet as of this writing.
    void NotifyAssetReferenceRenamed(
        AssetKind kind, const std::filesystem::path& oldAbsolutePath, const std::filesystem::path& newAbsolutePath
    );

    void DrawEmptySpaceContextMenu();
    void HandleKeyboardShortcuts();

    // Shared "type a name, see an error, Create/Cancel" chrome for the
    // Create Folder / Create Level / Create File modals; each caller still
    // owns its own validation and filesystem work.
    AssetBrowserModalAction DrawNameEntryModalBody(const std::string& description);
    void DrawCreateFolderModal();
    void DrawCreateLevelModal();
    void DrawCreateFileModal();
    void DrawRenameModal();
    void DrawDeleteConfirmModal();

    std::filesystem::path rootDirectory;
    std::filesystem::path currentDirectory;

    std::optional<std::filesystem::path> pendingNavigation;

    // Set by HandleDroppedMove() when a drag-and-drop move is accepted
    // this frame; consumed by PerformPendingMove(), called from Draw()
    // only after DrawEntries() has returned. See HandleDroppedMove()'s
    // comment for why this can't be performed immediately.
    std::optional<AssetBrowserPendingMove> pendingMove;

    std::vector<std::unique_ptr<AssetEntry>> entries;
    bool scanFailed = false;

    char searchBuffer[128] = "";

    std::filesystem::path selectedFile;

    std::optional<std::filesystem::path> pendingConfirmedPath;
    AssetKind pendingConfirmedKind = AssetKind::Other;

    // Last known on-screen rectangle of the host ImGui window, captured at
    // the top of Draw(). Used to scope OS-level file drops (and the
    // hover-highlight below) to this panel instead of a drop that landed
    // somewhere else entirely.
    float lastWindowScreenMinX = 0.0f;
    float lastWindowScreenMinY = 0.0f;
    float lastWindowScreenMaxX = 0.0f;
    float lastWindowScreenMaxY = 0.0f;
    bool externalDragHovering = false;

    // Non-modal operation failures (OS-file-drop imports, level-open
    // failures reached via double-click rather than a modal) surfaced as a
    // dismissible banner at the top of Draw(). Modal-specific failures
    // (rename/delete/create) live in activeModal.errorMessage instead,
    // inside the modal itself, per the per-workflow "show a useful error
    // message inside the modal" requirement.
    std::string lastOperationError;

    AssetBrowserModalState activeModal;
};