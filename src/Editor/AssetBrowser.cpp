//
// Created by berke on 7/15/2026.
//
#include "Headers/Editor/AssetBrowser.hpp"

#include <algorithm>
#include <cctype>

#include "imgui.h"
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace {
    std::string LowerCopy(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    bool HasPngExtension(const fs::path& path) {
        return LowerCopy(path.extension().string()) == ".png";
    }

    // Shrinks `text` (plus an ellipsis) until it fits inside `maxWidth`.
    // Relies on the currently bound ImGui font, so only call this while
    // drawing.
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
}

AssetBrowser::~AssetBrowser() {
    ReleaseOwnedThumbnails();
}

void AssetBrowser::ReleaseOwnedThumbnails() {
    for (auto& [path, texture] : ownedThumbnails) {
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
    }
    ownedThumbnails.clear();
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

    rootDirectory = root;
    currentDirectory = root;
    selectedFile.clear();
    pendingConfirmedSelection.reset();
    searchBuffer[0] = '\0';

    ScanCurrentDirectory();
}

void AssetBrowser::Refresh() {
    ReleaseOwnedThumbnails();
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
    if (!IsPathWithinRoot(absoluteDirectory))
        return;

    if (!fs::exists(absoluteDirectory) || !fs::is_directory(absoluteDirectory))
        return;

    currentDirectory = absoluteDirectory;
    searchBuffer[0] = '\0';
    ScanCurrentDirectory();
}

void AssetBrowser::NavigateToParent() {
    if (currentDirectory == rootDirectory)
        return; // never step above the allowed root

    NavigateTo(currentDirectory.parent_path());
}

int AssetBrowser::TryFindEngineTextureIndex(const std::filesystem::path& absolutePath) const {
    // The engine only assigns permanent texture indices to files living
    // directly inside the project's flat Textures folder - see
    // EditorTextureCache::RefreshLevelTexturesFromFolder, which is
    // intentionally non-recursive. Anywhere else in the asset tree, the
    // browser has to load its own preview copy instead.
    std::error_code ec;

    const fs::path texturesPath = fs::weakly_canonical(ProjectManager::GetTexturesPath(), ec);
    if (ec) return -1;

    const fs::path parent = fs::weakly_canonical(absolutePath.parent_path(), ec);
    if (ec) return -1;

    if (LowerCopy(parent.string()) != LowerCopy(texturesPath.string()))
        return -1;

    const Level& level = LevelManager::CurrentLevel();
    const std::string wantedName = LowerCopy(absolutePath.filename().string());

    for (int i = 0; i < static_cast<int>(level.textures.size()); ++i) {
        if (level.textures[i].fileName.empty())
            continue;

        if (LowerCopy(level.textures[i].fileName) == wantedName)
            return i;
    }

    return -1;
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

                if (dirEntry.is_directory()) {
                    AssetBrowserEntry entry;
                    entry.absolutePath = absolutePath;
                    entry.relativePath = absolutePath.lexically_relative(rootDirectory);
                    entry.displayName = absolutePath.filename().string();
                    entry.isDirectory = true;
                    folders.push_back(std::move(entry));
                    continue;
                }

                if (!dirEntry.is_regular_file() || !HasPngExtension(absolutePath))
                    continue;

                AssetBrowserEntry entry;
                entry.absolutePath = absolutePath;
                entry.relativePath = absolutePath.lexically_relative(rootDirectory);
                entry.displayName = absolutePath.filename().string();
                entry.isDirectory = false;
                entry.engineTextureIndex = TryFindEngineTextureIndex(absolutePath);

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

SDL_Texture* AssetBrowser::ResolveThumbnail(const AssetBrowserEntry& entry, SDL_Renderer* renderer) {
    if (entry.isDirectory)
        return nullptr;

    if (entry.engineTextureIndex >= 0) {
        // Reuse the texture the engine already has loaded - never load a
        // second copy of the same file.
        return EditorTextureCache::Get(entry.engineTextureIndex);
    }

    const std::string key = entry.absolutePath.string();

    const auto found = ownedThumbnails.find(key);
    if (found != ownedThumbnails.end())
        return found->second; // may legitimately be nullptr from a prior failed load

    if (renderer == nullptr)
        return nullptr;

    SDL_Texture* texture = IMG_LoadTexture(renderer, key.c_str());

    if (texture == nullptr) {
        spdlog::warn("Asset browser: failed to load thumbnail for {}: {}", key, SDL_GetError());
    }

    // Cache the result either way (including nullptr) so a broken image
    // doesn't get retried on every single frame.
    ownedThumbnails.emplace(key, texture);
    return texture;
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

void AssetBrowser::DrawPngTile(const AssetBrowserEntry& entry, const float tileSize, SDL_Renderer* renderer) {
    const bool isSelected = (!selectedFile.empty() && selectedFile == entry.absolutePath);
    const float labelHeight = ImGui::GetTextLineHeight() + 6.0f;

    ImGui::BeginGroup();

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::Selectable(
        "##PngTile",
        isSelected,
        ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(tileSize, tileSize + labelHeight)
    );

    if (clicked) {
        selectedFile = entry.absolutePath;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            pendingConfirmedSelection = entry.absolutePath;
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const std::string payloadPath = entry.absolutePath.string();
        ImGui::SetDragDropPayload(
            PNG_DRAG_DROP_PAYLOAD_TYPE,
            payloadPath.c_str(),
            payloadPath.size() + 1
        );
        ImGui::TextUnformatted(entry.displayName.c_str());
        ImGui::EndDragDropSource();
    }

    SDL_Texture* texture = ResolveThumbnail(entry, renderer);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 imageMax = ImVec2(topLeft.x + tileSize, topLeft.y + tileSize);

    if (texture != nullptr) {
        drawList->AddImage(reinterpret_cast<ImTextureID>(texture), topLeft, imageMax);
    } else {
        drawList->AddRectFilled(topLeft, imageMax, IM_COL32(60, 45, 45, 255), 4.0f);
        drawList->AddRect(topLeft, imageMax, IM_COL32(150, 90, 90, 255), 4.0f);

        constexpr const char* placeholder = "?";
        const ImVec2 placeholderSize = ImGui::CalcTextSize(placeholder);
        drawList->AddText(
            ImVec2(topLeft.x + (tileSize - placeholderSize.x) * 0.5f, topLeft.y + (tileSize - placeholderSize.y) * 0.5f),
            IM_COL32(220, 170, 170, 255),
            placeholder
        );
    }

    if (isSelected)
        drawList->AddRect(topLeft, imageMax, IM_COL32(90, 170, 250, 255), 4.0f, 0, 2.5f);

    const std::string name = TruncateToWidth(entry.displayName, tileSize);
    const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
    drawList->AddText(
        ImVec2(topLeft.x + (tileSize - nameSize.x) * 0.5f, topLeft.y + tileSize + 4.0f),
        IM_COL32(220, 220, 220, 255),
        name.c_str()
    );

    ImGui::EndGroup();
}

void AssetBrowser::DrawEntries(SDL_Renderer* renderer) {
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

        if (entry.isDirectory)
            DrawFolderTile(entry, tileSize);
        else
            DrawPngTile(entry, tileSize, renderer);

        ImGui::PopID();

        const float lastTileRight = ImGui::GetItemRectMax().x;
        const float nextTileRight = lastTileRight + ImGui::GetStyle().ItemSpacing.x + tileSize;

        isFirstInRow = (nextTileRight > rightEdge);
    }

    if (!anyVisible)
        ImGui::TextDisabled("%s", "No items match your search.");
}

void AssetBrowser::Draw(SDL_Renderer* renderer) {
    if (rootDirectory.empty())
        return; // SetRootDirectory() hasn't been called yet

    DrawBreadcrumbs();
    ImGui::Spacing();
    DrawSearchBar();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawEntries(renderer);
}

bool AssetBrowser::HasSelection() const {
    return !selectedFile.empty();
}

const std::filesystem::path& AssetBrowser::GetSelectedFile() const {
    return selectedFile;
}

bool AssetBrowser::ConsumePendingConfirmedSelection(std::filesystem::path& outAbsolutePath) {
    if (!pendingConfirmedSelection.has_value())
        return false;

    outAbsolutePath = *pendingConfirmedSelection;
    pendingConfirmedSelection.reset();
    return true;
}