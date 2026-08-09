#include "Headers/Editor/AssetBrowser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "imgui.h"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/InputManager.hpp"

namespace fs = std::filesystem;

namespace {
    std::string LowerCopy(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    std::string UpperCopy(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return text;
    }

    bool IsHidden(const fs::path& path) {
        const std::string name = path.filename().string();
        return !name.empty() && name.front() == '.';
    }

    // Shrinks `text` (plus an ellipsis) until it fits inside `maxWidth`.
    // Relies on the currently bound ImGui font, so only call while drawing.
    std::string TruncateToWidth(const std::string& text, const float maxWidth) {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;

        constexpr const char* ellipsis = "...";
        const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;

        std::string truncated = text;
        while (!truncated.empty() &&
               ImGui::CalcTextSize(truncated.c_str()).x + ellipsisWidth > maxWidth) {
            truncated.pop_back();
        }

        return truncated.empty() ? std::string(ellipsis) : truncated + ellipsis;
    }

    // Relative-path helper shared by ToAssetReference: relative to `base`
    // if the file is actually inside it, else just the bare filename as a
    // safe fallback (never an absolute path leaking into level data). Note
    // this is independent of where the browser lets a texture/sound/script
    // physically live - a file outside its conventional folder is still
    // browsable and usable, it just falls back to a bare-filename
    // reference here instead of a folder-relative one.
    fs::path RelativeOrFallback(const fs::path& absolutePath, const fs::path& base) {
        std::error_code ec;
        const fs::path canonicalBase = fs::weakly_canonical(base, ec);
        if (ec) return absolutePath.filename();

        const fs::path canonicalPath = fs::weakly_canonical(absolutePath, ec);
        if (ec) return absolutePath.filename();

        const fs::path rel = canonicalPath.lexically_relative(canonicalBase);
        if (rel.empty() || *rel.begin() == "..")
            return absolutePath.filename();

        return rel;
    }

    void CopyIntoNameBuffer(char* buffer, const std::size_t bufferSize, const std::string& text) {
        const std::size_t copyLength = std::min(text.size(), bufferSize - 1);
        std::memcpy(buffer, text.data(), copyLength);
        buffer[copyLength] = '\0';
    }

    // --- Name validation, shared by every Create/Rename modal ---------------

    bool IsValidNewFileSystemName(const std::string& name, std::string& outError) {
        if (name.empty()) {
            outError = "Name cannot be empty.";
            return false;
        }

        if (name == "." || name == "..") {
            outError = "\".\" and \"..\" are not valid names.";
            return false;
        }

        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            outError = "Name cannot contain path separators.";
            return false;
        }

        static constexpr std::string_view kInvalidChars = "<>:\"|?*";
        for (const unsigned char c : name) {
            if (c < 0x20) {
                outError = "Name contains an invalid control character.";
                return false;
            }
            if (kInvalidChars.find(static_cast<char>(c)) != std::string_view::npos) {
                outError = "Name contains a character that is not allowed: " + std::string(1, static_cast<char>(c));
                return false;
            }
        }

        if (name.back() == ' ' || name.back() == '.') {
            outError = "Name cannot end with a space or a period.";
            return false;
        }

        static constexpr std::array<std::string_view, 22> kReservedWindowsNames = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };

        const std::string stemUpper = UpperCopy(fs::path(name).stem().string());
        if (std::ranges::find(kReservedWindowsNames, stemUpper) != kReservedWindowsNames.end()) {
            outError = "\"" + name + "\" is a reserved system name.";
            return false;
        }

        return true;
    }

    // --- Create Folder --------------------------------------------------

    bool TryCreateFolder(const fs::path& destinationDirectory, const std::string& name,
                          fs::path& createdPath, std::string& errorMessage) {
        if (!IsValidNewFileSystemName(name, errorMessage)) return false;

        std::error_code destEc;
        if (!fs::exists(destinationDirectory, destEc) || !fs::is_directory(destinationDirectory, destEc)) {
            errorMessage = "Destination folder no longer exists.";
            return false;
        }

        const fs::path destination = destinationDirectory / name;

        std::error_code existsEc;
        if (fs::exists(destination, existsEc)) {
            errorMessage = "An item with that name already exists.";
            return false;
        }

        std::error_code createEc;
        if (!fs::create_directory(destination, createEc) || createEc) {
            errorMessage = createEc ? ("Could not create folder: " + createEc.message()) : "Could not create folder.";
            return false;
        }

        createdPath = destination;
        return true;
    }

    // --- Rename ----------------------------------------------------------

    bool TryRenameEntry(const fs::path& targetPath, const bool targetIsDirectory, const std::string& enteredName,
                         fs::path& newPath, std::string& errorMessage) {
        if (!IsValidNewFileSystemName(enteredName, errorMessage)) return false;

        std::error_code existsEc;
        if (!fs::exists(targetPath, existsEc)) {
            errorMessage = "This item no longer exists.";
            return false;
        }

        // Preserve the current extension by default: only files (not
        // folders) have one, and only if the user didn't explicitly type a
        // different one of their own.
        std::string finalName = enteredName;
        if (!targetIsDirectory) {
            const fs::path enteredAsPath(enteredName);
            if (!enteredAsPath.has_extension() && targetPath.has_extension()) {
                finalName += targetPath.extension().string();
            }
        }

        if (finalName == targetPath.filename().string()) {
            newPath = targetPath; // nothing actually changed - a harmless no-op
            return true;
        }

        const fs::path destination = targetPath.parent_path() / finalName;

        std::error_code destExistsEc;
        if (fs::exists(destination, destExistsEc)) {
            // On case-insensitive filesystems, a pure-case rename (e.g.
            // "Foo.png" -> "foo.png") makes `destination` "exist" as soon
            // as we look it up, even though it's the very file being
            // renamed. Only treat this as a real collision if it resolves
            // to a genuinely different file.
            std::error_code equivEc;
            const bool sameUnderlyingFile = fs::equivalent(destination, targetPath, equivEc) && !equivEc;
            if (!sameUnderlyingFile) {
                errorMessage = "An item with that name already exists.";
                return false;
            }
        }

        std::error_code renameEc;
        fs::rename(targetPath, destination, renameEc);
        if (renameEc) {
            errorMessage = "Could not rename: " + renameEc.message();
            return false;
        }

        newPath = destination;
        return true;
    }

    // --- Delete ------------------------------------------------------------

    bool TryDeleteEntry(const fs::path& targetPath, const bool targetIsDirectory, std::string& errorMessage) {
        std::error_code existsEc;
        if (!fs::exists(targetPath, existsEc)) {
            errorMessage = "This item no longer exists.";
            return false;
        }

        std::error_code opEc;
        if (targetIsDirectory) fs::remove_all(targetPath, opEc);
        else fs::remove(targetPath, opEc);

        if (opEc) {
            errorMessage = "Could not delete: " + opEc.message();
            return false;
        }

        return true;
    }

    // --- Create Level ------------------------------------------------------

    // Strips a trailing, case-insensitive occurrence of the level extension
    // from `enteredName` (so typing "MyLevel" or "MyLevel.bson" both land on
    // the same file) and appends AssetBrowser::kLevelFileExtension.
    fs::path BuildLevelFileName(const std::string& enteredName) {
        std::string stem = enteredName;
        const std::string ext(AssetBrowser::kLevelFileExtension);

        if (stem.size() >= ext.size()) {
            const std::string tail = stem.substr(stem.size() - ext.size());
            if (LowerCopy(tail) == LowerCopy(ext)) {
                stem.resize(stem.size() - ext.size());
            }
        }

        return fs::path(stem + ext);
    }

    [[nodiscard]] bool CreateLevelAsset(
        const fs::path& destinationDirectory,
        const std::string& levelName,
        fs::path& createdLevelPath,
        std::string& errorMessage
    ) {
        (void)createdLevelPath; // only assigned on the success path, unreachable until the TODO below is resolved

        std::string validationError;
        if (!IsValidNewFileSystemName(levelName, validationError)) {
            errorMessage = validationError;
            return false;
        }

        std::error_code destEc;
        if (!fs::exists(destinationDirectory, destEc) || !fs::is_directory(destinationDirectory, destEc)) {
            errorMessage = "Destination folder no longer exists.";
            return false;
        }

        const fs::path fileName = BuildLevelFileName(levelName);
        const fs::path destination = destinationDirectory / fileName;

        std::error_code existsEc;
        if (fs::exists(destination, existsEc)) {
            errorMessage = "A level with that name already exists.";
            return false;
        }

        const fs::path newLevelPath = destination;
        Level newLevel{};
        newLevel.name = destination.stem().string();

        newLevel.nextEntityID = 1;
        newLevel.nextSectorID = 0;
        newLevel.nextWallID = 0;

        if (!LevelSerialization::SaveLevelToFile(destination,newLevel)) return false;

        createdLevelPath = destination;
        return true;
    }

    // --- Create File (extensible "New File" submenu) ------------------------

    bool CreateGenericFileAsset(
        const fs::path &destinationDirectory,
        const std::string &fileName,
        fs::path &createdFilePath,
        std::string &errorMessage
    ) {
        std::string validationError;
        if (!IsValidNewFileSystemName(fileName, validationError)) {
            errorMessage = validationError;
            return false;
        }

        std::error_code destEc;
        if (!fs::exists(destinationDirectory, destEc) ||
            !fs::is_directory(destinationDirectory, destEc)) {
            errorMessage = "Destination folder no longer exists.";
            return false;
        }

        const fs::path destination = destinationDirectory / fileName;

        std::error_code existsEc;
        if (fs::exists(destination, existsEc)) {
            errorMessage = "A file with that name already exists.";
            return false;
        }

        std::ofstream file(destination, std::ios::binary);
        if (!file.is_open()) {
            errorMessage = "Could not create the file.";
            return false;
        }

        if (destination.extension() == ".lua") {
            file <<
                    R"(function Start()

end

function Update()

end
)";
        }

        if (!file.good()) {
            errorMessage = "Could not write to the file.";
            file.close();

            std::error_code removeEc;
            fs::remove(destination, removeEc);

            return false;
        }

        file.close();

        createdFilePath = destination;
        return true;
    }

    // --- Open Level ----------------------------------------------------------

    // See LevelEntry::OnDoubleClick / AssetBrowser::RequestOpenLevel for how
    // this is reached and where the asset-root containment check happens
    // (it needs AssetBrowser's rootDirectory, so it lives one level up from
    // here rather than in this free function).
    [[nodiscard]] bool OpenLevelAsset(const fs::path& levelPath, std::string& errorMessage) {
        std::error_code existsEc;
        if (!fs::exists(levelPath, existsEc) || existsEc) {
            errorMessage = "Level file no longer exists: " + levelPath.string();
            return false;
        }

        std::error_code regularEc;
        if (!fs::is_regular_file(levelPath, regularEc) || regularEc) {
            errorMessage = "Level path is not a regular file: " + levelPath.string();
            return false;
        }

        std::error_code canonicalEc;
        const fs::path normalizedLevelPath = fs::weakly_canonical(levelPath, canonicalEc);
        const fs::path& levelPathToOpen = canonicalEc ? levelPath : normalizedLevelPath;

        if (!LevelManager::LoadLevelFromFile(levelPathToOpen)) {
            errorMessage = "Failed to open level (see log for details): " + levelPathToOpen.string();
            return false;
        }

        return true;
    }

    // --- Extension registry, backing CreateAssetEntry ------------------------

    enum class RegisteredExtensionKind { Texture, Sound, Script, Level };

    // Extension -> first-class kind. Matching is case-insensitive (see
    // LowerCopy). Add an entry here (and, if it needs behavior beyond just
    // a different AssetKind tag, a case in CreateAssetEntry below) to
    // register a new extension-driven asset type; anything absent from
    // this map still comes back as a GenericFileEntry(AssetKind::Other)
    // rather than being hidden - this is what lets "unknown extensions
    // must still appear" hold in general.
    const std::unordered_map<std::string, RegisteredExtensionKind> kExtensionRegistry = {
        { ".png",  RegisteredExtensionKind::Texture },
        { ".jpg",  RegisteredExtensionKind::Texture },
        { ".jpeg", RegisteredExtensionKind::Texture },
        { ".wav",  RegisteredExtensionKind::Sound   },
        { ".lua",  RegisteredExtensionKind::Script  },
        { std::string(AssetBrowser::kLevelFileExtension), RegisteredExtensionKind::Level },
    };

    // --- Tile visuals, keyed off the entry rather than a bare AssetKind so
    // Level (which has no AssetKind of its own) still gets a distinct look.

    const char* EntryVisualTag(const AssetEntry& entry) {
        if (entry.GetType() == AssetEntryType::Level) return "lvl";

        switch (entry.GetAssetKind()) {
            case AssetKind::Sound: return "wav";
            case AssetKind::Script: return "lua";
            default: return "file";
        }
    }

    ImU32 EntryTileTint(const AssetEntry& entry) {
        if (entry.GetType() == AssetEntryType::Level) return IM_COL32(90, 60, 110, 255);

        switch (entry.GetAssetKind()) {
            case AssetKind::Sound: return IM_COL32(45, 70, 90, 255);
            case AssetKind::Script: return IM_COL32(55, 80, 55, 255);
            default: return IM_COL32(60, 60, 65, 255);
        }
    }
}

// ============================================================================
// Text Editor
// ============================================================================
void AssetBrowser::RequestOpenScript(const std::filesystem::path& absolutePath) {
    if (!IsPathWithinRoot(absolutePath)) {
        lastOperationError = "Refused to open a script outside the asset root: " + absolutePath.string();
        return;
    }

    if (LowerCopy(absolutePath.extension().string()) != ".lua") {
        lastOperationError = "Only Lua scripts can be opened.";
        return;
    }

    std::ifstream file(absolutePath, std::ios::binary);
    if (!file.is_open()) {
        lastOperationError =
            "Could not open script: " + absolutePath.string();
        return;
    }

    const std::string contents{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{}
    };

    scriptEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());

    scriptEditor.SetText(contents);

    openScriptPath = absolutePath;
    scriptEditorOpen = true;
    scriptEditorDirty = false;
    lastOperationError.clear();
}

void AssetBrowser::SaveOpenScript() {
    if (openScriptPath.empty()) return;

    std::ofstream file(openScriptPath, std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        lastOperationError = "Could not save script: " + openScriptPath.string();
        return;
    }

    file << scriptEditor.GetText();

    if (!file.good()) {
        lastOperationError = "Failed while writing script: " + openScriptPath.string();
        return;
    }

    scriptEditorDirty = false;
    lastOperationError.clear();
}

void AssetBrowser::DrawTextEditorWindow(ImFont* scriptEditorFont) {
    if (!scriptEditorOpen) return;

    std::string title = openScriptPath.filename().string();

    if (scriptEditorDirty) title += " *";

    title += "##TilkyScriptEditor";

    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f),ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title.c_str(), &scriptEditorOpen, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    bool saveRequested = false;

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save", "Ctrl+S", false, scriptEditorDirty)) saveRequested = true;

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (scriptEditorFont != nullptr) [[likely]] ImGui::PushFont(scriptEditorFont);
    scriptEditor.Render("##LuaCodeEditor", ImGui::GetContentRegionAvail(),false);
    if (scriptEditorFont != nullptr) [[likely]] ImGui::PopFont();

    if (scriptEditor.IsTextChanged()) scriptEditorDirty = true;

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveRequested = true;

    if (saveRequested) SaveOpenScript();

    ImGui::End();
}

// ============================================================================
// AssetEntry hierarchy
// ============================================================================

void AssetEntry::OnDoubleClick(AssetBrowser&) {
    // Default: no-op. Concrete entry types override this for anything that
    // should happen on double-click / Enter.
}

void AssetEntry::DrawContextMenu(AssetBrowser&) {
    // Default: no extra context-menu content.
}

void DirectoryEntry::OnDoubleClick(AssetBrowser& browser) {
    browser.RequestNavigate(GetPath());
}

void DirectoryEntry::DrawContextMenu(AssetBrowser& browser) {
    if (ImGui::MenuItem("Open")) browser.RequestNavigate(GetPath());
    if (ImGui::MenuItem("Rename")) browser.RequestRename(GetPath(), true);
    if (ImGui::MenuItem("Delete")) browser.RequestDelete(GetPath(), true);

    ImGui::Separator();

    if (ImGui::MenuItem("Create Folder")) browser.RequestCreateFolder(GetPath());
    if (ImGui::MenuItem("Create Level")) browser.RequestCreateLevel(GetPath());
    if (ImGui::BeginMenu("Create File")) {
        browser.DrawCreateFileSubmenuItems(GetPath());
        ImGui::EndMenu();
    }
}

void GenericFileEntry::OnDoubleClick(AssetBrowser& browser) {
    if (kind == AssetKind::Script) {
        browser.RequestOpenScript(GetPath());
        return;
    }

    if (kind != AssetKind::Other) {
        browser.RequestConsumeAsFieldReference(kind, GetPath());
    }
}

void GenericFileEntry::DrawRenameAndDeleteMenuItems(AssetBrowser& browser) const {
    if (ImGui::MenuItem("Rename")) browser.RequestRename(GetPath(), false);
    if (ImGui::MenuItem("Delete")) browser.RequestDelete(GetPath(), false);
}

void GenericFileEntry::DrawContextMenu(AssetBrowser& browser) {
    DrawRenameAndDeleteMenuItems(browser);
}

TextureEntry::TextureEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath, std::string displayName)
    : GenericFileEntry(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Texture)
    , textureReference(AssetBrowser::ToAssetReference(absolutePath, AssetKind::Texture)) {}

void TextureEntry::DrawContextMenu(AssetBrowser& browser) {
    if (ImGui::MenuItem("Copy Texture Reference")) {
        ImGui::SetClipboardText(textureReference.c_str());
    }
    ImGui::Separator();
    DrawRenameAndDeleteMenuItems(browser);
}

void LevelEntry::OnDoubleClick(AssetBrowser& browser) {
    browser.RequestOpenLevel(GetPath());
}

void LevelEntry::DrawContextMenu(AssetBrowser& browser) {
    if (ImGui::MenuItem("Open Level")) browser.RequestOpenLevel(GetPath());
    ImGui::Separator();
    DrawRenameAndDeleteMenuItems(browser);
}

std::unique_ptr<AssetEntry> CreateAssetEntry(
    const std::filesystem::path& absolutePath,
    const std::filesystem::path& rootDirectory,
    const bool isDirectory
) {
    fs::path relativePath = absolutePath.lexically_relative(rootDirectory);
    std::string displayName = absolutePath.filename().string();

    if (isDirectory) {
        return std::make_unique<DirectoryEntry>(absolutePath, std::move(relativePath), std::move(displayName));
    }

    const std::string extension = LowerCopy(absolutePath.extension().string());
    const auto it = kExtensionRegistry.find(extension);

    if (it == kExtensionRegistry.end()) {
        return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Other);
    }

    switch (it->second) {
        case RegisteredExtensionKind::Texture:
            return std::make_unique<TextureEntry>(absolutePath, std::move(relativePath), std::move(displayName));
        case RegisteredExtensionKind::Sound:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Sound);
        case RegisteredExtensionKind::Script:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Script);
        case RegisteredExtensionKind::Level:
            return std::make_unique<LevelEntry>(absolutePath, std::move(relativePath), std::move(displayName));
    }

    return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Other);
}

// ============================================================================
// AssetBrowser
// ============================================================================

const char* AssetBrowser::DragDropPayloadTypeFor(const AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return "TILKY_ASSET_TEXTURE";
        case AssetKind::Sound:   return "TILKY_ASSET_SOUND";
        case AssetKind::Script:  return "TILKY_ASSET_SCRIPT";
        default:                 return "TILKY_ASSET_OTHER";
    }
}

std::string AssetBrowser::ToAssetReference(const std::filesystem::path& absolutePath, const AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture:return RelativeOrFallback(absolutePath,ProjectManager::GetAssetsPath()).generic_string();

        case AssetKind::Sound: {
            fs::path rel = RelativeOrFallback(absolutePath, ProjectManager::GetSoundsPath());
            rel.replace_extension();
            return rel.generic_string();
        }

        case AssetKind::Script: {
            fs::path rel = RelativeOrFallback(absolutePath, ProjectManager::GetScriptsPath());
            rel.replace_extension();
            return rel.generic_string();
        }

        default: return absolutePath.filename().string();
    }
}

void AssetBrowser::SetRootDirectory(const std::filesystem::path& root) {
    if (!fs::exists(root)) {
        try {
            fs::create_directories(root);
            spdlog::warn("Asset browser: created missing asset root folder: {}", root.string());
        }
        catch (const fs::filesystem_error& e) {
            spdlog::error("Asset browser: failed to create asset root folder {}: {}", root.string(), e.what());
        }
    }

    rootDirectory = root;
    currentDirectory = root;
    selectedFile.clear();
    pendingConfirmedPath.reset();
    searchBuffer[0] = '\0';
    lastOperationError.clear();
    externalDragHovering = false;
    activeModal = AssetBrowserModalState{};

    ScanCurrentDirectory();
}

void AssetBrowser::Refresh() {
    ScanCurrentDirectory();
}

bool AssetBrowser::IsPathWithinRoot(const std::filesystem::path& absolutePath) const {
    std::error_code ec;

    const fs::path canonicalRoot = fs::weakly_canonical(rootDirectory, ec);
    if (ec) return false;

    const fs::path canonicalCandidate = fs::weakly_canonical(absolutePath, ec);
    if (ec) return false;

    const fs::path rel = canonicalCandidate.lexically_relative(canonicalRoot);
    if (rel.empty()) return false;
    if (rel == ".") return true;

    return *rel.begin() != "..";
}

std::string AssetBrowser::DisplayRelative(const std::filesystem::path& absolutePath) const {
    const fs::path relative = absolutePath.lexically_relative(rootDirectory);

    if (relative.empty() || relative == ".")
        return rootDirectory.filename().empty() ? std::string("Assets") : rootDirectory.filename().string();

    return relative.generic_string();
}

AssetEntry* AssetBrowser::FindEntry(const std::filesystem::path& absolutePath) const {
    for (const std::unique_ptr<AssetEntry>& entry : entries) if (entry->GetPath() == absolutePath) return entry.get();
    return nullptr;
}

void AssetBrowser::ClearSelectionUnder(const std::filesystem::path& removedPath) {
    if (selectedFile.empty()) return;

    if (selectedFile == removedPath) {
        selectedFile.clear();
        return;
    }

    // Also clear if the selection was somewhere inside a recursively
    // deleted folder.
    const fs::path rel = selectedFile.lexically_relative(removedPath);
    if (!rel.empty() && *rel.begin() != "..") {
        selectedFile.clear();
    }
}

void AssetBrowser::NavigateTo(const std::filesystem::path& absoluteDirectory) {
    if (!IsPathWithinRoot(absoluteDirectory)) return;
    if (!fs::exists(absoluteDirectory) || !fs::is_directory(absoluteDirectory)) return;

    currentDirectory = absoluteDirectory;
    searchBuffer[0] = '\0';
    ScanCurrentDirectory();
}

void AssetBrowser::NavigateToParent() {
    if (currentDirectory == rootDirectory) return; // never step above the allowed root

    NavigateTo(currentDirectory.parent_path());
}

void AssetBrowser::ScanCurrentDirectory() {
    entries.clear();
    scanFailed = false;

    if (currentDirectory.empty()) {
        scanFailed = true;
        return;
    }

    std::error_code ec;
    if (!fs::exists(currentDirectory, ec) || !fs::is_directory(currentDirectory, ec)) {
        // The directory we were showing has disappeared out from under us
        // (deleted externally, or deleted through this browser while a
        // stale reference to it was still current). Fall back to the asset
        // root instead of getting stuck showing a dead directory.
        spdlog::warn("Asset browser: current directory disappeared, returning to root: {}", currentDirectory.string());
        currentDirectory = rootDirectory;
        selectedFile.clear();

        std::error_code rootEc;
        if (!fs::exists(currentDirectory, rootEc) || !fs::is_directory(currentDirectory, rootEc)) {
            scanFailed = true;
            return;
        }
    }

    try {
        for (const auto& dirEntry : fs::directory_iterator(currentDirectory)) {
            try {
                const fs::path absolutePath = dirEntry.path();
                if (IsHidden(absolutePath)) continue;

                const bool isDirectory = dirEntry.is_directory();
                if (!isDirectory && !dirEntry.is_regular_file()) continue; // skip devices, broken symlinks, etc.

                entries.push_back(CreateAssetEntry(absolutePath, rootDirectory, isDirectory));
            }
            catch (const fs::filesystem_error& e) {
                // One bad entry (broken symlink, permission issue, etc.)
                // must not take the whole listing down with it.
                spdlog::warn("Asset browser: skipping inaccessible entry: {}", e.what());
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        spdlog::warn("Asset browser: failed to read directory {}: {}", currentDirectory.string(), e.what());
        entries.clear();
        scanFailed = true;
        return;
    }

    std::ranges::sort(entries, [](const std::unique_ptr<AssetEntry>& a, const std::unique_ptr<AssetEntry>& b) {
        const bool aDir = a->GetType() == AssetEntryType::Directory;
        const bool bDir = b->GetType() == AssetEntryType::Directory;
        if (aDir != bDir) return aDir; // directories before files
        return LowerCopy(a->GetDisplayName()) < LowerCopy(b->GetDisplayName());
    });
}

bool AssetBrowser::ImportExternalFile(const std::filesystem::path& sourceAbsolutePath) {
    if (currentDirectory.empty()) return false;

    std::error_code existsEc;
    if (!fs::exists(sourceAbsolutePath, existsEc) || existsEc) {
        lastOperationError = "Dropped item no longer exists: " + sourceAbsolutePath.string();
        spdlog::warn("Asset browser: {}", lastOperationError);
        return false;
    }

    std::error_code isDirEc;
    const bool sourceIsDirectory = fs::is_directory(sourceAbsolutePath, isDirEc);

    fs::path destination = currentDirectory / sourceAbsolutePath.filename();

    // Auto-uniquify on a name collision (matches the existing "(2)", "(3)",
    // ... convention rather than prompting to overwrite, since importing
    // happens from an asynchronous OS drop callback with no good place to
    // block for a confirmation dialog).
    if (sourceIsDirectory)
        for (int suffix = 2; fs::exists(destination); ++suffix)
            destination = currentDirectory / (sourceAbsolutePath.filename().string() + " (" + std::to_string(suffix) + ")");
    else {
        const std::string stem = sourceAbsolutePath.stem().string();
        const std::string ext = sourceAbsolutePath.extension().string();
        for (int suffix = 2; fs::exists(destination); ++suffix)
            destination = currentDirectory / (stem + " (" + std::to_string(suffix) + ")" + ext);
    }

    std::error_code copyEc;
    if (sourceIsDirectory) {
        // Directory drop: copy the whole subtree, preserving its internal
        // structure, rather than leaving this unimplemented - a plain
        // recursive fs::copy is enough to do this safely.
        fs::copy(sourceAbsolutePath, destination, fs::copy_options::recursive, copyEc);
    }
    else fs::copy_file(sourceAbsolutePath, destination, fs::copy_options::none, copyEc);


    if (copyEc) {
        lastOperationError = "Failed to import \"" + sourceAbsolutePath.filename().string() + "\": " + copyEc.message();
        spdlog::error("Asset browser: {}", lastOperationError);
        return false;
    }

    spdlog::info("Asset browser: imported {} -> {}", sourceAbsolutePath.string(), destination.string());
    lastOperationError.clear();
    selectedFile = destination;
    Refresh();
    return true;
}

bool AssetBrowser::IsScreenPointInside(const float screenX, const float screenY) const {
    return screenX >= lastWindowScreenMinX && screenX <= lastWindowScreenMaxX &&
           screenY >= lastWindowScreenMinY && screenY <= lastWindowScreenMaxY;
}

void AssetBrowser::SetExternalDragHovering(const bool isHovering) {
    externalDragHovering = isHovering;
}

void AssetBrowser::DrawBreadcrumbs() {
    const bool atRoot = (currentDirectory == rootDirectory);

    ImGui::BeginDisabled(atRoot);
    if (ImGui::Button("^ Up"))
        NavigateToParent();
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 10.0f);

    const std::string rootLabel = rootDirectory.filename().empty() ? std::string("Assets") : rootDirectory.filename().string();

    if (ImGui::SmallButton(rootLabel.c_str())) NavigateTo(rootDirectory);

    const fs::path relative = currentDirectory.lexically_relative(rootDirectory);

    if (relative != ".") {
        fs::path accumulated = rootDirectory;
        int segmentIndex = 0;

        for (const auto& part : relative) {
            accumulated /= part;

            ImGui::PushID(segmentIndex++);
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextUnformatted("/");
            ImGui::SameLine(0.0f, 2.0f);

            if (ImGui::SmallButton(part.string().c_str())) NavigateTo(accumulated);

            ImGui::PopID();
        }
    }
}

void AssetBrowser::DrawSearchBar() {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
    ImGui::InputTextWithHint("##AssetBrowserSearch", "Search this folder...", searchBuffer, sizeof(searchBuffer));

    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::SmallButton("x")) searchBuffer[0] = '\0';
}

void AssetBrowser::DrawEntryTile(AssetEntry& entry, const float tileSize, const ThumbnailProvider& thumbnailProvider) {
    const bool isDirectory = entry.GetType() == AssetEntryType::Directory;
    const bool isSelected = !selectedFile.empty() && selectedFile == entry.GetPath();
    const float labelHeight = ImGui::GetTextLineHeight() + 6.0f;

    ImGui::BeginGroup();

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::Selectable(
        "##EntryTile",
        isSelected,
        ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(tileSize, tileSize + labelHeight)
    );

    if (clicked) {
        selectedFile = entry.GetPath();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) entry.OnDoubleClick(*this);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        selectedFile = entry.GetPath(); // right-click selects the target before its context menu opens

    if (!isDirectory && entry.GetAssetKind() != AssetKind::Other && ImGui::BeginDragDropSource()) {
        const std::string payloadPath = entry.GetPath().string();

        ImGui::SetDragDropPayload(
            DragDropPayloadTypeFor(entry.GetAssetKind()),
            payloadPath.c_str(),
            payloadPath.size() + 1
        );

        ImGui::TextUnformatted(entry.GetDisplayName().c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem()) {
        entry.DrawContextMenu(*this);
        ImGui::EndPopup();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 boxMax = {topLeft.x + tileSize, topLeft.y + tileSize};

    ImTextureID texture{};

    if (!isDirectory && thumbnailProvider)
        if (const std::optional<std::string> reference = entry.GetThumbnailReference()) texture = thumbnailProvider(*reference);

    if (texture != ImTextureID{}) drawList->AddImage(texture, topLeft, boxMax);
    else if (isDirectory) {
        drawList->AddRectFilled(topLeft, boxMax, IM_COL32(90, 78, 45, 255), 4.0f);
        drawList->AddRect(topLeft, boxMax, IM_COL32(150, 130, 80, 255), 4.0f);

        constexpr const char* folderLabel = "[dir]";
        const ImVec2 folderLabelSize = ImGui::CalcTextSize(folderLabel);
        drawList->AddText(
            ImVec2(topLeft.x + (tileSize - folderLabelSize.x) * 0.5f, topLeft.y + (tileSize - folderLabelSize.y) * 0.5f),
            IM_COL32(230, 220, 190, 255),
            folderLabel
        );
    }
    else {
        drawList->AddRectFilled(topLeft, boxMax, EntryTileTint(entry), 4.0f);
        drawList->AddRect(topLeft, boxMax, IM_COL32(150, 150, 150, 255), 4.0f);

        const char* tag = EntryVisualTag(entry);
        const ImVec2 tagSize = ImGui::CalcTextSize(tag);
        drawList->AddText(
            {topLeft.x + (tileSize - tagSize.x) * 0.5f, topLeft.y + (tileSize - tagSize.y) * 0.5f},
            IM_COL32(220, 220, 220, 255),
            tag
        );
    }

    if (isSelected)
        drawList->AddRect(topLeft, boxMax, IM_COL32(90, 170, 250, 255), 4.0f, 0, 2.5f);

    const std::string name = TruncateToWidth(entry.GetDisplayName(), tileSize);
    const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());

    drawList->AddText(
        {topLeft.x + (tileSize - nameSize.x) * 0.5f, topLeft.y + tileSize + 4.0f},
        IM_COL32(220, 220, 220, 255),
        name.c_str()
    );

    ImGui::EndGroup();
}

void AssetBrowser::DrawEmptySpaceContextMenu() {
    // ImGuiPopupFlags_NoOpenOverItems is what keeps this from firing when
    // the right-click actually landed on a tile (which opens its own
    // BeginPopupContextItem instead).
    if (ImGui::BeginPopupContextWindow("AssetBrowserEmptySpaceContext",
                                        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Folder")) RequestCreateFolder(currentDirectory);
        if (ImGui::MenuItem("Create Level")) RequestCreateLevel(currentDirectory);
        if (ImGui::BeginMenu("Create File")) {
            DrawCreateFileSubmenuItems(currentDirectory);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Refresh")) Refresh();
        ImGui::EndPopup();
    }
}

void AssetBrowser::DrawEntries(const ThumbnailProvider& thumbnailProvider) {
    if (scanFailed) {
        ImGui::TextDisabled("%s", "This asset folder could not be read.");
        DrawEmptySpaceContextMenu();
        return;
    }

    const std::string searchLower = LowerCopy(searchBuffer);
    const bool filtering = !searchLower.empty();

    constexpr float tileSize = 84.0f;

    const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();
    const float rightEdge = gridOrigin.x + ImGui::GetContentRegionAvail().x;

    bool anyVisible = false;
    bool isFirstInRow = true;

    for (const std::unique_ptr<AssetEntry>& entry : entries) {
        if (filtering && LowerCopy(entry->GetDisplayName()).find(searchLower) == std::string::npos) continue;

        anyVisible = true;

        if (!isFirstInRow) ImGui::SameLine();

        ImGui::PushID(entry->GetPath().string().c_str());
        DrawEntryTile(*entry, tileSize, thumbnailProvider);
        ImGui::PopID();

        const float lastTileRight = ImGui::GetItemRectMax().x;
        const float nextTileRight = lastTileRight + ImGui::GetStyle().ItemSpacing.x + tileSize;

        isFirstInRow = (nextTileRight > rightEdge);
    }

    if (entries.empty()) ImGui::TextDisabled("%s", "This folder is empty.");
    else if (!anyVisible) ImGui::TextDisabled("%s", "No items match your search.");

    // Clicking truly empty space (not any tile, not a popup) clears the
    // selection.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        selectedFile.clear();

    DrawEmptySpaceContextMenu();
}

AssetBrowserModalAction AssetBrowser::DrawNameEntryModalBody(const std::string& description) {
    ImGui::TextWrapped("%s", description.c_str());
    ImGui::Spacing();

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

    ImGui::SetNextItemWidth(280.0f);
    const bool enterPressed = ImGui::InputText(
        "##AssetBrowserModalName", activeModal.nameBuffer, sizeof(activeModal.nameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );

    if (!activeModal.errorMessage.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", activeModal.errorMessage.c_str());

    ImGui::Spacing();

    AssetBrowserModalAction action = AssetBrowserModalAction::None;
    if (ImGui::Button("Create") || enterPressed) action = AssetBrowserModalAction::Confirmed;
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) action = AssetBrowserModalAction::Cancelled;

    return action;
}

void AssetBrowser::DrawCreateFolderModal() {
    constexpr const char* kPopupId = "Create Folder##AssetBrowserModal";

    if (activeModal.kind == AssetBrowserModalKind::CreateFolder && activeModal.justOpened) {
        ImGui::OpenPopup(kPopupId);
        activeModal.justOpened = false;
    }

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (activeModal.kind == AssetBrowserModalKind::CreateFolder) {
        const std::string description = "New folder in \"" + DisplayRelative(activeModal.destinationDirectory) + "\":";
        const AssetBrowserModalAction action = DrawNameEntryModalBody(description);

        if (action == AssetBrowserModalAction::Confirmed) {
            fs::path createdPath;
            std::string error;
            if (TryCreateFolder(activeModal.destinationDirectory, activeModal.nameBuffer, createdPath, error)) {
                const bool wasVisible = createdPath.parent_path() == currentDirectory;
                activeModal.kind = AssetBrowserModalKind::None;
                ImGui::CloseCurrentPopup();
                Refresh();
                if (wasVisible) selectedFile = createdPath;
            }
            else activeModal.errorMessage = error;
        }
        else if (action == AssetBrowserModalAction::Cancelled) {
            activeModal.kind = AssetBrowserModalKind::None;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawCreateLevelModal() {
    constexpr const char* kPopupId = "Create Level##AssetBrowserModal";

    if (activeModal.kind == AssetBrowserModalKind::CreateLevel && activeModal.justOpened) {
        ImGui::OpenPopup(kPopupId);
        activeModal.justOpened = false;
    }

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (activeModal.kind == AssetBrowserModalKind::CreateLevel) {
        const std::string description = "New level in \"" + DisplayRelative(activeModal.destinationDirectory) +
                                         "\" (" + std::string(kLevelFileExtension) + " is added automatically):";
        const AssetBrowserModalAction action = DrawNameEntryModalBody(description);

        if (action == AssetBrowserModalAction::Confirmed) {
            fs::path createdLevelPath;
            std::string error;
            if (CreateLevelAsset(activeModal.destinationDirectory, activeModal.nameBuffer, createdLevelPath, error)) {
                const bool wasVisible = createdLevelPath.parent_path() == currentDirectory;
                activeModal.kind = AssetBrowserModalKind::None;
                ImGui::CloseCurrentPopup();
                Refresh();
                if (wasVisible) selectedFile = createdLevelPath;
            }
            else {
                activeModal.errorMessage = error;
            }
        }
        else if (action == AssetBrowserModalAction::Cancelled) {
            activeModal.kind = AssetBrowserModalKind::None;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawCreateFileModal() {
    constexpr const char* kPopupId = "Create File##AssetBrowserModal";

    if (activeModal.kind == AssetBrowserModalKind::CreateFile && activeModal.justOpened) {
        ImGui::OpenPopup(kPopupId);
        activeModal.justOpened = false;
    }

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    //TILKY(TODO) translation json
    if (activeModal.kind == AssetBrowserModalKind::CreateFile) {
        const std::string description = activeModal.forcedExtension.empty()
            ? ("New file in \"" + DisplayRelative(activeModal.destinationDirectory) + "\" (include an extension):")
            : ("New " + activeModal.forcedExtension + " file in \"" + DisplayRelative(activeModal.destinationDirectory) + "\":");

        const AssetBrowserModalAction action = DrawNameEntryModalBody(description);

        if (action == AssetBrowserModalAction::Confirmed) {
            std::string enteredName = activeModal.nameBuffer;

            if (!activeModal.forcedExtension.empty() && !fs::path(enteredName).has_extension())
                enteredName += activeModal.forcedExtension;

            fs::path createdFilePath;
            std::string error;
            if (CreateGenericFileAsset(activeModal.destinationDirectory, enteredName, createdFilePath, error)) {
                const bool wasVisible = createdFilePath.parent_path() == currentDirectory;
                activeModal.kind = AssetBrowserModalKind::None;
                ImGui::CloseCurrentPopup();
                Refresh();
                if (wasVisible) selectedFile = createdFilePath;
            }
            else activeModal.errorMessage = error;
        }
        else if (action == AssetBrowserModalAction::Cancelled) {
            activeModal.kind = AssetBrowserModalKind::None;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawRenameModal() {
    constexpr const char* kPopupId = "Rename##AssetBrowserModal";

    if (activeModal.kind == AssetBrowserModalKind::Rename && activeModal.justOpened) {
        ImGui::OpenPopup(kPopupId);
        activeModal.justOpened = false;
    }

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (activeModal.kind == AssetBrowserModalKind::Rename) {
        ImGui::Text("Renaming \"%s\"", activeModal.targetPath.filename().string().c_str());
        ImGui::Spacing();

        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

        ImGui::SetNextItemWidth(280.0f);
        const bool enterPressed = ImGui::InputText(
            "##RenameNewName", activeModal.nameBuffer, sizeof(activeModal.nameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

        if (!activeModal.errorMessage.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", activeModal.errorMessage.c_str());

        ImGui::Spacing();
        const bool renamePressed = ImGui::Button("Rename") || enterPressed;
        ImGui::SameLine();
        const bool cancelPressed = ImGui::Button("Cancel");

        if (renamePressed) {
            fs::path newPath;
            std::string error;
            if (TryRenameEntry(activeModal.targetPath, activeModal.targetIsDirectory, activeModal.nameBuffer, newPath, error)) {
                const fs::path oldPath = activeModal.targetPath;

                activeModal.kind = AssetBrowserModalKind::None;
                ImGui::CloseCurrentPopup();

                // Update any cached paths this browser owns before refreshing.
                if (selectedFile == oldPath) selectedFile = newPath;
                if (pendingConfirmedPath.has_value() && *pendingConfirmedPath == oldPath) pendingConfirmedPath = newPath;

                Refresh();
            }
            else activeModal.errorMessage = error;
        }
        else if (cancelPressed) {
            activeModal.kind = AssetBrowserModalKind::None;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void AssetBrowser::DrawDeleteConfirmModal() {
    constexpr const char* kPopupId = "Confirm Delete##AssetBrowserModal";

    if (activeModal.kind == AssetBrowserModalKind::DeleteConfirm && activeModal.justOpened) {
        ImGui::OpenPopup(kPopupId);
        activeModal.justOpened = false;
    }

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (activeModal.kind == AssetBrowserModalKind::DeleteConfirm) {
        const std::string relative = DisplayRelative(activeModal.targetPath);

        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f), "This cannot be undone.");
        ImGui::TextWrapped("Delete \"%s\"?", relative.c_str());

        std::error_code emptyEc;
        if (activeModal.targetIsDirectory && !fs::is_empty(activeModal.targetPath, emptyEc) && !emptyEc)
            ImGui::TextWrapped("This folder is not empty - everything inside it will be deleted too.");

        if (!activeModal.errorMessage.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", activeModal.errorMessage.c_str());

        ImGui::Spacing();
        const bool deletePressed = ImGui::Button("Delete");
        ImGui::SameLine();
        const bool cancelPressed = ImGui::Button("Cancel");

        if (deletePressed) {
            std::string error;
            if (TryDeleteEntry(activeModal.targetPath, activeModal.targetIsDirectory, error)) {
                const fs::path deletedPath = activeModal.targetPath;

                activeModal.kind = AssetBrowserModalKind::None;
                ImGui::CloseCurrentPopup();

                ClearSelectionUnder(deletedPath);
                if (pendingConfirmedPath.has_value()) {
                    const fs::path pendingRel = pendingConfirmedPath->lexically_relative(deletedPath);
                    if (*pendingConfirmedPath == deletedPath || (!pendingRel.empty() && *pendingRel.begin() != ".."))
                        pendingConfirmedPath.reset();
                }

                Refresh();
            }
            else activeModal.errorMessage = error;
        }
        else if (cancelPressed) {
            activeModal.kind = AssetBrowserModalKind::None;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void AssetBrowser::HandleKeyboardShortcuts() {
    if (!ImGui::IsWindowFocused()) return;
    if (ImGui::GetIO().WantTextInput) return;
    if (activeModal.kind != AssetBrowserModalKind::None) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        NavigateToParent();
        return;
    }

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R)) {
        Refresh();
        return;
    }

    if (selectedFile.empty()) return;

    AssetEntry* selectedEntry = FindEntry(selectedFile);
    if (selectedEntry == nullptr) return; // stale selection (e.g. deleted externally) - nothing to act on

    if (ImGui::IsKeyPressed(ImGuiKey_F2))
        RequestRename(selectedEntry->GetPath(), selectedEntry->GetType() == AssetEntryType::Directory);
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        RequestDelete(selectedEntry->GetPath(), selectedEntry->GetType() == AssetEntryType::Directory);
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
        selectedEntry->OnDoubleClick(*this);
}

void AssetBrowser::Draw(const ThumbnailProvider& thumbnailProvider) {
    if (rootDirectory.empty()) return; // SetRootDirectory() hasn't been called yet

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    lastWindowScreenMinX = windowPos.x;
    lastWindowScreenMinY = windowPos.y;
    lastWindowScreenMaxX = windowPos.x + windowSize.x;
    lastWindowScreenMaxY = windowPos.y + windowSize.y;

    const Vector2 externalDragPosition = InputManager::GetExternalDragPosition();

    SetExternalDragHovering(
        InputManager::IsExternalDragActive() &&
        IsScreenPointInside(externalDragPosition.x, externalDragPosition.y)
    );

    for (const InputManager::DroppedFile &droppedFile: InputManager::GetDroppedFiles()) {
        if (!IsScreenPointInside(droppedFile.position.x, droppedFile.position.y)) continue;
        ImportExternalFile(droppedFile.path);
    }

    if (!lastOperationError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", lastOperationError.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss##AssetBrowserError")) lastOperationError.clear();
        ImGui::Spacing();
    }

    DrawBreadcrumbs();
    ImGui::Spacing();
    DrawSearchBar();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawEntries(thumbnailProvider);

    DrawCreateFolderModal();
    DrawCreateLevelModal();
    DrawCreateFileModal();
    DrawRenameModal();
    DrawDeleteConfirmModal();

    HandleKeyboardShortcuts();

    if (externalDragHovering) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect(
            ImVec2(lastWindowScreenMinX, lastWindowScreenMinY),
            ImVec2(lastWindowScreenMaxX, lastWindowScreenMaxY),
            IM_COL32(90, 170, 250, 255), 0.0f, 0, 3.0f
        );
    }

    if (pendingNavigation.has_value()) {
        const fs::path destination = *pendingNavigation;
        pendingNavigation.reset();
        NavigateTo(destination);
    }
}

bool AssetBrowser::HasSelection() const {
    return !selectedFile.empty();
}

const std::filesystem::path& AssetBrowser::GetSelectedFile() const {
    return selectedFile;
}

bool AssetBrowser::ConsumePendingConfirmedSelection(const AssetKind expectedKind, std::filesystem::path& outAbsolutePath) {
    if (!pendingConfirmedPath.has_value()) return false;
    if (pendingConfirmedKind != expectedKind) return false; // wrong kind - leave it queued for a matching field

    outAbsolutePath = *pendingConfirmedPath;
    pendingConfirmedPath.reset();
    return true;
}

void AssetBrowser::RequestNavigate(const std::filesystem::path& absoluteDirectory) {
    pendingNavigation = absoluteDirectory;
}

void AssetBrowser::RequestRename(const std::filesystem::path& targetAbsolutePath, const bool targetIsDirectory) {
    if (activeModal.kind != AssetBrowserModalKind::None) return;
    if (targetAbsolutePath == rootDirectory) return; // never rename the asset root

    activeModal = AssetBrowserModalState{};
    activeModal.kind = AssetBrowserModalKind::Rename;
    activeModal.targetPath = targetAbsolutePath;
    activeModal.targetIsDirectory = targetIsDirectory;

    CopyIntoNameBuffer(activeModal.nameBuffer, sizeof(activeModal.nameBuffer), targetAbsolutePath.filename().string());

    activeModal.justOpened = true;
}

void AssetBrowser::RequestDelete(const std::filesystem::path& targetAbsolutePath, const bool targetIsDirectory) {
    if (activeModal.kind != AssetBrowserModalKind::None) return;
    if (targetAbsolutePath == rootDirectory) return; // never delete the asset root

    activeModal = AssetBrowserModalState{};
    activeModal.kind = AssetBrowserModalKind::DeleteConfirm;
    activeModal.targetPath = targetAbsolutePath;
    activeModal.targetIsDirectory = targetIsDirectory;
    activeModal.justOpened = true;
}

void AssetBrowser::RequestCreateFolder(const std::filesystem::path& destinationDirectory) {
    if (activeModal.kind != AssetBrowserModalKind::None) return;

    activeModal = AssetBrowserModalState{};
    activeModal.kind = AssetBrowserModalKind::CreateFolder;
    activeModal.destinationDirectory = destinationDirectory;
    activeModal.justOpened = true;
}

void AssetBrowser::RequestCreateLevel(const std::filesystem::path& destinationDirectory) {
    if (activeModal.kind != AssetBrowserModalKind::None) return;

    activeModal = AssetBrowserModalState{};
    activeModal.kind = AssetBrowserModalKind::CreateLevel;
    activeModal.destinationDirectory = destinationDirectory;
    activeModal.justOpened = true;
}

void AssetBrowser::RequestCreateFile(const std::filesystem::path& destinationDirectory, const std::string& forcedExtension) {
    if (activeModal.kind != AssetBrowserModalKind::None) return;

    activeModal = AssetBrowserModalState{};
    activeModal.kind = AssetBrowserModalKind::CreateFile;
    activeModal.destinationDirectory = destinationDirectory;
    activeModal.forcedExtension = forcedExtension;
    activeModal.justOpened = true;
}

void AssetBrowser::RequestOpenLevel(const std::filesystem::path& absolutePath) {
    if (!IsPathWithinRoot(absolutePath)) {
        lastOperationError = "Refused to open a level outside the asset root: " + absolutePath.string();
        spdlog::warn("Asset browser: {}", lastOperationError);
        return;
    }

    std::string errorMessage;
    if (!OpenLevelAsset(absolutePath, errorMessage)) {
        lastOperationError = errorMessage;
        spdlog::error("Asset browser: {}", lastOperationError);
        return;
    }

    lastOperationError.clear();
}

void AssetBrowser::RequestConsumeAsFieldReference(const AssetKind kind, const std::filesystem::path& absolutePath) {
    pendingConfirmedPath = absolutePath;
    pendingConfirmedKind = kind;
}

void AssetBrowser::DrawCreateFileSubmenuItems(const std::filesystem::path &destinationDirectory) {
    if (ImGui::MenuItem("Script (.lua)")) RequestCreateFile(destinationDirectory, ".lua");
    if (ImGui::MenuItem("Custom...")) RequestCreateFile(destinationDirectory, "");
}
