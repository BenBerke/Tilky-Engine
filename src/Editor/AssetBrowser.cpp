#include "Headers/Editor/AssetBrowser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "External/tracy/profiler/src/profiler/IconsFontAwesome6.h"

#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "EditorInternal.hpp" // SaveUserSettings()
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/FlipbookAsset.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Objects/UILayouts.hpp"
#include "Headers/Project/AssetSets.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/TagRegistry.hpp"

namespace fs = std::filesystem;

namespace {
    // Breaks `text` into lines of at most `width` characters at spaces, so
    // a long doc string doesn't stretch a tooltip across the screen.
    std::string WrapText(const std::string& text, const std::size_t width = 80) {
        std::string wrapped;
        std::size_t lineLength = 0;

        for (std::size_t start = 0; start < text.size();) {
            std::size_t end = text.find(' ', start);
            if (end == std::string::npos) end = text.size();
            const std::size_t wordLength = end - start;

            if (lineLength > 0 && lineLength + 1 + wordLength > width) {
                wrapped += '\n';
                lineLength = 0;
            }
            else if (lineLength > 0) {
                wrapped += ' ';
                ++lineLength;
            }

            wrapped.append(text, start, wordLength);
            lineLength += wordLength;
            start = end + 1;
        }

        return wrapped;
    }

    // Lua highlighting plus Tilky's declaration keywords (`public`) as
    // keywords, and as known identifiers (hover tooltip + their own color)
    // its field types plus every callback, script global, script function and type
    // registered with LuaBindingMetadata.
    const TextEditor::LanguageDefinition& ScriptLanguageDefinition() {
        LevelSystem::EnsureScriptingInitialized();

        static const TextEditor::LanguageDefinition definition = [] {
            TextEditor::LanguageDefinition lua = TextEditor::LanguageDefinition::Lua();
            // Coloring comes from ScriptIntellisense (see RequestOpenScript),
            // so the regexes would only cost time to compile.
            lua.mTokenRegexStrings.clear();

            for (const std::string& keyword : LuaScriptCompiler::DeclarationKeywordNames())
                lua.mKeywords.insert(keyword);

            const auto addIdentifier = [&](const std::string& name, const std::string& declaration) {
                TextEditor::Identifier identifier;
                identifier.mDeclaration = declaration;
                lua.mIdentifiers.insert_or_assign(name, identifier);
            };

            const std::vector<std::string> fieldTypes = LuaScriptCompiler::FieldTypeNames();
            for (const std::string& type : fieldTypes) addIdentifier(type, "Field type");

            for (const LuaBindingMetadata::TypeDoc& type : LuaBindingMetadata::AllTypes()) {
                std::string declaration = WrapText(type.doc.empty() ? "Engine type." : type.doc);
                if (std::ranges::find(fieldTypes, type.name) != fieldTypes.end()) declaration = "Field type\n" + declaration;
                addIdentifier(type.name, declaration);
            }

            for (const LuaBindingMetadata::PropertyDoc& global : LuaBindingMetadata::AllScriptGlobals())
                addIdentifier(global.name, global.name + ": " + global.luaType + "\n" + WrapText(global.doc));

            for (const LuaBindingMetadata::MethodDoc& function : LuaBindingMetadata::AllScriptFunctions())
                addIdentifier(function.name, LuaBindingMetadata::FunctionSignature(function) + "\n" + WrapText(function.doc));

            for (const LuaBindingMetadata::CallbackDoc& callback : LuaBindingMetadata::AllCallbacks()) {
                addIdentifier(callback.name, LuaBindingMetadata::CallbackSignature(callback) + "\n" +
                    WrapText(LuaBindingMetadata::CallbackOwnerNote(callback.owner) + callback.doc));
            }

            return lua;
        }();

        return definition;
    }

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

    // Drag-and-drop payload type for moving an entry within this browser
    // itself (dropped onto a folder tile, the "^ Up" button, or a
    // breadcrumb segment - see AssetBrowser::DrawMoveDropTarget()). Kept
    // separate from AssetBrowser::DragDropPayloadTypeFor(), the public
    // per-AssetKind type field widgets elsewhere accept. Outside this file
    // only the Levels list reads it, through AssetBrowser::MoveEntryPayloadType().
    // A Texture/Sound/Script file is dragged
    // with its usual field-reference payload instead of this one (an entry
    // never offers both at once - see DrawEntryTile) and still moves
    // correctly, because DrawMoveDropTarget() also accepts each of those
    // payload types as a move.
    constexpr const char* kMoveEntryPayloadType = "TILKY_MOVE_ENTRY";

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
        // The file, or a level inside the folder, may be one being auto saved.
        Editor::WaitForAutoSave();

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

        // A level renamed to the name of a level in another folder would make
        // that name ambiguous - see LevelSerialization::FindLevelPath.
        if (!targetIsDirectory && destination.extension() == AssetBrowser::kLevelFileExtension) {
            for (const fs::path& existing : LevelSerialization::FindLevelFiles(destination.stem().string())) {
                std::error_code equivEc;
                if (!fs::equivalent(existing, targetPath, equivEc) || equivEc) {
                    errorMessage = "A level with that name already exists.";
                    return false;
                }
            }
        }

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
        // The file, or a level inside the folder, may be one being auto saved.
        Editor::WaitForAutoSave();

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

    // --- Move (drag-and-drop within the browser) ----------------------------

    // Moves `sourcePath` into `destinationDirectory`, keeping its filename.
    // Used exclusively for drags that start and end inside this browser -
    // see AssetBrowser::HandleDroppedMove(). ImportExternalFile() above
    // handles the separate case of an OS-level drop from outside the
    // project, and uses the same auto-uniquify convention on a name
    // collision.
    bool TryMoveEntry(const fs::path& sourcePath, const bool sourceIsDirectory,
                       const fs::path& destinationDirectory, fs::path& newPath, std::string& errorMessage) {
        // The file, or a level inside the folder, may be one being auto saved.
        Editor::WaitForAutoSave();

        std::error_code existsEc;
        if (!fs::exists(sourcePath, existsEc)) {
            errorMessage = "This item no longer exists.";
            return false;
        }

        std::error_code destExistsEc;
        if (!fs::exists(destinationDirectory, destExistsEc) || !fs::is_directory(destinationDirectory, destExistsEc)) {
            errorMessage = "Destination folder no longer exists.";
            return false;
        }

        // Dropped back onto the folder that already contains it - a
        // harmless no-op rather than an error, since this is exactly what
        // happens for any drag that starts and ends without actually
        // crossing onto a different folder.
        std::error_code sameParentEc;
        if (fs::equivalent(sourcePath.parent_path(), destinationDirectory, sameParentEc) && !sameParentEc) {
            newPath = sourcePath;
            return true;
        }

        if (sourceIsDirectory) {
            // A folder can never be moved into itself...
            std::error_code selfEc;
            if (fs::equivalent(sourcePath, destinationDirectory, selfEc) && !selfEc) {
                errorMessage = "Can't move a folder into itself.";
                return false;
            }

            // ...or into one of its own subfolders - fs::rename would
            // either fail with a confusing OS error or, on some platforms,
            // corrupt the tree, so this is checked explicitly up front.
            std::error_code sourceCanonEc;
            const fs::path canonicalSource = fs::weakly_canonical(sourcePath, sourceCanonEc);
            if (!sourceCanonEc) {
                std::error_code destCanonEc;
                const fs::path canonicalDestination = fs::weakly_canonical(destinationDirectory, destCanonEc);
                if (!destCanonEc) {
                    const fs::path rel = canonicalDestination.lexically_relative(canonicalSource);
                    if (!rel.empty() && *rel.begin() != "..") {
                        errorMessage = "Can't move a folder into one of its own subfolders.";
                        return false;
                    }
                }
            }
        }

        fs::path destination = destinationDirectory / sourcePath.filename();

        // Same auto-uniquify convention as ImportExternalFile: append
        // " (2)", " (3)", ... on a name collision rather than overwriting
        // or blocking the move behind a confirmation dialog.
        if (sourceIsDirectory)
            for (int suffix = 2; fs::exists(destination); ++suffix)
                destination = destinationDirectory / (sourcePath.filename().string() + " (" + std::to_string(suffix) + ")");
        else {
            const std::string stem = sourcePath.stem().string();
            const std::string ext = sourcePath.extension().string();
            for (int suffix = 2; fs::exists(destination); ++suffix)
                destination = destinationDirectory / (stem + " (" + std::to_string(suffix) + ")" + ext);
        }

        std::error_code renameEc;
        fs::rename(sourcePath, destination, renameEc);
        if (renameEc) {
            errorMessage = "Could not move: " + renameEc.message();
            return false;
        }

        newPath = destination;
        return true;
    }

    // --- Copy --------------------------------------------------------------

    // Where a copy of `source` goes in `directory`: its own name, or " (2)",
    // " (3)", ... when that's taken. Level names must be free across the whole
    // Assets folder, not just here.
    fs::path FreeCopyPath(const fs::path& directory, const fs::path& source, const bool sourceIsDirectory) {
        fs::path destination = directory / source.filename();

        if (sourceIsDirectory) {
            for (int suffix = 2; fs::exists(destination); ++suffix)
                destination = directory / (source.filename().string() + " (" + std::to_string(suffix) + ")");

            return destination;
        }

        const std::string stem = source.stem().string();
        const std::string ext = source.extension().string();
        const bool isLevel = ext == AssetBrowser::kLevelFileExtension;

        const auto taken = [&] {
            return fs::exists(destination) || (isLevel && !LevelSerialization::FindLevelFiles(destination.stem().string()).empty());
        };

        for (int suffix = 2; taken(); ++suffix)
            destination = directory / (stem + " (" + std::to_string(suffix) + ")" + ext);

        return destination;
    }

    // Copies `sourcePath` into `destinationDirectory`. A copied UI layout gets
    // an ID of its own from UILayouts::Rescan().
    bool TryCopyEntry(const fs::path& sourcePath, const bool sourceIsDirectory,
                       const fs::path& destinationDirectory, fs::path& newPath, std::string& errorMessage) {
        // The file, or a level inside the folder, may be one being auto saved.
        Editor::WaitForAutoSave();

        std::error_code ec;
        if (!fs::exists(sourcePath, ec)) {
            errorMessage = "This item no longer exists.";
            return false;
        }

        if (!fs::is_directory(destinationDirectory, ec)) {
            errorMessage = "Destination folder no longer exists.";
            return false;
        }

        if (sourceIsDirectory) {
            const fs::path rel = fs::weakly_canonical(destinationDirectory, ec).lexically_relative(fs::weakly_canonical(sourcePath, ec));
            if (!rel.empty() && *rel.begin() != "..") {
                errorMessage = "Can't paste a folder into itself.";
                return false;
            }
        }

        const fs::path destination = FreeCopyPath(destinationDirectory, sourcePath, sourceIsDirectory);

        ec.clear();
        if (sourceIsDirectory) fs::copy(sourcePath, destination, fs::copy_options::recursive, ec);
        else fs::copy_file(sourcePath, destination, fs::copy_options::none, ec);

        if (ec) {
            errorMessage = "Could not copy: " + ec.message();
            std::error_code ignored;
            fs::remove_all(destination, ignored);
            return false;
        }

        // Levels in a copied folder would share their names with the originals.
        if (sourceIsDirectory) {
            std::vector<fs::path> copiedLevels;
            for (fs::recursive_directory_iterator it(destination, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file() && it->path().extension() == AssetBrowser::kLevelFileExtension)
                    copiedLevels.push_back(it->path());

            for (const fs::path& level : copiedLevels) {
                if (LevelSerialization::FindLevelFiles(level.stem().string()).size() < 2) continue;

                std::error_code renameEc;
                fs::rename(level, FreeCopyPath(level.parent_path(), level, false), renameEc);
                if (renameEc) spdlog::error("Asset browser: could not rename copied level {}: {}", level.string(), renameEc.message());
            }
        }

        newPath = destination;
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
            if (LowerCopy(tail) == LowerCopy(ext)) stem.resize(stem.size() - ext.size());
        }

        return fs::path(stem + ext);
    }

    [[nodiscard]] bool CreateLevelAsset(
        const fs::path& destinationDirectory,
        const std::string& levelName,
        fs::path& createdLevelPath,
        std::string& errorMessage
    ) {
        (void)createdLevelPath;

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

        // Levels are found by name anywhere under Assets, so the name must be
        // free project-wide, not just in this folder.
        std::error_code existsEc;
        if (fs::exists(destination, existsEc) || !LevelSerialization::FindLevelFiles(destination.stem().string()).empty()) {
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
                R"lua(-- `public` fields show up (and become editable) in the Inspector.
-- The word after `public` is the field's type - see the Tilky scripting
-- docs for the full list (number, int, bool, string, Vector2/3/4, Entity,
-- Behaviour, an engine component name like Rigidbody, enum(...), Key, ...).

public number speed = 200
public Entity target = nil

-- Called once, the first time this script becomes active.
function Start()

end

-- Called every frame.
function Update()
    -- entity is this script's own Entity - every script gets one
    -- automatically, no lookup required.
    -- entity.transform:AddPosition(Vector3(0, 0, speed * GameTime.deltaTime))

    -- Reading an Entity-reference field gives you a real Entity back,
    -- or nil if nothing is assigned in the Inspector.
    -- if target ~= nil then
    --     print(entity.name .. " is looking at " .. target.name)
    -- end
end

-- Called on a fixed timestep, independent of the frame rate - use this for
-- movement/physics-flavoured logic instead of Update().
function FixedUpdate()

end

-- Called when this script's Entity becomes active/inactive.
-- function OnEnable() end
-- function OnDisable() end

-- Called once when this script or its Entity is destroyed.
function OnDestroy()

end
)lua";
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

        // Through the editor, not straight into LevelManager: the editor
        // has to know which level is open, or its next save writes this
        // level's contents under the previous level's name.
        Editor::OpenLevel(levelPath.stem().string());

        return true;
    }

    // --- Extension registry, backing CreateAssetEntry ------------------------

    enum class RegisteredExtensionKind { Texture, Sound, Script, Model, Flipbook, Font, Prefab, UILayout, Level };

    // Extension -> first-class kind. Matching is case-insensitive (see
    // LowerCopy). Add an entry here (and, if it needs behavior beyond just
    // a different AssetKind tag, a case in CreateAssetEntry below) to
    // register a new extension-driven asset type; anything absent from
    // this map still comes back as a GenericFileEntry(AssetKind::Other)
    // rather than being hidden - this is what lets "unknown extensions
    // must still appear" hold in general.
    //
    // Model extensions come from ModelLoader so the browser, the importer
    // and the exporter always agree on what counts as a model.
    const std::unordered_map<std::string, RegisteredExtensionKind> kExtensionRegistry = [] {
        std::unordered_map<std::string, RegisteredExtensionKind> registry = {
            { ".png",  RegisteredExtensionKind::Texture },
            { ".jpg",  RegisteredExtensionKind::Texture },
            { ".jpeg", RegisteredExtensionKind::Texture },
            { ".wav",  RegisteredExtensionKind::Sound   },
            { ".mp3",  RegisteredExtensionKind::Sound   },
            { ".ogg",  RegisteredExtensionKind::Sound   },
            { ".flac", RegisteredExtensionKind::Sound   },
            { ".lua",  RegisteredExtensionKind::Script  },
            { ".ttf",  RegisteredExtensionKind::Font    },
            { ".otf",  RegisteredExtensionKind::Font    },
            { std::string(FlipbookIO::kExtension), RegisteredExtensionKind::Flipbook },
            { std::string(Prefab::kExtension), RegisteredExtensionKind::Prefab },
            { std::string(UILayouts::kExtension), RegisteredExtensionKind::UILayout },
            { std::string(AssetBrowser::kLevelFileExtension), RegisteredExtensionKind::Level },
        };

        for (const std::string& extension : ModelLoader::SupportedExtensions())
            registry.emplace(extension, RegisteredExtensionKind::Model);

        return registry;
    }();

    // --- Tile visuals, keyed off the entry rather than a bare AssetKind so
    // Level (which has no AssetKind of its own) still gets a distinct look.

    // Models are never rendered into a thumbnail; their tile just names the
    // file type, e.g. ".fbx".
    std::string EntryVisualTag(const AssetEntry& entry) {
        if (entry.GetType() == AssetEntryType::Level) return "lvl";

        switch (entry.GetAssetKind()) {
            case AssetKind::Sound: return LowerCopy(entry.GetPath().extension().string()).substr(1);
            case AssetKind::Script: return "lua";
            case AssetKind::Model: return LowerCopy(entry.GetPath().extension().string());
            case AssetKind::Flipbook: return "fpk";
            case AssetKind::Prefab: return "prefab";
            case AssetKind::UILayout: return "ui layout";
            case AssetKind::Font: return LowerCopy(entry.GetPath().extension().string());
            default: return "file";
        }
    }

    // Font Awesome glyph for a tile without a thumbnail.
    const char* EntryIcon(const AssetEntry& entry) {
        if (entry.GetType() == AssetEntryType::Level) return ICON_FA_MAP;

        switch (entry.GetAssetKind()) {
            case AssetKind::Sound: return ICON_FA_FILE_AUDIO;
            case AssetKind::Script: return ICON_FA_FILE_CODE;
            case AssetKind::Model: return ICON_FA_CUBE;
            case AssetKind::Flipbook: return ICON_FA_FILM;
            case AssetKind::Prefab: return ICON_FA_CUBES;
            case AssetKind::UILayout: return ICON_FA_LAYER_GROUP;
            case AssetKind::Font: return ICON_FA_FONT;
            default: return ICON_FA_FILE;
        }
    }

    ImU32 EntryTileTint(const AssetEntry& entry) {
        if (entry.GetType() == AssetEntryType::Level) return IM_COL32(90, 60, 110, 255);

        switch (entry.GetAssetKind()) {
            case AssetKind::Sound: return IM_COL32(45, 70, 90, 255);
            case AssetKind::Script: return IM_COL32(55, 80, 55, 255);
            case AssetKind::Model: return IM_COL32(95, 70, 45, 255);
            case AssetKind::Flipbook: return IM_COL32(100, 55, 75, 255);
            case AssetKind::Prefab: return IM_COL32(45, 85, 95, 255);
            case AssetKind::UILayout: return IM_COL32(70, 90, 50, 255);
            case AssetKind::Font: return IM_COL32(85, 85, 45, 255);
            default: return IM_COL32(60, 60, 65, 255);
        }
    }
}

// ============================================================================
// Text Editor
// ============================================================================
namespace {
    // How long typing has to pause before the script is checked for errors.
    constexpr double kSyntaxCheckDelay = 0.5;
    // How often the project's files are rescanned for script/level/sound names.
    constexpr double kProjectScanInterval = 3.0;
    constexpr int kAutocompleteRows = 12;

    TextEditor::PaletteIndex PaletteIndexOf(const ScriptIntellisense::ColorRole role) {
        using R = ScriptIntellisense::ColorRole;
        using P = TextEditor::PaletteIndex;
        switch (role) {
            case R::Keyword: return P::Keyword;
            case R::Number: return P::Number;
            case R::String: return P::String;
            case R::Punctuation: return P::Punctuation;
            case R::Identifier: return P::Identifier;
            case R::KnownIdentifier: return P::KnownIdentifier;
            case R::Comment: return P::Comment;
            case R::MultiLineComment: return P::MultiLineComment;
            case R::Local: return P::Local;
            case R::Parameter: return P::Parameter;
            case R::Function: return P::Function;
            case R::Field: return P::Field;
            default: return P::Default;
        }
    }

    // The short label beside a suggestion.
    const char* ItemKindLabel(const ScriptIntellisense::ItemKind kind) {
        using K = ScriptIntellisense::ItemKind;
        switch (kind) {
            case K::Keyword: return "keyword";
            case K::Local: return "local";
            case K::Parameter: return "param";
            case K::Function: return "function";
            case K::Field: return "field";
            case K::Type: return "type";
            case K::Global: return "global";
            case K::EngineFunction: return "function";
            case K::Callback: return "callback";
            case K::Property: return "property";
            case K::Method: return "method";
            case K::EnumValue: return "enum";
            case K::Tag: return "tag";
            case K::Script: return "script";
            case K::Level: return "level";
            case K::Sound: return "sound";
            default: return "";
        }
    }
}

bool AssetBrowser::IsPlainTextFile(const std::filesystem::path& path) {
    const std::string extension = LowerCopy(path.extension().string());
    return extension == ".txt" || extension == ".md" || extension == ".json";
}

void AssetBrowser::RequestOpenScript(const std::filesystem::path& absolutePath) {
    if (!IsPathWithinRoot(absolutePath)) {
        lastOperationError = "Refused to open a file outside the asset root: " + absolutePath.string();
        return;
    }

    const bool isLua = LowerCopy(absolutePath.extension().string()) == ".lua";

    if (!isLua && !IsPlainTextFile(absolutePath)) {
        lastOperationError = "Only Lua scripts and text files (.txt, .md, .json) can be opened.";
        return;
    }

    std::ifstream file(absolutePath, std::ios::binary);
    if (!file.is_open()) {
        lastOperationError =
            "Could not open file: " + absolutePath.string();
        return;
    }

    const std::string contents{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{}
    };

    // One editor serves both kinds, so set the colorizer every time a file
    // is opened: scripts are colored from their ScriptIntellisense analysis
    // (the language definition only supplies the hover tooltips), plain text
    // not at all.
    if (isLua) {
        scriptEditor.SetLanguageDefinition(ScriptLanguageDefinition());
        scriptEditor.SetExternalColorizer([this] { CurrentScriptAnalysis(); });
    } else {
        scriptEditor.SetLanguageDefinition(TextEditor::LanguageDefinition());
        scriptEditor.SetExternalColorizer({});
    }
    scriptEditor.SetColorizerEnable(isLua);

    openFileIsLua = isLua;
    openScriptPath = absolutePath;
    scriptAnalysisValid = false;

    scriptEditor.SetText(contents);
    scriptEditor.SetErrorMarkers({});

    scriptEditorOpen = true;
    scriptEditorDirty = false;
    autocompleteActive = false;
    autocompleteManual = false;
    autocompleteSuppressed = false;
    signatureHelpOpen = false;
    syntaxErrors.clear();
    syntaxScheduledVersion = scriptEditor.GetTextVersion();
    syntaxCheckDueTime = isLua ? 0.0 : -1.0; // check a script right away
    lastOperationError.clear();
}

void AssetBrowser::SaveOpenScript() {
    if (openScriptPath.empty()) return;

    std::ofstream file(openScriptPath, std::ios::binary | std::ios::trunc);

    if (!file.is_open()) {
        lastOperationError = "Could not save file: " + openScriptPath.string();
        return;
    }

    file << scriptEditor.GetText();

    if (!file.good()) {
        lastOperationError = "Failed while writing file: " + openScriptPath.string();
        return;
    }

    scriptEditorDirty = false;
    lastOperationError.clear();
}

void AssetBrowser::DrawTextEditorWindow(ImFont* scriptEditorFont) {
    if (!scriptEditorOpen) return;

    std::string title = openScriptPath.filename().string();

    if (scriptEditorDirty) title += " *";

    // "###" (not "##") pins the window's ID to the fixed suffix alone.
    // With plain "##", ImGui still hashes the text before it into the ID -
    // so toggling the " *" dirty marker (on the very first edit, and again
    // on every save) made this look like a BRAND NEW window each time,
    // resetting it to its ImGuiCond_FirstUseEver size/position and
    // stealing focus. That's what made the editor appear to randomly
    // resize itself and lose focus while typing/saving.
    title += "###TilkyScriptEditor";

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

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Toggle Line Comment", "Ctrl+/", false, openFileIsLua)) ToggleLineComment();
            if (ImGui::MenuItem("Duplicate Line", "Ctrl+D")) DuplicateCurrentLine();

            ImGui::EndMenu();
        }

        DrawColorsMenu();

        ImGui::EndMenuBar();
    }

    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGuiIO& io = ImGui::GetIO();

    // While the autocomplete popup is up, Tab/Enter/Up/Down/Escape are
    // reserved for accepting/navigating/dismissing the suggestion list
    // instead of their usual editor meaning (indent/newline/move cursor).
    // Disabling the editor's own keyboard handling for just this frame,
    // only when one of those keys is actually pressed, keeps every other
    // key (typed characters, Backspace, Ctrl+Z, ...) flowing through to
    // the editor as normal while the popup is open.
    bool autocompleteAccept = false;
    bool autocompleteDismiss = false;
    int autocompleteNavDelta = 0;
    bool autocompleteClickPending = false;

    // Ctrl+Space opens the suggestion list, Ctrl+Shift+Space the parameter
    // hints. The space itself must not reach the editor.
    bool manualSuggestions = false;
    bool manualHints = false;

    if (openFileIsLua && windowFocused && io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) (io.KeyShift ? manualHints : manualSuggestions) = true;
        // The space can arrive a frame apart from the key press.
        for (int i = io.InputQueueCharacters.Size - 1; i >= 0; --i)
            if (io.InputQueueCharacters[i] == ' ') io.InputQueueCharacters.erase(io.InputQueueCharacters.Data + i);
    }

    // Escape closes the suggestion list first, then the parameter hints.
    if (!autocompleteActive && signatureHelpOpen && windowFocused && ImGui::IsKeyPressed(ImGuiKey_Escape)) signatureHelpOpen = false;

    if (autocompleteActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) autocompleteDismiss = true;
        else if (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) autocompleteAccept = true;
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) autocompleteNavDelta = 1;
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) autocompleteNavDelta = -1;

        if (autocompleteAccept || autocompleteDismiss || autocompleteNavDelta != 0)
            scriptEditor.SetHandleKeyboardInputs(false);
    }

    // Same idea for the mouse: a click landing inside where the popup was
    // last drawn (see DrawAutocompletePopup(), which sets
    // autocompletePopupMin/Max) must not ALSO be treated by the editor as
    // "click to move the cursor here" - TextEditor::HandleMouseInputs() has
    // no awareness of the popup at all (it's drawn on the foreground draw
    // list, not a window, precisely so it can't fight the editor for
    // keyboard focus/Z-order - see DrawAutocompletePopup()'s comment), so
    // without this it would reposition the cursor to wherever the popup
    // visually sits and then AcceptAutocomplete() would insert the
    // suggestion at that wrong location instead of the word being typed.
    // Using LAST frame's rect is a one-frame-stale approximation (the
    // popup's exact position for THIS frame isn't known until after
    // Render(), since it follows the caret) - close enough in practice,
    // since the popup barely moves between two consecutive frames.
    // The mouse wheel over the suggestion list scrolls the list. Owning the
    // wheel keeps ImGui from also scrolling the code behind it; ImGui reads
    // the owner at the start of the next frame and then drops it, so it has
    // to be taken again every frame the mouse is over the list.
    const ImGuiID wheelOwner = ImGui::GetID("##AutocompleteWheel");

    if (autocompleteActive) {
        const ImVec2 mousePos = io.MousePos;
        const bool mouseInsidePopup =
            mousePos.x >= autocompletePopupMin.x && mousePos.x <= autocompletePopupMax.x &&
            mousePos.y >= autocompletePopupMin.y && mousePos.y <= autocompletePopupMax.y;

        if (mouseInsidePopup) {
            scriptEditor.SetHandleMouseInputs(false);

            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, wheelOwner);
            if (io.MouseWheel != 0.0f) {
                const int rowsPerNotch = 3;
                const int delta = static_cast<int>(std::lround(-io.MouseWheel * static_cast<float>(rowsPerNotch)));
                autocompleteFirstVisible += delta != 0 ? delta : (io.MouseWheel > 0.0f ? -1 : 1);
            }

            // Also keep this frame from re-deriving the suggestion list
            // below (see the UpdateAutocomplete() call further down): the
            // swallow above is only a one-frame-stale approximation, so a
            // click can still slip through and let the editor move the
            // text cursor to wherever the popup happens to be drawn over.
            // Re-deriving from that unexpectedly-moved cursor could
            // deactivate the popup before DrawAutocompletePopup() ever
            // gets a chance to hit-test the click itself, silently
            // swallowing it instead of accepting the clicked suggestion.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) autocompleteClickPending = true;
        }
    }

    // Auto-close bracket/quote pairs, part 1: "type over" an existing
    // matching closer instead of inserting a duplicate one right next to
    // it. Checked (and, if it applies, swallowed) BEFORE Render() sees the
    // keystroke, since fixing it up afterward would leave a real duplicate
    // character sitting in the undo history for a moment.
    // `typedChars` is a copy of this frame's raw input, taken before
    // Render() consumes and clears the real queue - part 2 (auto-inserting
    // a closer for a freshly-typed opener) reads it again after Render().
    const std::vector<ImWchar> typedChars(io.InputQueueCharacters.begin(), io.InputQueueCharacters.end());
    bool autoCloseHandled = false;

    // Bracket/quote pairing is for code only: in prose an apostrophe
    // ("don't") would get a stray closing quote.
    if (openFileIsLua && windowFocused && typedChars.size() == 1 && !scriptEditor.IsReadOnly() && !scriptEditor.HasSelection()) {
        static const std::unordered_set<ImWchar> kClosers = {')', ']', '}', '"', '\''};

        if (kClosers.contains(typedChars[0])) {
            const TextEditor::Coordinates cursor = scriptEditor.GetCursorPosition();
            const std::string line = scriptEditor.GetCurrentLineText();
            const int col = std::clamp(cursor.mColumn, 0, static_cast<int>(line.size()));

            if (col < static_cast<int>(line.size()) && line[col] == static_cast<char>(typedChars[0])) {
                io.InputQueueCharacters.resize(0); // don't let Render() also insert this keystroke
                scriptEditor.MoveRight(1, false, false);
                autoCloseHandled = true;
            }
        }
    }

    // Ctrl+Backspace / Ctrl+Delete: delete a whole word instead of one
    // character. The editor's own keyboard handling only fires Backspace/
    // Delete when Ctrl is NOT held, so this can't double up with it.
    // MoveLeft/MoveRight's word-boundary mode is the same logic Ctrl+Left/
    // Ctrl+Right already use to jump by word.
    bool deleteWordLeft = false;
    bool deleteWordRight = false;

    if (windowFocused && !scriptEditor.IsReadOnly() && io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
        deleteWordLeft = ImGui::IsKeyPressed(ImGuiKey_Backspace);
        deleteWordRight = ImGui::IsKeyPressed(ImGuiKey_Delete);

        if (ImGui::IsKeyPressed(ImGuiKey_Slash)) { if (openFileIsLua) ToggleLineComment(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateCurrentLine();
    }

    if (scriptEditorFont != nullptr) [[likely]] ImGui::PushFont(scriptEditorFont);

    const float statusBarHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 editorSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - statusBarHeight);

    scriptEditor.Render("##LuaCodeEditor", editorSize, false);
    if (scriptEditorFont != nullptr) [[likely]] ImGui::PopFont();

    scriptEditor.SetHandleKeyboardInputs(true);
    scriptEditor.SetHandleMouseInputs(true);

    if (scriptEditor.IsTextChanged()) scriptEditorDirty = true;

    // Auto-close bracket/quote pairs, part 2: a freshly-typed opener gets
    // its closer inserted right after it, with the cursor left sitting
    // between the pair - unless part 1 above already fully handled this
    // exact keystroke as a type-over (relevant for quotes, which are their
    // own opener AND closer).
    if (openFileIsLua && !autoCloseHandled && windowFocused && typedChars.size() == 1 && !scriptEditor.IsReadOnly()) {
        static const std::unordered_map<ImWchar, char> kOpenToClose = {
            {'(', ')'}, {'[', ']'}, {'{', '}'}, {'"', '"'}, {'\'', '\''},
        };

        if (const auto it = kOpenToClose.find(typedChars[0]); it != kOpenToClose.end()) {
            const TextEditor::Coordinates cursor = scriptEditor.GetCursorPosition();
            const std::string line = scriptEditor.GetCurrentLineText();
            const int col = std::clamp(cursor.mColumn, 0, static_cast<int>(line.size()));
            const char nextChar = col < static_cast<int>(line.size()) ? line[col] : '\0';

            // Don't auto-close a quote immediately before an identifier
            // character - most likely an apostrophe/quote inside existing
            // text rather than the start of a new string literal.
            const bool isQuote = typedChars[0] == '"' || typedChars[0] == '\'';
            const bool nextIsWordChar = std::isalnum(static_cast<unsigned char>(nextChar)) != 0 || nextChar == '_';

            if (!(isQuote && nextIsWordChar)) {
                scriptEditor.InsertText(std::string(1, it->second));
                scriptEditor.MoveLeft(1, false, false);
                scriptEditorDirty = true;
            }
        }
    }

    if (deleteWordLeft) {
        // Backspace() is private on TextEditor; Delete() is public and,
        // like Backspace(), just removes whatever's currently selected -
        // equivalent once MoveLeft has already made the word the selection.
        if (!scriptEditor.HasSelection()) scriptEditor.MoveLeft(1, true, true);
        scriptEditor.Delete();
        scriptEditorDirty = true;
    } else if (deleteWordRight) {
        if (!scriptEditor.HasSelection()) scriptEditor.MoveRight(1, true, true);
        scriptEditor.Delete();
        scriptEditorDirty = true;
    }

    if (autocompleteNavDelta != 0 && !autocompleteItems.empty()) {
        const int count = static_cast<int>(autocompleteItems.size());
        autocompleteSelectedIndex = (autocompleteSelectedIndex + autocompleteNavDelta + count) % count;
        autocompleteScrollToSelection = true;
    }

    // Typing or moving the cursor ends an Escape dismissal.
    if (scriptEditor.IsTextChanged() || scriptEditor.IsCursorPositionChanged()) autocompleteSuppressed = false;

    if (autocompleteDismiss) {
        autocompleteActive = false;
        autocompleteManual = false;
        autocompleteSuppressed = true;
    } else if (autocompleteAccept && !autocompleteItems.empty()) {
        AcceptAutocomplete(autocompleteSelectedIndex);
    } else if (manualSuggestions) {
        autocompleteSuppressed = false;
        UpdateAutocomplete(true);
        autocompleteManual = autocompleteActive;
    } else if (openFileIsLua && autocompleteNavDelta == 0 && !autocompleteClickPending &&
               ((!autocompleteActive && !autocompleteSuppressed) || scriptEditor.IsTextChanged() || scriptEditor.IsCursorPositionChanged())) {
        // Only re-derive suggestions from the buffer when something that
        // should actually invalidate the current list happened: the text
        // changed, the cursor moved for a reason other than interacting
        // with the popup (see autocompleteClickPending above), or the
        // popup isn't showing yet. UpdateAutocomplete() always resets
        // autocompleteSelectedIndex back to 0 and rebuilds
        // autocompleteMatches from scratch - calling it on every frame
        // that merely wasn't itself a nav keypress (which is most frames:
        // holding an arrow key between OS key-repeat pulses, the mouse
        // sitting still over a suggestion, ...) undid the selection change
        // applied two lines above almost as soon as it happened, which is
        // why arrow-key navigation looked like it did nothing.
        UpdateAutocomplete(autocompleteManual);
        if (!autocompleteActive) autocompleteManual = false;
    }

    // Parameter hints open on a typed '(' or ',' (or Ctrl+Shift+Space) and
    // follow the cursor until it leaves the call.
    if (openFileIsLua) {
        const bool typedOpener = windowFocused && std::ranges::any_of(typedChars, [](const ImWchar c) { return c == '(' || c == ','; });
        if (manualHints || typedOpener || signatureHelpOpen) {
            UpdateSignatureHelp();
            signatureHelpOpen = signatureHelp.has_value();
        }
    } else {
        signatureHelpOpen = false;
    }

    if (autocompleteActive) DrawAutocompletePopup();
    if (signatureHelpOpen) DrawSignatureHelp();

    // Syntax check once typing has paused for a moment.
    if (openFileIsLua) {
        if (scriptEditor.GetTextVersion() != syntaxScheduledVersion) {
            syntaxScheduledVersion = scriptEditor.GetTextVersion();
            syntaxCheckDueTime = ImGui::GetTime() + kSyntaxCheckDelay;
        }
        if (syntaxCheckDueTime >= 0.0 && ImGui::GetTime() >= syntaxCheckDueTime) {
            syntaxCheckDueTime = -1.0;
            CheckScriptSyntax();
        }
    }

    if (windowFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveRequested = true;

    if (saveRequested) SaveOpenScript();

    ImGui::Separator();

    const TextEditor::Coordinates statusCursor = scriptEditor.GetCursorPosition();
    ImGui::TextDisabled(
        "Ln %d, Col %d  |  %d lines  |  %s",
        statusCursor.mLine + 1,
        statusCursor.mColumn + 1,
        scriptEditor.GetTotalLines(),
        scriptEditorDirty ? "unsaved changes" : "saved"
    );

    if (openFileIsLua && !syntaxErrors.empty()) {
        ImGui::SameLine();
        char error[512];
        std::snprintf(error, sizeof(error), Localisation::Get("editor.script_editor.error_status").c_str(),
                      syntaxErrors.front().line, syntaxErrors.front().message.c_str());
        std::string text = error;
        if (syntaxErrors.size() > 1) {
            std::snprintf(error, sizeof(error), Localisation::Get("editor.script_editor.error_more").c_str(), static_cast<int>(syntaxErrors.size()) - 1);
            text += " ";
            text += error;
        }
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "|  %s", text.c_str());
    }

    ImGui::End();
}

std::span<const AssetBrowser::ScriptEditorColorInfo> AssetBrowser::ScriptEditorColors() {
    using PI = TextEditor::PaletteIndex;

    // Only the slots Lua and plain text actually use (no preprocessor or
    // breakpoints).
    static constexpr ScriptEditorColorInfo kColors[] = {
        {PI::Default, "default", "editor.script_editor.color.default"},
        {PI::Keyword, "keyword", "editor.script_editor.color.keyword"},
        {PI::Number, "number", "editor.script_editor.color.number"},
        {PI::String, "string", "editor.script_editor.color.string"},
        {PI::Punctuation, "punctuation", "editor.script_editor.color.punctuation"},
        {PI::Identifier, "identifier", "editor.script_editor.color.identifier"},
        {PI::KnownIdentifier, "knownIdentifier", "editor.script_editor.color.known_identifier"},
        {PI::Comment, "comment", "editor.script_editor.color.comment"},
        {PI::MultiLineComment, "multiLineComment", "editor.script_editor.color.multi_line_comment"},
        {PI::Background, "background", "editor.script_editor.color.background"},
        {PI::Cursor, "cursor", "editor.script_editor.color.cursor"},
        {PI::Selection, "selection", "editor.script_editor.color.selection"},
        {PI::LineNumber, "lineNumber", "editor.script_editor.color.line_number"},
        {PI::CurrentLineFill, "currentLineFill", "editor.script_editor.color.current_line_fill"},
        {PI::CurrentLineFillInactive, "currentLineFillInactive", "editor.script_editor.color.current_line_fill_inactive"},
        {PI::CurrentLineEdge, "currentLineEdge", "editor.script_editor.color.current_line_edge"},
        {PI::ErrorMarker, "errorMarker", "editor.script_editor.color.error_marker"},
        {PI::Local, "local", "editor.script_editor.color.local"},
        {PI::Parameter, "parameter", "editor.script_editor.color.parameter"},
        {PI::Function, "function", "editor.script_editor.color.function"},
        {PI::Field, "field", "editor.script_editor.color.field"},
    };

    return kColors;
}

void AssetBrowser::DrawColorsMenu() {
    if (!ImGui::BeginMenu(Localisation::Get("editor.script_editor.colors").c_str())) {
        if (scriptEditorColorsChanged) {
            scriptEditorColorsChanged = false;
            MapEditorInternal::SaveUserSettings();
        }
        return;
    }

    constexpr ImGuiColorEditFlags colorFlags =
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_PickerHueBar |
            ImGuiColorEditFlags_AlphaBar |
            ImGuiColorEditFlags_AlphaPreviewHalf;

    const TextEditor::Palette& defaults = TextEditor::GetDarkPalette();
    TextEditor::Palette palette = scriptEditor.GetPalette();
    bool changed = false;

    if (ImGui::BeginTable("##ScriptEditorColors", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const ScriptEditorColorInfo& info : ScriptEditorColors()) {
            ImU32& color = palette[static_cast<std::size_t>(info.index)];

            ImGui::PushID(info.saveName);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Localisation::Get(info.labelKey).c_str());

            ImGui::TableSetColumnIndex(1);
            ImVec4 edited = ImGui::ColorConvertU32ToFloat4(color);
            if (ImGui::ColorEdit4("##Color", &edited.x, colorFlags)) {
                color = ImGui::ColorConvertFloat4ToU32(edited);
                changed = true;
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::BeginDisabled(color == defaults[static_cast<std::size_t>(info.index)]);
            if (ImGui::SmallButton(Localisation::Get("editor.user_settings.default").c_str())) {
                color = defaults[static_cast<std::size_t>(info.index)];
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(Localisation::Get("editor.script_editor.colors.reset_all").c_str(), nullptr, false, palette != defaults)) {
        palette = defaults;
        changed = true;
    }

    if (changed) {
        scriptEditor.SetPalette(palette);
        scriptEditorColorsChanged = true;
    }

    ImGui::EndMenu();
}

const ScriptIntellisense::Analysis& AssetBrowser::CurrentScriptAnalysis() {
    const unsigned version = scriptEditor.GetTextVersion();
    if (scriptAnalysisValid && version == analyzedTextVersion) return scriptAnalysis;

    LevelSystem::EnsureScriptingInitialized();

    scriptAnalysis = ScriptIntellisense::Analyze(scriptEditor.GetText(), ScriptContext());
    analyzedTextVersion = version;
    scriptAnalysisValid = true;

    scriptLineStarts.assign(1, 0);
    for (int i = 0; i < static_cast<int>(scriptAnalysis.text.size()); ++i)
        if (scriptAnalysis.text[i] == '\n') scriptLineStarts.push_back(i + 1);

    std::vector<TextEditor::PaletteIndex> colors(scriptAnalysis.colors.size());
    std::ranges::transform(scriptAnalysis.colors, colors.begin(), PaletteIndexOf);
    scriptEditor.SetColors(colors);

    return scriptAnalysis;
}

const ScriptIntellisense::Context& AssetBrowser::ScriptContext() {
    if (!scriptContextReady) {
        scriptContext.findScript = [this](const std::string& name) { return FindOtherScript(name); };
        scriptContext.projectNames = [this](const ScriptIntellisense::StringKind kind) { return ProjectNames(kind); };
        scriptContextReady = true;
    }
    return scriptContext;
}

int AssetBrowser::ScriptCursorOffset() {
    const ScriptIntellisense::Analysis& analysis = CurrentScriptAnalysis();
    const TextEditor::Coordinates cursor = scriptEditor.GetCursorPosition();
    if (cursor.mLine < 0 || cursor.mLine >= static_cast<int>(scriptLineStarts.size())) return static_cast<int>(analysis.text.size());

    const int index = std::max(0, scriptEditor.GetCharacterIndexOf(cursor));
    return std::min(scriptLineStarts[cursor.mLine] + index, static_cast<int>(analysis.text.size()));
}

TextEditor::Coordinates AssetBrowser::ScriptCoordinatesOf(const int offset) {
    CurrentScriptAnalysis();
    const auto it = std::ranges::upper_bound(scriptLineStarts, offset);
    const int line = std::max(0, static_cast<int>(it - scriptLineStarts.begin()) - 1);
    return scriptEditor.CoordinatesOfCharacter(line, offset - scriptLineStarts[line]);
}

void AssetBrowser::RescanProjectFiles() {
    const double now = ImGui::GetTime();
    if (projectFilesScanTime >= 0.0 && now - projectFilesScanTime < kProjectScanInterval) return;
    projectFilesScanTime = now;

    projectScriptPaths.clear();
    projectLevelNames.clear();
    projectSoundNames.clear();

    const fs::path assets = ProjectManager::GetAssetsPath();
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(assets, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back(it->path());
    std::ranges::sort(files);

    for (const fs::path& file : files) {
        const std::string extension = LowerCopy(file.extension().string());
        if (extension == ".lua") projectScriptPaths.emplace(file.stem().string(), file);
        else if (extension == kLevelFileExtension) projectLevelNames.push_back(file.stem().string());
        else {
            const std::string reference = RelativeOrFallback(file, assets).generic_string();
            if (AssetSets::IsSoundFile(reference)) projectSoundNames.push_back(reference);
        }
    }
}

std::vector<std::string> AssetBrowser::ProjectNames(const ScriptIntellisense::StringKind kind) {
    using K = ScriptIntellisense::StringKind;
    std::vector<std::string> names;

    if (kind == K::Tag) {
        for (const auto& [name, id] : TagRegistry::GetAllOfKind(TagRegistry::TagKind::NORMAL)) names.push_back(name);
        return names;
    }

    RescanProjectFiles();
    switch (kind) {
        case K::Script:
            for (const auto& [name, path] : projectScriptPaths) names.push_back(name);
            break;
        case K::Level: names = projectLevelNames; break;
        case K::Sound: names = projectSoundNames; break;
        default: break;
    }
    return names;
}

const ScriptIntellisense::Analysis* AssetBrowser::FindOtherScript(const std::string& scriptName) {
    RescanProjectFiles();

    const auto pathIt = projectScriptPaths.find(scriptName);
    if (pathIt == projectScriptPaths.end()) return nullptr;
    const fs::path& path = pathIt->second;

    // The open script itself: its live text, not what was last saved.
    if (path == openScriptPath && scriptAnalysisValid) return &scriptAnalysis;

    std::error_code ec;
    const fs::file_time_type writeTime = fs::last_write_time(path, ec);
    if (ec) return nullptr;

    const std::string key = path.generic_string();
    if (const auto cached = otherScripts.find(key); cached != otherScripts.end() && cached->second.writeTime == writeTime)
        return &cached->second.analysis;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return nullptr;
    std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

    OtherScript& entry = otherScripts[key];
    entry.writeTime = writeTime;
    entry.analysis = ScriptIntellisense::Analyze(std::move(text));
    return &entry.analysis;
}

void AssetBrowser::UpdateAutocomplete(const bool manual) {
    if (!scriptEditorOpen || scriptEditor.IsReadOnly() ||
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        autocompleteActive = false;
        return;
    }

    const ScriptIntellisense::Analysis& analysis = CurrentScriptAnalysis();
    ScriptIntellisense::Completion completion = ScriptIntellisense::Complete(analysis, ScriptCursorOffset(), manual, ScriptContext());

    constexpr std::size_t kMaxItems = 200;
    if (completion.items.size() > kMaxItems) completion.items.resize(kMaxItems);

    autocompleteItems = std::move(completion.items);
    autocompleteReplaceBegin = completion.replaceBegin;
    autocompleteSelectedIndex = 0;
    autocompleteFirstVisible = 0;
    autocompleteActive = !autocompleteItems.empty();
}

void AssetBrowser::DrawAutocompletePopup() {
    const int count = static_cast<int>(autocompleteItems.size());
    const int rows = std::min(count, kAutocompleteRows);

    // Keep the selection in view after the arrow keys moved it (not after
    // the mouse wheel scrolled the list away from it).
    autocompleteSelectedIndex = std::clamp(autocompleteSelectedIndex, 0, count - 1);
    if (autocompleteScrollToSelection) {
        if (autocompleteSelectedIndex < autocompleteFirstVisible) autocompleteFirstVisible = autocompleteSelectedIndex;
        if (autocompleteSelectedIndex >= autocompleteFirstVisible + rows) autocompleteFirstVisible = autocompleteSelectedIndex - rows + 1;
        autocompleteScrollToSelection = false;
    }
    autocompleteFirstVisible = std::clamp(autocompleteFirstVisible, 0, std::max(0, count - rows));

    const ImGuiStyle& style = ImGui::GetStyle();
    const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 padding = style.WindowPadding;

    float widest = 0.0f;
    for (int i = autocompleteFirstVisible; i < autocompleteFirstVisible + rows; ++i) {
        const ScriptIntellisense::CompletionItem& item = autocompleteItems[i];
        widest = std::max(widest, ImGui::CalcTextSize(item.text.c_str()).x + ImGui::CalcTextSize(ItemKindLabel(item.kind)).x);
    }
    const float popupWidth = std::clamp(widest + padding.x * 2.0f + 24.0f, 240.0f, 480.0f);
    const float popupHeight = itemHeight * static_cast<float>(rows) + padding.y * 2.0f;

    // Clamp against the host editor window AND the main viewport, so the
    // popup can never render below/right of the IDE window or spill off
    // the screen. Called from within the editor window's Begin()/End()
    // (see DrawTextEditorWindow()), so GetWindowPos()/GetWindowSize() here
    // still refer to that host window.
    const ImVec2 hostWindowMin = ImGui::GetWindowPos();
    const ImVec2 hostWindowSize = ImGui::GetWindowSize();
    ImGuiViewport* viewport = ImGui::GetWindowViewport();

    const ImVec2 clampMin = viewport->WorkPos;
    const ImVec2 clampMax(
        std::min(hostWindowMin.x + hostWindowSize.x, viewport->WorkPos.x + viewport->WorkSize.x),
        std::min(hostWindowMin.y + hostWindowSize.y, viewport->WorkPos.y + viewport->WorkSize.y)
    );

    // GetCursorScreenPos() already returns a point just below the caret's
    // line; flip to just above it instead when there isn't enough room
    // below, mirroring how a normal IDE's suggestion list behaves near the
    // bottom of the window.
    ImVec2 anchor = scriptEditor.GetCursorScreenPos();

    if (anchor.y + popupHeight > clampMax.y && anchor.y - itemHeight - popupHeight >= clampMin.y)
        anchor.y -= itemHeight + popupHeight;

    const ImVec2 pos(
        std::clamp(anchor.x, clampMin.x, std::max(clampMin.x, clampMax.x - popupWidth)),
        std::clamp(anchor.y, clampMin.y, std::max(clampMin.y, clampMax.y - popupHeight))
    );

    autocompletePopupMin = pos;
    autocompletePopupMax = ImVec2(pos.x + popupWidth, pos.y + popupHeight);

    // Drawn on the foreground draw list rather than as a separate ImGui
    // window - deliberately. A real window here competes with the editor
    // for Dear ImGui's keyboard focus and Z-order the moment it's clicked
    // (ImGui::Selectable() calls FocusWindow() unconditionally), which
    // stole keyboard focus from the editor and could leave the popup
    // behind it. The foreground draw list belongs to no window and always
    // composites last. Clicks are hit-tested manually below;
    // DrawTextEditorWindow() uses autocompletePopupMin/Max to stop the
    // editor's own click-to-move-cursor handling from also firing.
    ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

    drawList->AddRectFilled(autocompletePopupMin, autocompletePopupMax, ImGui::GetColorU32(ImGuiCol_PopupBg), style.PopupRounding);
    drawList->AddRect(autocompletePopupMin, autocompletePopupMax, ImGui::GetColorU32(ImGuiCol_Border), style.PopupRounding);

    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 kindColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 highlightColor = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const bool mouseClickedThisFrame = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    drawList->PushClipRect(autocompletePopupMin, autocompletePopupMax, true);

    int clickedIndex = -1;

    for (int row = 0; row < rows; ++row) {
        const int i = autocompleteFirstVisible + row;
        const ImVec2 itemMin(autocompletePopupMin.x, autocompletePopupMin.y + padding.y + itemHeight * static_cast<float>(row));
        const ImVec2 itemMax(autocompletePopupMax.x, itemMin.y + itemHeight);

        const bool hovered = mousePos.x >= itemMin.x && mousePos.x < itemMax.x &&
                              mousePos.y >= itemMin.y && mousePos.y < itemMax.y;

        // Hovering also moves the selection (as any dropdown does).
        if (hovered) {
            autocompleteSelectedIndex = i;
            if (mouseClickedThisFrame) clickedIndex = i;
        }

        if (i == autocompleteSelectedIndex) drawList->AddRectFilled(itemMin, itemMax, highlightColor);

        const float textY = itemMin.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f;
        drawList->AddText(ImVec2(itemMin.x + padding.x, textY), textColor, autocompleteItems[i].text.c_str());

        const char* kind = ItemKindLabel(autocompleteItems[i].kind);
        drawList->AddText(ImVec2(itemMax.x - padding.x - ImGui::CalcTextSize(kind).x, textY), kindColor, kind);
    }

    drawList->PopClipRect();

    // A scroll bar when not everything fits.
    if (count > rows) {
        const float trackHeight = popupHeight - padding.y * 2.0f;
        const float thumbHeight = std::max(8.0f, trackHeight * static_cast<float>(rows) / static_cast<float>(count));
        const float thumbY = autocompletePopupMin.y + padding.y +
                             (trackHeight - thumbHeight) * static_cast<float>(autocompleteFirstVisible) / static_cast<float>(count - rows);
        drawList->AddRectFilled(ImVec2(autocompletePopupMax.x - 4.0f, thumbY), ImVec2(autocompletePopupMax.x - 1.0f, thumbY + thumbHeight),
                                ImGui::GetColorU32(ImGuiCol_ScrollbarGrab), 2.0f);
    }

    // The selected suggestion's signature and docs, beside the list.
    const ScriptIntellisense::CompletionItem& selected = autocompleteItems[autocompleteSelectedIndex];
    if (!selected.detail.empty() || !selected.doc.empty()) {
        constexpr float docsWidth = 360.0f;
        const float wrap = docsWidth - padding.x * 2.0f;
        const ImVec2 detailSize = selected.detail.empty() ? ImVec2() : ImGui::CalcTextSize(selected.detail.c_str(), nullptr, false, wrap);
        const ImVec2 docSize = selected.doc.empty() ? ImVec2() : ImGui::CalcTextSize(selected.doc.c_str(), nullptr, false, wrap);
        const float gap = !selected.detail.empty() && !selected.doc.empty() ? style.ItemSpacing.y : 0.0f;
        const float docsHeight = detailSize.y + gap + docSize.y + padding.y * 2.0f;

        float x = autocompletePopupMax.x + 4.0f;
        if (x + docsWidth > clampMax.x) x = autocompletePopupMin.x - 4.0f - docsWidth;
        x = std::max(x, clampMin.x);
        const float y = std::clamp(autocompletePopupMin.y, clampMin.y, std::max(clampMin.y, clampMax.y - docsHeight));

        const ImVec2 docsMin(x, y);
        const ImVec2 docsMax(x + docsWidth, y + docsHeight);
        drawList->AddRectFilled(docsMin, docsMax, ImGui::GetColorU32(ImGuiCol_PopupBg), style.PopupRounding);
        drawList->AddRect(docsMin, docsMax, ImGui::GetColorU32(ImGuiCol_Border), style.PopupRounding);

        ImVec2 textPos(docsMin.x + padding.x, docsMin.y + padding.y);
        if (!selected.detail.empty()) {
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, textColor, selected.detail.c_str(), nullptr, wrap);
            textPos.y += detailSize.y + gap;
        }
        if (!selected.doc.empty())
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, kindColor, selected.doc.c_str(), nullptr, wrap);
    }

    // Deferred until after drawing: AcceptAutocomplete() changes the text
    // and the list this loop reads.
    if (clickedIndex >= 0) AcceptAutocomplete(clickedIndex);
}

void AssetBrowser::AcceptAutocomplete(const int index) {
    if (index < 0 || index >= static_cast<int>(autocompleteItems.size())) return;

    const std::string text = autocompleteItems[index].text;

    // Replace what was typed of the word (matching is case-insensitive, so
    // the typed part may not share the suggestion's casing). Guarded on a
    // non-empty range: TextEditor::Delete() with an empty selection would
    // eat the character after the cursor instead.
    const TextEditor::Coordinates cursor = scriptEditor.GetCursorPosition();
    const TextEditor::Coordinates start = ScriptCoordinatesOf(autocompleteReplaceBegin);
    if (start < cursor) {
        scriptEditor.SetSelection(start, cursor);
        scriptEditor.Delete();
    }

    scriptEditor.InsertText(text);

    scriptEditorDirty = true;
    autocompleteActive = false;
    autocompleteManual = false;
    // Stay closed until the next keystroke or cursor move: rebuilding the
    // list for the word just accepted would reopen it straight away.
    autocompleteSuppressed = true;
}

void AssetBrowser::UpdateSignatureHelp() {
    if (!scriptEditorOpen || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        signatureHelp.reset();
        return;
    }
    signatureHelp = ScriptIntellisense::GetSignatureHelp(CurrentScriptAnalysis(), ScriptCursorOffset(), ScriptContext());
}

void AssetBrowser::DrawSignatureHelp() {
    if (!signatureHelp) return;
    const ScriptIntellisense::SignatureHelp& help = *signatureHelp;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 padding = style.WindowPadding;
    const float lineHeight = ImGui::GetTextLineHeight();
    constexpr float maxWidth = 560.0f;
    const float wrap = maxWidth - padding.x * 2.0f;

    const ImVec2 labelSize = ImGui::CalcTextSize(help.label.c_str());
    const ImVec2 docSize = help.doc.empty() ? ImVec2() : ImGui::CalcTextSize(help.doc.c_str(), nullptr, false, wrap);
    const float width = std::min(maxWidth, std::max(labelSize.x, docSize.x) + padding.x * 2.0f);
    const float height = labelSize.y + (help.doc.empty() ? 0.0f : style.ItemSpacing.y + docSize.y) + padding.y * 2.0f;

    const ImVec2 hostMin = ImGui::GetWindowPos();
    const ImVec2 hostSize = ImGui::GetWindowSize();
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const ImVec2 clampMin(std::max(hostMin.x, viewport->WorkPos.x), std::max(hostMin.y, viewport->WorkPos.y));
    const ImVec2 clampMax(std::min(hostMin.x + hostSize.x, viewport->WorkPos.x + viewport->WorkSize.x),
                          std::min(hostMin.y + hostSize.y, viewport->WorkPos.y + viewport->WorkSize.y));

    // Above the caret's line, so it never covers the suggestion list below
    // it; below everything when there's no room above.
    const ImVec2 caret = scriptEditor.GetCursorScreenPos(); // just below the caret's line
    float y = caret.y - lineHeight - height - 2.0f;
    if (y < clampMin.y) y = (autocompleteActive ? std::max(caret.y, autocompletePopupMax.y) : caret.y) + 2.0f;

    const ImVec2 boxMin(std::clamp(caret.x - padding.x, clampMin.x, std::max(clampMin.x, clampMax.x - width)), y);
    const ImVec2 boxMax(boxMin.x + width, boxMin.y + height);

    ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
    drawList->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_PopupBg), style.PopupRounding);
    drawList->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border), style.PopupRounding);
    drawList->PushClipRect(boxMin, boxMax, true);

    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 activeColor = scriptEditor.GetPalette()[static_cast<std::size_t>(TextEditor::PaletteIndex::Parameter)] | IM_COL32_A_MASK;

    // The label in pieces: the active parameter in its own color, underlined.
    int activeBegin = -1, activeEnd = -1;
    if (help.activeParam >= 0 && help.activeParam < static_cast<int>(help.paramRanges.size()))
        std::tie(activeBegin, activeEnd) = help.paramRanges[help.activeParam];

    const char* label = help.label.c_str();
    ImVec2 pos(boxMin.x + padding.x, boxMin.y + padding.y);
    const auto drawPiece = [&](const int begin, const int end, const ImU32 color, const bool underline) {
        if (end <= begin) return;
        drawList->AddText(pos, color, label + begin, label + end);
        const float pieceWidth = ImGui::CalcTextSize(label + begin, label + end).x;
        if (underline) drawList->AddLine(ImVec2(pos.x, pos.y + lineHeight), ImVec2(pos.x + pieceWidth, pos.y + lineHeight), color);
        pos.x += pieceWidth;
    };

    const int labelLength = static_cast<int>(help.label.size());
    if (activeBegin < 0) {
        drawPiece(0, labelLength, textColor, false);
    } else {
        drawPiece(0, activeBegin, textColor, false);
        drawPiece(activeBegin, activeEnd, activeColor, true);
        drawPiece(activeEnd, labelLength, textColor, false);
    }

    if (!help.doc.empty()) {
        const ImVec2 docPos(boxMin.x + padding.x, boxMin.y + padding.y + labelSize.y + style.ItemSpacing.y);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), docPos, dimColor, help.doc.c_str(), nullptr, wrap);
    }

    drawList->PopClipRect();
}

void AssetBrowser::CheckScriptSyntax() {
    syntaxErrors = ScriptIntellisense::CheckSyntax(CurrentScriptAnalysis());

    // Keep every marker on a line the editor has.
    for (ScriptIntellisense::SyntaxError& error : syntaxErrors) error.line = std::clamp(error.line, 1, std::max(1, scriptEditor.GetTotalLines()));

    TextEditor::ErrorMarkers markers;
    for (const ScriptIntellisense::SyntaxError& error : syntaxErrors) markers.emplace(error.line, error.message);
    scriptEditor.SetErrorMarkers(markers);
}

void AssetBrowser::ToggleLineComment() {
    if (scriptEditor.IsReadOnly()) return;

    const bool hadSelection = scriptEditor.HasSelection();
    const TextEditor::Coordinates rangeStart = hadSelection ? scriptEditor.GetSelectionStart() : scriptEditor.GetCursorPosition();
    const TextEditor::Coordinates rangeEnd = hadSelection ? scriptEditor.GetSelectionEnd() : scriptEditor.GetCursorPosition();

    const int firstLine = rangeStart.mLine;
    int lastLine = rangeEnd.mLine;

    // A selection ending exactly at column 0 of a line doesn't really
    // "include" that line - matches the usual Ctrl+/ behavior in other
    // editors for a selection that ends right at a line start.
    if (hadSelection && rangeEnd.mColumn == 0 && lastLine > firstLine) --lastLine;

    // Uncomment only if EVERY non-blank line in range is already commented;
    // otherwise comment every line (including already-commented ones,
    // which just end up double-commented - consistent with most editors).
    bool allCommented = true;

    for (int lineIndex = firstLine; lineIndex <= lastLine; ++lineIndex) {
        scriptEditor.SetCursorPosition({lineIndex, 0});
        const std::string text = scriptEditor.GetCurrentLineText();
        const std::size_t firstNonSpace = text.find_first_not_of(" \t");

        if (firstNonSpace != std::string::npos && text.compare(firstNonSpace, 2, "--") != 0) {
            allCommented = false;
            break;
        }
    }

    for (int lineIndex = firstLine; lineIndex <= lastLine; ++lineIndex) {
        scriptEditor.SetCursorPosition({lineIndex, 0});

        const std::string line = scriptEditor.GetCurrentLineText();
        if (line.empty()) continue;

        if (allCommented) {
            const std::size_t dashPos = line.find("--");
            if (dashPos == std::string::npos) continue;

            std::size_t removeLen = 2;
            if (dashPos + 2 < line.size() && line[dashPos + 2] == ' ') removeLen = 3;

            scriptEditor.SetSelection({lineIndex, static_cast<int>(dashPos)}, {lineIndex, static_cast<int>(dashPos + removeLen)});
            scriptEditor.Delete();
        } else {
            std::size_t firstNonSpace = line.find_first_not_of(" \t");
            if (firstNonSpace == std::string::npos) firstNonSpace = 0;

            scriptEditor.SetCursorPosition({lineIndex, static_cast<int>(firstNonSpace)});
            scriptEditor.InsertText("-- ");
        }
    }

    scriptEditor.SetCursorPosition({firstLine, 0});
    scriptEditorDirty = true;
}

void AssetBrowser::DuplicateCurrentLine() {
    if (scriptEditor.IsReadOnly()) return;

    const TextEditor::Coordinates original = scriptEditor.GetCursorPosition();
    const std::string line = scriptEditor.GetCurrentLineText();

    scriptEditor.SetCursorPosition({original.mLine, 0});
    scriptEditor.MoveEnd(false);
    scriptEditor.InsertText("\n" + line);
    scriptEditor.SetCursorPosition({original.mLine + 1, original.mColumn});

    scriptEditorDirty = true;
}

// ============================================================================
// Sound preview
// ============================================================================

namespace {
    // OpenAL only runs while the game plays, so editor previews go through
    // SDL's own audio. One preview at a time: starting another replaces it.
    SDL_AudioStream* previewStream = nullptr;

    void PlaySoundPreview(const std::filesystem::path& path) {
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            spdlog::error("Sound preview: SDL audio failed to start: {}", SDL_GetError());
            return;
        }

        const std::u8string utf8Path = path.u8string();

        SDL_AudioSpec spec;
        Uint8* data = nullptr;
        Uint32 length = 0;

        if (!SDL_LoadWAV(reinterpret_cast<const char*>(utf8Path.c_str()), &spec, &data, &length)) {
            spdlog::error("Sound preview: SDL_LoadWAV failed for {}: {}", path.string(), SDL_GetError());
            return;
        }

        if (previewStream != nullptr) SDL_DestroyAudioStream(previewStream);

        previewStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

        if (previewStream == nullptr) {
            spdlog::error("Sound preview: no playback device: {}", SDL_GetError());
            SDL_free(data);
            return;
        }

        SDL_PutAudioStreamData(previewStream, data, static_cast<int>(length));
        SDL_FlushAudioStream(previewStream);
        SDL_free(data);

        SDL_ResumeAudioStreamDevice(previewStream); // device streams start paused
    }
}

// ============================================================================
// AssetEntry hierarchy
// ============================================================================

void AssetEntry::OnDoubleClick(AssetBrowser&) {
    // Default: no-op. Concrete entry types override this for anything that
    // should happen on double-click / Enter.
}

void AssetEntry::OnCtrlLeftClick(AssetBrowser&) {
    // Default: no-op.
}

void AssetEntry::DrawContextMenu(AssetBrowser&) {
    // Default: no extra context-menu content.
}

void DirectoryEntry::OnDoubleClick(AssetBrowser& browser) {
    browser.RequestNavigate(GetPath());
}

void DirectoryEntry::DrawContextMenu(AssetBrowser& browser) {
    if (ImGui::MenuItem("Open")) browser.RequestNavigate(GetPath());
    ImGui::Separator();
    browser.DrawClipboardMenuItems(*this);
    if (ImGui::MenuItem("Paste Into Folder", nullptr, false, browser.CanPaste())) browser.RequestPaste(GetPath());
    ImGui::Separator();
    if (ImGui::MenuItem("Rename")) browser.RequestRename(GetPath(), true);
    if (ImGui::MenuItem("Delete")) browser.RequestDelete(GetPath(), true);

    ImGui::Separator();

    if (ImGui::MenuItem("Create Folder")) browser.RequestCreateFolder(GetPath());
    if (ImGui::MenuItem("Create Level")) browser.RequestCreateLevel(GetPath());
    if (ImGui::BeginMenu("Create File")) {
        browser.DrawCreateFileSubmenuItems(GetPath());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Prefabs")) {
        browser.DrawCreatePrefabSubmenuItems(GetPath());
        ImGui::EndMenu();
    }
}

void GenericFileEntry::OnDoubleClick(AssetBrowser& browser) {
    if (kind == AssetKind::Script) {
        browser.RequestOpenScript(GetPath());
        return;
    }
    if (kind == AssetKind::Flipbook) {
        browser.RequestOpenFlipbook(GetPath());
        return;
    }
    if (kind == AssetKind::Prefab) {
        browser.RequestOpenPrefab(GetPath());
        return;
    }
    if (kind == AssetKind::UILayout) {
        browser.RequestOpenUILayout(GetPath());
        return;
    }
    if (kind == AssetKind::Other && AssetBrowser::IsPlainTextFile(GetPath())) {
        browser.RequestOpenScript(GetPath());
        return;
    }
    if (kind == AssetKind::Sound) PlaySoundPreview(GetPath());

    if (kind != AssetKind::Other) browser.RequestConsumeAsFieldReference(kind, GetPath());

}

void GenericFileEntry::OnCtrlLeftClick(AssetBrowser& browser) {
    // The host decides which kinds its edit target takes.
    if (kind != AssetKind::Other) browser.RequestAssignToEditTarget(kind, GetPath());
}

void GenericFileEntry::DrawRenameAndDeleteMenuItems(AssetBrowser& browser) const {
    browser.DrawClipboardMenuItems(*this);
    ImGui::Separator();
    if (ImGui::MenuItem("Rename")) browser.RequestRename(GetPath(), false);
    if (ImGui::MenuItem("Delete")) browser.RequestDelete(GetPath(), false);
}

void GenericFileEntry::DrawContextMenu(AssetBrowser& browser) {
    if (kind == AssetKind::Script || AssetBrowser::IsPlainTextFile(GetPath())) {
        if (ImGui::MenuItem("Open in Text Editor")) browser.RequestOpenScript(GetPath());
        ImGui::Separator();
    }
    if (kind == AssetKind::UILayout) {
        if (ImGui::MenuItem("Open in UI Editor")) browser.RequestOpenUILayout(GetPath());
        ImGui::Separator();
    }
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

PrefabEntry::PrefabEntry(std::filesystem::path absolutePath, std::filesystem::path relativePath, std::string displayName)
    : GenericFileEntry(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Prefab)
    , prefabReference(AssetBrowser::ToAssetReference(absolutePath, AssetKind::Prefab)) {}

std::optional<std::string> PrefabEntry::GetThumbnailReference() const {
    const Level* prefab = Prefab::Get(prefabReference);
    if (prefab == nullptr || prefab->entities.empty()) return std::nullopt;

    // The prefab entity's first Sprite, north slot (slot 0).
    const ComponentSprite* sprite = prefab->sprites.Get(Prefab::RootOf(*prefab).id);
    if (sprite == nullptr || sprite->textureFileNames[0].empty()) return std::nullopt;

    return sprite->textureFileNames[0];
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

    if (isDirectory)
        return std::make_unique<DirectoryEntry>(absolutePath, std::move(relativePath), std::move(displayName));


    const std::string extension = LowerCopy(absolutePath.extension().string());
    const auto it = kExtensionRegistry.find(extension);

    if (it == kExtensionRegistry.end())
        return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Other);

    switch (it->second) {
        case RegisteredExtensionKind::Texture:
            return std::make_unique<TextureEntry>(absolutePath, std::move(relativePath), std::move(displayName));
        case RegisteredExtensionKind::Sound:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Sound);
        case RegisteredExtensionKind::Script:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Script);
        case RegisteredExtensionKind::Model:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Model);
        case RegisteredExtensionKind::Flipbook:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Flipbook);
        case RegisteredExtensionKind::Font:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::Font);
        case RegisteredExtensionKind::Prefab:
            return std::make_unique<PrefabEntry>(absolutePath, std::move(relativePath), std::move(displayName));
        case RegisteredExtensionKind::UILayout:
            return std::make_unique<GenericFileEntry>(absolutePath, std::move(relativePath), std::move(displayName), AssetKind::UILayout);
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
        case AssetKind::Model:   return "TILKY_ASSET_MODEL";
        case AssetKind::Flipbook: return "TILKY_ASSET_FLIPBOOK";
        case AssetKind::Font:    return "TILKY_ASSET_FONT";
        case AssetKind::Prefab:  return "TILKY_ASSET_PREFAB";
        default:                 return "TILKY_ASSET_OTHER";
    }
}

const char* AssetBrowser::MoveEntryPayloadType() {
    return kMoveEntryPayloadType;
}

std::string AssetBrowser::ToAssetReference(const std::filesystem::path& absolutePath, const AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture:return RelativeOrFallback(absolutePath,ProjectManager::GetAssetsPath()).generic_string();

        case AssetKind::Model: return RelativeOrFallback(absolutePath, ProjectManager::GetAssetsPath()).generic_string();

        case AssetKind::Flipbook: return RelativeOrFallback(absolutePath, ProjectManager::GetAssetsPath()).generic_string();

        case AssetKind::Font: return RelativeOrFallback(absolutePath, ProjectManager::GetAssetsPath()).generic_string();

        case AssetKind::Prefab: return RelativeOrFallback(absolutePath, ProjectManager::GetAssetsPath()).generic_string();

        // Sounds keep their extension: "open.wav" and "open.mp3" are two sounds.
        case AssetKind::Sound: return RelativeOrFallback(absolutePath, ProjectManager::GetAssetsPath()).generic_string();

        case AssetKind::Script: {
            fs::path rel = RelativeOrFallback(absolutePath, ProjectManager::GetAssetsPath());
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

    // A UI layout may have been made, moved, renamed or deleted.
    UILayouts::Rescan();

    // A level too: the project's level list and order follow.
    MapEditorInternal::UpdateLevels();
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

    // Auto-uniquify on a name collision (matches the existing "(2)", "(3)",
    // ... convention rather than prompting to overwrite, since importing
    // happens from an asynchronous OS drop callback with no good place to
    // block for a confirmation dialog).
    const fs::path destination = FreeCopyPath(currentDirectory, sourceAbsolutePath, sourceIsDirectory);

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

    if (!sourceIsDirectory && ModelLoader::IsModelFile(sourceAbsolutePath))
        ImportModelDependencies(sourceAbsolutePath, destination);

    selectedFile = destination;
    Refresh();
    return true;
}

void AssetBrowser::ImportModelDependencies(const std::filesystem::path& sourceModel, const std::filesystem::path& importedModel) {
    std::vector<ModelLoader::ModelDependency> dependencies;
    std::string errorMessage;

    // Resolved against the source folder only: the model is not inside the
    // project yet, so the project's Assets folder says nothing about it.
    if (!ModelLoader::CollectDependencies(sourceModel, {}, dependencies, errorMessage)) {
        lastOperationError = "Imported \"" + importedModel.filename().string() + "\", but it could not be read: " + errorMessage;
        spdlog::error("Asset browser: {}", lastOperationError);
        return;
    }

    const fs::path sourceDirectory = sourceModel.parent_path().lexically_normal();
    const fs::path importDirectory = importedModel.parent_path();

    std::vector<std::string> missing;
    int copied = 0;

    for (const ModelLoader::ModelDependency& dependency : dependencies) {
        if (dependency.status == ModelLoader::DependencyStatus::Embedded) continue;

        if (dependency.status == ModelLoader::DependencyStatus::Missing) {
            missing.push_back(dependency.reference);
            continue;
        }

        const fs::path relative = dependency.resolvedPath.lexically_relative(sourceDirectory);
        const bool insideSource = !relative.empty() && *relative.begin() != "..";
        const fs::path target = importDirectory / (insideSource ? relative : dependency.resolvedPath.filename());

        std::error_code ec;
        if (fs::exists(target, ec)) {
            spdlog::info("Asset browser: model dependency already present, kept: {}", target.string());
            continue;
        }

        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(dependency.resolvedPath, target, fs::copy_options::none, ec);

        if (ec) {
            missing.push_back(dependency.reference + " (copy failed: " + ec.message() + ")");
            continue;
        }

        ++copied;
        spdlog::info("Asset browser: imported model dependency {} -> {}", dependency.resolvedPath.string(), target.string());
    }

    if (missing.empty()) return;

    std::string list;
    for (const std::string& name : missing) list += (list.empty() ? "" : ", ") + name;

    lastOperationError = "Imported \"" + importedModel.filename().string() + "\" with " + std::to_string(copied) +
        " dependency file(s), but these were not found and will be missing: " + list;
    spdlog::warn("Asset browser: {}", lastOperationError);
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
    bool upPressed;

    if (iconFont != nullptr) {
        // Icon and word as one button: the arrow from the icon font, the
        // rest in the normal font, laid out by hand inside the frame.
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float iconSize = ImGui::GetFontSize() * 0.75f;
        const ImVec2 textSize = ImGui::CalcTextSize("Up");
        const ImVec2 padding = ImGui::GetStyle().FramePadding;
        const float gap = 5.0f;

        upPressed = ImGui::Button("##Up", ImVec2(padding.x * 2.0f + iconSize + gap + textSize.x, ImGui::GetFrameHeight()));

        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(iconFont, iconSize,
                          ImVec2(cursor.x + padding.x, cursor.y + (ImGui::GetFrameHeight() - iconSize) * 0.5f),
                          textColor, ICON_FA_ARROW_UP);
        drawList->AddText(ImVec2(cursor.x + padding.x + iconSize + gap, cursor.y + padding.y), textColor, "Up");
    }
    else upPressed = ImGui::Button("^ Up");

    if (upPressed)
        NavigateToParent();
    ImGui::EndDisabled();

    // Dropping a dragged entry onto "^ Up" moves it to the parent folder,
    // matching a normal file explorer's breadcrumb trail. Skipped entirely
    // while disabled (atRoot): there's no parent to move to in that state.
    if (!atRoot && DrawMoveDropTarget(currentDirectory.parent_path())) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                             IM_COL32(120, 220, 120, 255), 3.0f, 0, 2.0f);
    }

    ImGui::SameLine(0.0f, 10.0f);

    const std::string rootLabel = rootDirectory.filename().empty() ? std::string("Assets") : rootDirectory.filename().string();

    if (ImGui::SmallButton(rootLabel.c_str())) NavigateTo(rootDirectory);

    if (DrawMoveDropTarget(rootDirectory)) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                             IM_COL32(120, 220, 120, 255), 3.0f, 0, 2.0f);
    }

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

            // Every breadcrumb segment is a valid "move here" target too,
            // exactly like the root button above.
            if (DrawMoveDropTarget(accumulated)) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                     IM_COL32(120, 220, 120, 255), 3.0f, 0, 2.0f);
            }

            ImGui::PopID();
        }
    }
}

void AssetBrowser::DrawSearchBar() {
    const float clearSize = ImGui::GetFrameHeight();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearSize - 4.0f);
    ImGui::InputTextWithHint("##AssetBrowserSearch", "Search this folder...", searchBuffer, sizeof(searchBuffer));

    ImGui::SameLine(0.0f, 4.0f);

    bool clearPressed;

    if (iconFont != nullptr) {
        ImGui::PushFont(iconFont, ImGui::GetFontSize() * 0.75f);
        clearPressed = ImGui::Button(ICON_FA_XMARK "##ClearSearch", ImVec2(clearSize, clearSize));
        ImGui::PopFont();
    }
    else clearPressed = ImGui::Button("x##ClearSearch", ImVec2(clearSize, clearSize));

    if (clearPressed) searchBuffer[0] = '\0';
}

bool AssetBrowser::DrawMoveDropTarget(const std::filesystem::path& destinationDirectory) {
    if (!ImGui::BeginDragDropTarget()) return false;

    // Only one of these ever actually matches the drag in progress this
    // frame - see kMoveEntryPayloadType's comment for why an entry never
    // offers more than one payload type at once - so probing all of them
    // here is how a drop target stays agnostic to which one a given
    // dragged entry happened to be offering.
    static constexpr std::array<AssetKind, 6> kFieldReferenceKinds = {
        AssetKind::Texture, AssetKind::Sound, AssetKind::Script, AssetKind::Model, AssetKind::Flipbook, AssetKind::Font
    };

    for (const AssetKind kind : kFieldReferenceKinds) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDropPayloadTypeFor(kind))) {
            HandleDroppedMove(kind, *payload, destinationDirectory);
        }
    }

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMoveEntryPayloadType)) {
        HandleDroppedMove(AssetKind::Other, *payload, destinationDirectory);
    }

    AcceptEntityPrefabDrop(destinationDirectory);

    ImGui::EndDragDropTarget();
    return true;
}

void AssetBrowser::AcceptEntityPrefabDrop(const std::filesystem::path& destinationDirectory) {
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(MapEditorInternal::ENTITY_REF_PAYLOAD);
    if (payload == nullptr) return;

    MapEditorInternal::LevelObjectDragPayload dragged;
    std::memcpy(&dragged, payload->Data, sizeof(dragged));

    // A canvas drag has been moving the entity along; dropping it here
    // shouldn't move it.
    if (dragged.fromCanvas) MapEditorInternal::RevertCanvasEntityDrag();

    pendingEntityPrefab = PendingEntityPrefab{dragged.id, destinationDirectory};
}

void AssetBrowser::PerformPendingPrefab() {
    if (!pendingEntityPrefab.has_value()) return;

    const PendingEntityPrefab pending = *pendingEntityPrefab;
    pendingEntityPrefab.reset();

    std::string error;
    const fs::path created = MapEditorInternal::CreatePrefabFromEntity(pending.entityID, pending.destinationDirectory, &error);

    if (created.empty()) {
        lastOperationError = "Could not create the prefab: " + error;
        spdlog::error("Asset browser: {}", lastOperationError);
        return;
    }

    lastOperationError.clear();
    selectedFile = created;
    Refresh();
    MapEditorInternal::RescanEntityPalette();
}

void AssetBrowser::HandleDroppedMove(const AssetKind kind, const ImGuiPayload& payload, const std::filesystem::path& destinationDirectory) {
    // Deliberately just records the request - see this function's header
    // comment for why calling TryMoveEntry()/Refresh() here (synchronously,
    // from inside DrawEntries()'s loop over `entries` for a folder-tile
    // drop) is not safe. `destinationDirectory` is copied into the struct
    // by value here, while it's still guaranteed valid, before anything
    // has a chance to invalidate it.
    pendingMove = AssetBrowserPendingMove{
        fs::path(static_cast<const char*>(payload.Data)),
        destinationDirectory,
        kind
    };
}

void AssetBrowser::PerformPendingMove() {
    if (!pendingMove.has_value()) return;

    const AssetBrowserPendingMove move = *pendingMove;
    pendingMove.reset();

    std::error_code isDirEc;
    const bool sourceIsDirectory = fs::is_directory(move.source, isDirEc);

    fs::path newPath;
    std::string error;
    if (TryMoveEntry(move.source, sourceIsDirectory, move.destinationDirectory, newPath, error)) {
        if (selectedFile == move.source) selectedFile = newPath;
        if (pendingConfirmedPath.has_value() && *pendingConfirmedPath == move.source) pendingConfirmedPath = newPath;

        if (newPath != move.source) NotifyAssetReferenceRenamed(move.kind, move.source, newPath);

        lastOperationError.clear();
        Refresh();
    }
    else {
        lastOperationError = error;
        spdlog::warn("Asset browser: {}", error);
    }
}

void AssetBrowser::PerformPendingCopy() {
    if (!pendingCopy.has_value()) return;

    const PendingCopy copy = *pendingCopy;
    pendingCopy.reset();

    std::error_code isDirEc;
    const bool sourceIsDirectory = fs::is_directory(copy.source, isDirEc);

    fs::path newPath;
    std::string error;
    if (!TryCopyEntry(copy.source, sourceIsDirectory, copy.destinationDirectory, newPath, error)) {
        lastOperationError = error;
        spdlog::warn("Asset browser: {}", error);
        return;
    }

    lastOperationError.clear();

    // A model's textures and side files, for a copy in another folder.
    if (!sourceIsDirectory && ModelLoader::IsModelFile(copy.source))
        ImportModelDependencies(copy.source, newPath);

    if (copy.destinationDirectory == currentDirectory) selectedFile = newPath;

    Refresh();
    MapEditorInternal::RescanEntityPalette();
}

void AssetBrowser::RequestCopy(const AssetEntry& entry) {
    clipboard = Clipboard{entry.GetPath(), entry.GetAssetKind(), false};
}

void AssetBrowser::RequestCut(const AssetEntry& entry) {
    clipboard = Clipboard{entry.GetPath(), entry.GetAssetKind(), true};
}

void AssetBrowser::RequestDuplicate(const std::filesystem::path& absolutePath) {
    pendingCopy = PendingCopy{absolutePath, absolutePath.parent_path()};
}

void AssetBrowser::RequestPaste(const std::filesystem::path& destinationDirectory) {
    if (!clipboard.has_value()) return;

    if (clipboard->cut) {
        pendingMove = AssetBrowserPendingMove{clipboard->source, destinationDirectory, clipboard->kind};
        clipboard.reset();
    }
    else pendingCopy = PendingCopy{clipboard->source, destinationDirectory};
}

void AssetBrowser::DrawClipboardMenuItems(const AssetEntry& entry) {
    if (ImGui::MenuItem("Copy", "Ctrl+C")) RequestCopy(entry);
    if (ImGui::MenuItem("Cut", "Ctrl+X")) RequestCut(entry);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) RequestDuplicate(entry.GetPath());
}

// See this function's declaration in AssetBrowser.hpp for the full
// rationale. LevelManager::RenameTextureReference() /
// RenameSoundReference() / RenameScriptReference() do not exist yet - this
// will not compile until they're added there.
void AssetBrowser::NotifyAssetReferenceRenamed(
    const AssetKind kind, const std::filesystem::path& oldAbsolutePath, const std::filesystem::path& newAbsolutePath
) {
    if (kind == AssetKind::Other) return; // folders / Levels / non-tracked files: nothing references these by name

    const std::string oldReference = ToAssetReference(oldAbsolutePath, kind);
    const std::string newReference = ToAssetReference(newAbsolutePath, kind);
    if (oldReference == newReference) return;

    switch (kind) {
        case AssetKind::Texture:
            LevelManager::RenameTextureReference(oldReference, newReference);
            // Flipbook frames name textures too: the files on disk and any
            // flipbook open in the editor (which may have unsaved changes).
            FlipbookIO::RenameTextureReferenceInProject(oldReference, newReference);
            flipbookEditor.RenameTextureReference(oldReference, newReference);
            break;
        case AssetKind::Flipbook:
            LevelManager::RenameFlipbookReference(oldReference, newReference);
            FlipbookLibrary::Invalidate(oldReference);
            flipbookEditor.OnFileMoved(oldAbsolutePath, newAbsolutePath);
            break;
        case AssetKind::Sound:   LevelManager::RenameSoundReference(oldReference, newReference); break;
        case AssetKind::Script:  LevelManager::RenameScriptReference(oldReference, newReference); break;
        case AssetKind::Model:   LevelManager::RenameModelReference(oldReference, newReference); break;
        case AssetKind::Font:    LevelManager::RenameFontReference(oldReference, newReference); break;
        case AssetKind::Prefab:
            LevelManager::RenamePrefabReference(oldReference, newReference);
            Prefab::Invalidate(oldReference);
            prefabEditor.OnFileMoved(oldAbsolutePath, newAbsolutePath);
            break;
        default: break; // AssetKind::Other already handled above; AssetKind::Folder does not exist as a GetAssetKind() value
    }
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
        else if (ImGui::GetIO().KeyCtrl) entry.OnCtrlLeftClick(*this);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        selectedFile = entry.GetPath(); // right-click selects the target before its context menu opens

    // Drag source: every entry can be picked up and dropped onto a folder
    // tile / breadcrumb / the "^ Up" button elsewhere in this browser to
    // move it there, like a normal file explorer - see
    // DrawMoveDropTarget(). A file with a field-reference AssetKind
    // (Texture/Sound/Script) keeps offering exactly the payload type it
    // always has instead of the move type, so existing field widgets
    // elsewhere are unaffected; DrawMoveDropTarget() already knows to
    // accept that same payload as a move too, so nothing is lost.
    // SourceNoHoldToOpenOthers: by default ImGui opens any collapsed tree node
    // or CollapsingHeader the cursor rests on for ~0.7s while a payload is
    // live. Dragging a texture across an inspector would then expand sections
    // under the cursor and shove the target field out from under it. Nothing
    // in this editor wants that behavior, so opt out for every payload we make.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
        selectedFile = entry.GetPath(); // dragging an item selects it too

        const std::string payloadPath = entry.GetPath().string();

        if (!isDirectory && entry.GetAssetKind() != AssetKind::Other) {
            ImGui::SetDragDropPayload(
                DragDropPayloadTypeFor(entry.GetAssetKind()),
                payloadPath.c_str(),
                payloadPath.size() + 1
            );
        }
        else {
            ImGui::SetDragDropPayload(
                kMoveEntryPayloadType,
                payloadPath.c_str(),
                payloadPath.size() + 1
            );
        }

        ImGui::TextUnformatted(entry.GetDisplayName().c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target: only folders accept items dropped onto them, checked
    // here (right after the drag source, before BeginPopupContextItem)
    // while this tile is still guaranteed to be the last-submitted item.
    // A folder being dragged is never a target for its own drag - ImGui's
    // BeginDragDropTarget() already excludes the payload's own source item.
    const bool isDropHighlighted = isDirectory && DrawMoveDropTarget(entry.GetPath());

    // The entry's path in Assets, for Always Loaded (empty outside Assets).
    std::string assetPath;
    {
        const fs::path relative = entry.GetPath().lexically_normal().lexically_relative(ProjectManager::GetAssetsPath().lexically_normal());
        if (!relative.empty() && *relative.begin() != ".." && relative != ".") assetPath = relative.generic_string();
    }

    if (ImGui::BeginPopupContextItem()) {
        entry.DrawContextMenu(*this);

        if (!assetPath.empty()) {
            ImGui::Separator();
            const bool listed = AssetSets::IsListedAlwaysLoaded(assetPath);
            const bool covered = !listed && AssetSets::IsAlwaysLoaded(assetPath);

            if (covered) ImGui::BeginDisabled();
            if (ImGui::MenuItem(Localisation::Get("asset_browser.always_loaded").c_str(), nullptr, listed || covered)) {
                AssetSets::SetAlwaysLoaded(assetPath, !listed);
                if (!MapEditorInternal::SaveProjectSettings()) spdlog::error("Couldn't save Always Loaded to the project settings");
            }
            if (covered) ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Localisation::Get(covered ? "asset_browser.tooltip.always_loaded_by_folder"
                                                                  : "asset_browser.tooltip.always_loaded").c_str());
        }

        ImGui::EndPopup();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 boxMax = {topLeft.x + tileSize, topLeft.y + tileSize};

    ImTextureID texture{};

    if (!isDirectory && thumbnailProvider)
        if (const std::optional<std::string> reference = entry.GetThumbnailReference()) texture = thumbnailProvider(*reference);

    // A big icon centred in the tile, `caption` under it (if any).
    const auto drawTileIcon = [&](const char* icon, const ImU32 color, const char* caption) {
        ImGui::PushFont(iconFont, tileSize * 0.42f);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImGui::PopFont();

        const ImVec2 captionSize = caption != nullptr ? ImGui::CalcTextSize(caption) : ImVec2(0.0f, 0.0f);
        const float gap = caption != nullptr ? 2.0f : 0.0f;
        const float top = topLeft.y + (tileSize - iconSize.y - gap - captionSize.y) * 0.5f;

        drawList->AddText(iconFont, tileSize * 0.42f, ImVec2(topLeft.x + (tileSize - iconSize.x) * 0.5f, top), color, icon);

        if (caption != nullptr)
            drawList->AddText(
                ImVec2(topLeft.x + (tileSize - captionSize.x) * 0.5f, top + iconSize.y + gap),
                IM_COL32(220, 220, 220, 255),
                caption
            );
    };

    if (texture != ImTextureID{} && entry.GetAssetKind() == AssetKind::Prefab) {
        // A prefab's sprite: a card in the prefab colour, the sprite inset
        // on a checkerboard, a coloured frame and a cube badge in the corner,
        // so it can't be mistaken for the .png it uses.
        const ImU32 accent = IM_COL32(90, 190, 210, 255);
        const float inset = std::max(4.0f, tileSize * 0.1f);
        const ImVec2 imageMin = {topLeft.x + inset, topLeft.y + inset};
        const ImVec2 imageMax = {boxMax.x - inset, boxMax.y - inset};

        drawList->AddRectFilled(topLeft, boxMax, EntryTileTint(entry), 6.0f);

        const float cell = std::max(4.0f, (imageMax.x - imageMin.x) / 8.0f);
        drawList->PushClipRect(imageMin, imageMax, true);
        for (float y = imageMin.y; y < imageMax.y; y += cell)
            for (float x = imageMin.x; x < imageMax.x; x += cell) {
                const bool dark = (static_cast<int>((x - imageMin.x) / cell) + static_cast<int>((y - imageMin.y) / cell)) % 2 == 0;
                drawList->AddRectFilled({x, y}, {x + cell, y + cell}, dark ? IM_COL32(30, 52, 58, 255) : IM_COL32(40, 66, 74, 255));
            }
        drawList->PopClipRect();

        drawList->AddImage(texture, imageMin, imageMax);
        drawList->AddRect(topLeft, boxMax, accent, 6.0f, 0, 2.0f);

        // Badge: a filled corner tab with the prefab glyph (or "P" without the icon font).
        const float badgeSize = std::max(14.0f, tileSize * 0.3f);
        const ImVec2 badgeMin = {boxMax.x - badgeSize, boxMax.y - badgeSize};
        drawList->AddRectFilled(badgeMin, boxMax, accent, 6.0f, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight);

        const char* badgeGlyph = iconFont != nullptr ? ICON_FA_CUBES : "P";
        ImFont* badgeFont = iconFont != nullptr ? iconFont : ImGui::GetFont();
        const float badgeFontSize = badgeSize * 0.6f;
        ImGui::PushFont(badgeFont, badgeFontSize);
        const ImVec2 glyphSize = ImGui::CalcTextSize(badgeGlyph);
        ImGui::PopFont();
        drawList->AddText(
            badgeFont,
            badgeFontSize,
            {badgeMin.x + (badgeSize - glyphSize.x) * 0.5f, badgeMin.y + (badgeSize - glyphSize.y) * 0.5f},
            IM_COL32(20, 30, 34, 255),
            badgeGlyph
        );
    }
    else if (texture != ImTextureID{}) drawList->AddImage(texture, topLeft, boxMax);
    else if (isDirectory) {
        if (iconFont != nullptr) drawTileIcon(ICON_FA_FOLDER, IM_COL32(232, 188, 84, 255), nullptr);
        else {
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
    }
    else {
        drawList->AddRectFilled(topLeft, boxMax, EntryTileTint(entry), 4.0f);
        drawList->AddRect(topLeft, boxMax, IM_COL32(150, 150, 150, 120), 4.0f);

        const std::string tag = EntryVisualTag(entry);

        if (iconFont != nullptr) drawTileIcon(EntryIcon(entry), IM_COL32(235, 235, 235, 255), tag.c_str());
        else {
            const ImVec2 tagSize = ImGui::CalcTextSize(tag.c_str());
            drawList->AddText(
                {topLeft.x + (tileSize - tagSize.x) * 0.5f, topLeft.y + (tileSize - tagSize.y) * 0.5f},
                IM_COL32(220, 220, 220, 255),
                tag.c_str()
            );
        }
    }

    // Faded until the Paste that moves it.
    if (clipboard.has_value() && clipboard->cut && clipboard->source == entry.GetPath())
        drawList->AddRectFilled(topLeft, boxMax, IM_COL32(0, 0, 0, 130), 4.0f);

    if (isSelected)
        drawList->AddRect(topLeft, boxMax, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f, 0, 2.5f);

    if (isDropHighlighted)
        drawList->AddRect(topLeft, boxMax, IM_COL32(120, 220, 120, 255), 4.0f, 0, 3.0f);

    // Always Loaded: a pin in the top-left corner (dimmer when a folder above sets it).
    if (!assetPath.empty() && AssetSets::IsAlwaysLoaded(assetPath)) {
        const bool listed = AssetSets::IsListedAlwaysLoaded(assetPath);
        const float badgeSize = std::max(12.0f, tileSize * 0.22f);
        const ImVec2 badgeMax = {topLeft.x + badgeSize, topLeft.y + badgeSize};
        drawList->AddRectFilled(topLeft, badgeMax, listed ? IM_COL32(230, 160, 40, 235) : IM_COL32(150, 110, 50, 190), 4.0f,
                                ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight);

        const char* glyph = iconFont != nullptr ? ICON_FA_THUMBTACK : "A";
        ImFont* font = iconFont != nullptr ? iconFont : ImGui::GetFont();
        const float fontSize = badgeSize * 0.62f;
        ImGui::PushFont(font, fontSize);
        const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
        ImGui::PopFont();
        drawList->AddText(font, fontSize, {topLeft.x + (badgeSize - glyphSize.x) * 0.5f, topLeft.y + (badgeSize - glyphSize.y) * 0.5f},
                          IM_COL32(255, 255, 255, 255), glyph);
    }

    const std::string name = TruncateToWidth(entry.GetDisplayName(), tileSize);
    const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());

    drawList->AddText(
        {topLeft.x + (tileSize - nameSize.x) * 0.5f, topLeft.y + tileSize + 4.0f},
        ImGui::GetColorU32(ImGuiCol_Text),
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
        if (ImGui::BeginMenu("Prefabs")) {
            DrawCreatePrefabSubmenuItems(currentDirectory);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, CanPaste())) RequestPaste(currentDirectory);
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
            const bool created = CreateGenericFileAsset(activeModal.destinationDirectory, enteredName, createdFilePath, error);

            // A .fpk has to be a valid (empty) flipbook from the start, or
            // nothing can open it.
            if (created && LowerCopy(createdFilePath.extension().string()) == FlipbookIO::kExtension) {
                if (FlipbookIO::Save(createdFilePath, FlipbookAsset{}, &error)) RequestOpenFlipbook(createdFilePath);
                else spdlog::error("Asset browser: {}", error);
            }

            // Same for a .prefab: it starts as an entity named after the file.
            if (created && LowerCopy(createdFilePath.extension().string()) == Prefab::kExtension) {
                const Level prefab = Prefab::MakeEmpty(createdFilePath.stem().string(), activeModal.createUIPrefab);
                if (LevelSerialization::SavePrefabToFile(createdFilePath, prefab, &error)) RequestOpenPrefab(createdFilePath);
                else spdlog::error("Asset browser: {}", error);
            }

            // A .uilayout needs its layout ID from the start; it opens in the UI editor.
            if (created && LowerCopy(createdFilePath.extension().string()) == UILayouts::kExtension) {
                std::uint64_t layoutID = 0;
                if (UILayouts::Create(createdFilePath, &layoutID, &error)) MapEditorInternal::OpenUILayoutInEditor(layoutID);
                else spdlog::error("Asset browser: {}", error);
            }

            // A new text file opens straight away, ready to type into.
            if (created && IsPlainTextFile(createdFilePath)) RequestOpenScript(createdFilePath);

            if (created) {
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
                const AssetKind renamedKind = activeModal.targetAssetKind;

                activeModal.kind = AssetBrowserModalKind::None;
                ImGui::CloseCurrentPopup();

                // Update any cached paths this browser owns before refreshing.
                if (selectedFile == oldPath) selectedFile = newPath;
                if (pendingConfirmedPath.has_value() && *pendingConfirmedPath == oldPath) pendingConfirmedPath = newPath;

                if (newPath != oldPath) NotifyAssetReferenceRenamed(renamedKind, oldPath, newPath);

                if (!activeModal.targetIsDirectory && oldPath.extension() == kLevelFileExtension)
                    Editor::LevelFileRenamed(oldPath.stem().string(), newPath.stem().string());

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

    MapEditorInternal::AssetBrowserHasKeyboard();

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        NavigateToParent();
        return;
    }

    const bool ctrl = ImGui::GetIO().KeyCtrl;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_R)) {
        Refresh();
        return;
    }

    // No repeat: holding the keys makes one copy.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        RequestPaste(currentDirectory);
        return;
    }

    if (selectedFile.empty()) return;

    AssetEntry* selectedEntry = FindEntry(selectedFile);
    if (selectedEntry == nullptr) return; // stale selection (e.g. deleted externally) - nothing to act on

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) RequestCopy(*selectedEntry);
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) RequestCut(*selectedEntry);
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) RequestDuplicate(selectedEntry->GetPath());
    else if (ImGui::IsKeyPressed(ImGuiKey_F2))
        RequestRename(selectedEntry->GetPath(), selectedEntry->GetType() == AssetEntryType::Directory);
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        RequestDelete(selectedEntry->GetPath(), selectedEntry->GetType() == AssetEntryType::Directory);
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
        selectedEntry->OnDoubleClick(*this);
}

void AssetBrowser::Draw(const ThumbnailProvider& thumbnailProvider, const AssignHandler& assignHandler) {
    if (rootDirectory.empty()) return; // SetRootDirectory() hasn't been called yet

    activeAssignHandler = &assignHandler;

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

    // An entity dropped anywhere else in the panel becomes a prefab in the
    // folder being shown. Folder tiles and breadcrumbs are smaller targets,
    // so ImGui lets them win when the drop is over one.
    if (const ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::BeginDragDropTargetCustom(window->InnerRect, window->ID)) {
        AcceptEntityPrefabDrop(currentDirectory);
        ImGui::EndDragDropTarget();
    }

    // Only safe once DrawEntries() has fully returned - see
    // HandleDroppedMove()'s comment for why.
    PerformPendingMove();
    PerformPendingCopy();
    PerformPendingPrefab();

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

    activeAssignHandler = nullptr;
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

    // Captured now, while the entry is still guaranteed to be in `entries`
    // - see NotifyAssetReferenceRenamed(), called once the rename actually
    // succeeds in DrawRenameModal(). A directory (or a stale/missing
    // entry, which shouldn't normally happen here) falls back to
    // AssetKind::Other, meaning "nothing to propagate".
    const AssetEntry* targetEntry = FindEntry(targetAbsolutePath);
    activeModal.targetAssetKind = targetEntry != nullptr ? targetEntry->GetAssetKind() : AssetKind::Other;

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

void AssetBrowser::RequestOpenFlipbook(const std::filesystem::path& absolutePath) {
    if (!IsPathWithinRoot(absolutePath)) {
        lastOperationError = "Refused to open a flipbook outside the asset root: " + absolutePath.string();
        return;
    }

    flipbookEditor.Open(absolutePath);
}

void AssetBrowser::DrawFlipbookEditorWindows() {
    flipbookEditor.Draw();
}

void AssetBrowser::RequestOpenPrefab(const std::filesystem::path& absolutePath) {
    if (!IsPathWithinRoot(absolutePath)) {
        lastOperationError = "Refused to open a prefab outside the asset root: " + absolutePath.string();
        return;
    }

    prefabEditor.Open(absolutePath);
}

void AssetBrowser::DrawPrefabEditorWindows() {
    prefabEditor.Draw();
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

bool AssetBrowser::RequestAssignToEditTarget(const AssetKind kind, const std::filesystem::path& absolutePath) {
    if (activeAssignHandler == nullptr || !*activeAssignHandler) return false;
    return (*activeAssignHandler)(kind, ToAssetReference(absolutePath, kind));
}

void AssetBrowser::DrawCreateFileSubmenuItems(const std::filesystem::path &destinationDirectory) {
    if (ImGui::MenuItem("Script (.lua)")) RequestCreateFile(destinationDirectory, ".lua");
    if (ImGui::MenuItem("Flipbook (.fpk)")) RequestCreateFile(destinationDirectory, std::string(FlipbookIO::kExtension));
    if (ImGui::MenuItem("UI Layout (.uilayout)")) RequestCreateFile(destinationDirectory, std::string(UILayouts::kExtension));
    if (ImGui::MenuItem("Text (.txt)")) RequestCreateFile(destinationDirectory, ".txt");
    if (ImGui::MenuItem("Custom...")) RequestCreateFile(destinationDirectory, "");
}

void AssetBrowser::DrawCreatePrefabSubmenuItems(const std::filesystem::path& destinationDirectory) {
    if (ImGui::MenuItem("Empty Prefab")) RequestCreatePrefab(destinationDirectory, false);
    if (ImGui::MenuItem("Empty UI Prefab")) RequestCreatePrefab(destinationDirectory, true);
}

void AssetBrowser::RequestOpenUILayout(const std::filesystem::path& absolutePath) {
    std::uint64_t layoutID = 0;
    std::string error;

    if (!LevelSerialization::ReadUILayoutID(absolutePath, layoutID, &error)) {
        lastOperationError = error;
        spdlog::warn("Asset browser: {}", error);
        return;
    }

    // Found by ID, so the file has to be in the list.
    if (UILayouts::Find(layoutID) == nullptr) UILayouts::Rescan();
    MapEditorInternal::OpenUILayoutInEditor(layoutID);
}

void AssetBrowser::RequestCreatePrefab(const std::filesystem::path& destinationDirectory, const bool uiEntity) {
    RequestCreateFile(destinationDirectory, std::string(Prefab::kExtension));
    if (activeModal.kind == AssetBrowserModalKind::CreateFile) activeModal.createUIPrefab = uiEntity;
}