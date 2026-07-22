#include "Headers/Editor/AssetBrowser.hpp"

#include <algorithm>
#include <cctype>

#include "imgui.h"
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

#include <array>
#include <string_view>

namespace fs = std::filesystem;

namespace {
    std::string LowerCopy(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    constexpr std::array<std::string_view, 6> ACCEPTED_EXTENSIONS = {
        ".png",
        ".jpg",
        ".jpeg",
        ".bson",
        ".wav",
        ".lua"
    };

    bool HasAcceptedExtension(const fs::path& path) {
        const std::string extension = LowerCopy(path.extension().string());
        return std::ranges::find(ACCEPTED_EXTENSIONS, extension) != ACCEPTED_EXTENSIONS.end();
    }

    bool IsHidden(const fs::path& path) {
        const std::string name = path.filename().string();
        return !name.empty() && name.front() == '.';
    }

    AssetKind DetermineAssetKind(const fs::path& absolutePath) {
        const std::string ext = LowerCopy(absolutePath.extension().string());

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return AssetKind::Texture;
        if (ext == ".wav") return AssetKind::Sound;
        if (ext == ".lua") return AssetKind::Script;

        return AssetKind::Other;
    }

    const char* KindTag(const AssetKind kind) {
        switch (kind) {
            case AssetKind::Texture: return "img";
            case AssetKind::Sound:   return "wav";
            case AssetKind::Script:  return "lua";
            default:                 return "file";
        }
    }

    ImU32 TileTint(const AssetKind kind) {
        switch (kind) {
            case AssetKind::Sound:  return IM_COL32(45, 70, 90, 255);
            case AssetKind::Script: return IM_COL32(55, 80, 55, 255);
            default:                return IM_COL32(60, 60, 65, 255);
        }
    }

    // Shrinks `text` (plus an ellipsis) until it fits inside `maxWidth`.
    // Relies on the currently bound ImGui font, so only call while drawing.
    std::string TruncateToWidth(const std::string& text, const float maxWidth) {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
            return text;

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
    // safe fallback (never an absolute path leaking into level data).
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
}

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
        case AssetKind::Texture:
            return RelativeOrFallback(absolutePath, ProjectManager::GetTexturesPath()).generic_string();

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

        default:
            return absolutePath.filename().string();
    }
}

void AssetBrowser::SetRootDirectory(const std::filesystem::path& root) {
    if (!fs::exists(root)) {
        try {
            fs::create_directories(root);
            spdlog::warn("Asset browser: created missing asset root folder: {}", root.string());
        } catch (const fs::filesystem_error& e) {
            spdlog::error("Asset browser: failed to create asset root folder {}: {}", root.string(), e.what());
        }
    }

    // Make sure the standard subfolders are visible immediately, even on a brand-new project.
    for (const fs::path &standardFolder:
         {
             ProjectManager::GetTexturesPath(),
             ProjectManager::GetSoundsPath(),
             ProjectManager::GetScriptsPath()
         }) {
        std::error_code ec;
        if (!fs::exists(standardFolder, ec))
            fs::create_directories(standardFolder, ec);
    }

    rootDirectory = root;
    currentDirectory = root;
    selectedFile.clear();
    pendingConfirmedPath.reset();
    searchBuffer[0] = '\0';

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

    if (currentDirectory.empty() || !fs::exists(currentDirectory) || !fs::is_directory(currentDirectory)) {
        spdlog::warn("Asset browser: directory unavailable: {}", currentDirectory.string());
        scanFailed = true;
        return;
    }

    std::vector<AssetBrowserEntry> folders;
    std::vector<AssetBrowserEntry> files;

    try {
        for (const auto& dirEntry : fs::directory_iterator(currentDirectory)) {
            try {
                const fs::path absolutePath = dirEntry.path();

                if (IsHidden(absolutePath)) continue;

                if (dirEntry.is_directory()) {
                    AssetBrowserEntry entry;
                    entry.absolutePath = absolutePath;
                    entry.relativePath = absolutePath.lexically_relative(rootDirectory);
                    entry.displayName = absolutePath.filename().string();
                    entry.isDirectory = true;
                    folders.push_back(std::move(entry));
                    continue;
                }

                if (!dirEntry.is_regular_file() || !HasAcceptedExtension(absolutePath)) continue;

                AssetBrowserEntry entry;
                entry.absolutePath = absolutePath;
                entry.relativePath = absolutePath.lexically_relative(rootDirectory);
                entry.displayName = absolutePath.filename().string();
                entry.isDirectory = false;
                entry.kind = DetermineAssetKind(absolutePath);

                files.push_back(std::move(entry));
            } catch (const fs::filesystem_error& e) {
                // One bad entry (broken symlink, permission issue, etc.)
                // must not take the whole listing down with it.
                spdlog::warn("Asset browser: skipping inaccessible entry: {}", e.what());
            }
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::warn("Asset browser: failed to read directory {}: {}", currentDirectory.string(), e.what());
        scanFailed = true;
        return;
    }

    const auto byNameCaseInsensitive = [](const AssetBrowserEntry& a, const AssetBrowserEntry& b) {
        return LowerCopy(a.displayName) < LowerCopy(b.displayName);
    };

    std::ranges::sort(folders, byNameCaseInsensitive);
    std::ranges::sort(files, byNameCaseInsensitive);

    entries.reserve(folders.size() + files.size());
    entries.insert(entries.end(), std::make_move_iterator(folders.begin()), std::make_move_iterator(folders.end()));
    entries.insert(entries.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
}

bool AssetBrowser::ImportExternalFile(const std::filesystem::path& sourceAbsolutePath) {
    if (currentDirectory.empty())
        return false;

    std::error_code existsEc;
    if (!fs::exists(sourceAbsolutePath, existsEc) || existsEc) {
        spdlog::warn("Asset browser: dropped file no longer exists: {}", sourceAbsolutePath.string());
        return false;
    }

    fs::path destination = currentDirectory / sourceAbsolutePath.filename();

    const std::string stem = sourceAbsolutePath.stem().string();
    const std::string ext = sourceAbsolutePath.extension().string();

    for (int suffix = 2; fs::exists(destination); ++suffix)
        destination = currentDirectory / (stem + " (" + std::to_string(suffix) + ")" + ext);

    try {
        fs::copy_file(sourceAbsolutePath, destination, fs::copy_options::none);
    } catch (const fs::filesystem_error& e) {
        spdlog::error("Asset browser: failed to import {}: {}", sourceAbsolutePath.string(), e.what());
        return false;
    }

    spdlog::info("Asset browser: imported {} -> {}", sourceAbsolutePath.string(), destination.string());
    selectedFile = destination;
    Refresh();
    return true;
}

bool AssetBrowser::IsScreenPointInside(const float screenX, const float screenY) const {
    return screenX >= lastWindowScreenMinX && screenX <= lastWindowScreenMaxX &&
           screenY >= lastWindowScreenMinY && screenY <= lastWindowScreenMaxY;
}

void AssetBrowser::DrawBreadcrumbs() {
    const bool atRoot = (currentDirectory == rootDirectory);

    ImGui::BeginDisabled(atRoot);
    if (ImGui::Button("^ Up"))
        NavigateToParent();
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 10.0f);

    const std::string rootLabel = rootDirectory.filename().empty()
                                       ? std::string("Assets")
                                       : rootDirectory.filename().string();

    if (ImGui::SmallButton(rootLabel.c_str()))
        NavigateTo(rootDirectory);

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

            if (ImGui::SmallButton(part.string().c_str()))
                NavigateTo(accumulated);

            ImGui::PopID();
        }
    }
}

void AssetBrowser::DrawSearchBar() {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
    ImGui::InputTextWithHint("##AssetBrowserSearch", "Search this folder...", searchBuffer, sizeof(searchBuffer));

    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::SmallButton("x"))
        searchBuffer[0] = '\0';
}

void AssetBrowser::DrawFolderTile(const AssetBrowserEntry& entry, const float tileSize) {
    const float labelHeight = ImGui::GetTextLineHeight() + 6.0f;

    ImGui::BeginGroup();

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::Selectable(
        "##FolderTile",
        false,
        ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(tileSize, tileSize + labelHeight)
    );

    if (clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        NavigateTo(entry.absolutePath);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 iconMax = ImVec2(topLeft.x + tileSize, topLeft.y + tileSize);

    drawList->AddRectFilled(topLeft, iconMax, IM_COL32(90, 78, 45, 255), 4.0f);
    drawList->AddRect(topLeft, iconMax, IM_COL32(150, 130, 80, 255), 4.0f);

    constexpr const char* folderLabel = "[dir]";
    const ImVec2 folderLabelSize = ImGui::CalcTextSize(folderLabel);
    drawList->AddText(
        ImVec2(topLeft.x + (tileSize - folderLabelSize.x) * 0.5f, topLeft.y + (tileSize - folderLabelSize.y) * 0.5f),
        IM_COL32(230, 220, 190, 255),
        folderLabel
    );

    const std::string name = TruncateToWidth(entry.displayName, tileSize);
    const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
    drawList->AddText(
        ImVec2(topLeft.x + (tileSize - nameSize.x) * 0.5f, topLeft.y + tileSize + 4.0f),
        IM_COL32(220, 220, 220, 255),
        name.c_str()
    );

    ImGui::EndGroup();
}

void AssetBrowser::DrawFileTile(
    const AssetBrowserEntry& entry,
    const float tileSize,
    const ThumbnailProvider& thumbnailProvider
) {
    const bool isSelected = !selectedFile.empty() && selectedFile == entry.absolutePath;
    const float labelHeight = ImGui::GetTextLineHeight() + 6.0f;

    ImGui::BeginGroup();

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable(
        "##FileTile",
        isSelected,
        ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(tileSize, tileSize + labelHeight)
    );

    if (clicked) {
        selectedFile = entry.absolutePath;

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            pendingConfirmedPath = entry.absolutePath;
            pendingConfirmedKind = entry.kind;
        }
    }

    if (entry.kind != AssetKind::Other && ImGui::BeginDragDropSource()) {
        const std::string payloadPath = entry.absolutePath.string();

        ImGui::SetDragDropPayload(
            DragDropPayloadTypeFor(entry.kind),
            payloadPath.c_str(),
            payloadPath.size() + 1
        );

        ImGui::TextUnformatted(entry.displayName.c_str());
        ImGui::EndDragDropSource();
    }

    ImTextureID texture{};

    if (entry.kind == AssetKind::Texture && thumbnailProvider) {
        texture = thumbnailProvider(ToAssetReference(entry.absolutePath, AssetKind::Texture));
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 boxMax = {topLeft.x + tileSize, topLeft.y + tileSize};

    if (texture != ImTextureID{}) {
        drawList->AddImage(texture, topLeft, boxMax);
    }
    else {
        drawList->AddRectFilled(topLeft, boxMax, TileTint(entry.kind), 4.0f);
        drawList->AddRect(topLeft, boxMax, IM_COL32(150, 150, 150, 255), 4.0f);

        const char* tag = KindTag(entry.kind);
        const ImVec2 tagSize = ImGui::CalcTextSize(tag);

        drawList->AddText(
            {topLeft.x + (tileSize - tagSize.x) * 0.5f, topLeft.y + (tileSize - tagSize.y) * 0.5f},
            IM_COL32(220, 220, 220, 255),
            tag
        );
    }

    if (isSelected) {
        drawList->AddRect(topLeft, boxMax, IM_COL32(90, 170, 250, 255), 4.0f, 0, 2.5f);
    }

    const std::string name = TruncateToWidth(entry.displayName, tileSize);
    const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());

    drawList->AddText(
        {topLeft.x + (tileSize - nameSize.x) * 0.5f, topLeft.y + tileSize + 4.0f},
        IM_COL32(220, 220, 220, 255),
        name.c_str()
    );

    ImGui::EndGroup();
}

void AssetBrowser::DrawEntries(const ThumbnailProvider& thumbnailProvider) {
    if (scanFailed) {
        ImGui::TextDisabled("%s", "This asset folder could not be read.");
        return;
    }

    if (entries.empty()) {
        ImGui::TextDisabled("%s", "This folder is empty.");
        return;
    }

    const std::string searchLower = LowerCopy(searchBuffer);
    const bool filtering = !searchLower.empty();

    constexpr float tileSize = 84.0f;

    const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();
    const float rightEdge = gridOrigin.x + ImGui::GetContentRegionAvail().x;

    bool anyVisible = false;
    bool isFirstInRow = true;

    for (const AssetBrowserEntry& entry : entries) {
        if (filtering && LowerCopy(entry.displayName).find(searchLower) == std::string::npos)
            continue;

        anyVisible = true;

        if (!isFirstInRow)
            ImGui::SameLine();

        ImGui::PushID(entry.absolutePath.string().c_str());

        if (entry.isDirectory) DrawFolderTile(entry, tileSize);
        else DrawFileTile(entry, tileSize, thumbnailProvider);

        ImGui::PopID();

        const float lastTileRight = ImGui::GetItemRectMax().x;
        const float nextTileRight = lastTileRight + ImGui::GetStyle().ItemSpacing.x + tileSize;

        isFirstInRow = (nextTileRight > rightEdge);
    }

    if (!anyVisible) ImGui::TextDisabled("%s", "No items match your search.");
}

void AssetBrowser::Draw(const ThumbnailProvider& thumbnailProvider) {
    if (rootDirectory.empty()) return; // SetRootDirectory() hasn't been called yet

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    lastWindowScreenMinX = windowPos.x;
    lastWindowScreenMinY = windowPos.y;
    lastWindowScreenMaxX = windowPos.x + windowSize.x;
    lastWindowScreenMaxY = windowPos.y + windowSize.y;

    DrawBreadcrumbs();
    ImGui::Spacing();
    DrawSearchBar();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawEntries(thumbnailProvider);
}

bool AssetBrowser::HasSelection() const {
    return !selectedFile.empty();
}

const std::filesystem::path& AssetBrowser::GetSelectedFile() const {
    return selectedFile;
}

bool AssetBrowser::ConsumePendingConfirmedSelection(const AssetKind expectedKind, std::filesystem::path& outAbsolutePath) {
    if (!pendingConfirmedPath.has_value())
        return false;

    if (pendingConfirmedKind != expectedKind)
        return false; // wrong kind - leave it queued for a matching field

    outAbsolutePath = *pendingConfirmedPath;
    pendingConfirmedPath.reset();
    return true;
}