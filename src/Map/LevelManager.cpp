//
// Created by berke on 5/2/2026.
//

#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace LevelManager {
    std::vector<Level> loadedLevels;
    int currentLevelIndex = -1;

    namespace {
        Level* currentLevelOverride = nullptr;
    }

    bool HasCurrentLevel() {
        return currentLevelOverride != nullptr ||
               (currentLevelIndex >= 0 && currentLevelIndex < static_cast<int>(loadedLevels.size()));
    }

    Level& CurrentLevel() { return currentLevelOverride != nullptr ? *currentLevelOverride : loadedLevels[currentLevelIndex]; }

    ScopedCurrentLevel::ScopedCurrentLevel(Level& level) : previous(currentLevelOverride) { currentLevelOverride = &level; }
    ScopedCurrentLevel::~ScopedCurrentLevel() { currentLevelOverride = previous; }

    void ClearLoadedLevels() {
        loadedLevels.clear();
        currentLevelIndex = -1;
    }

    bool LoadLevelFromFile(const fs::path& levelFile, LevelSerialization::LevelExtraData* outExtraData) {
        Level loadedLevel;
        std::string errorMessage;

        if (!LevelSerialization::LoadLevelFromFile(levelFile, loadedLevel, outExtraData, &errorMessage)) {
            std::cerr << errorMessage << "\n";
            return false;
        }

        ClearLoadedLevels();

        loadedLevels.push_back(std::move(loadedLevel));
        currentLevelIndex = 0;

        return true;
    }

    bool LoadLevelByName(const std::string& levelName) {
        std::string errorMessage;
        const fs::path levelPath = LevelSerialization::FindLevelPath(levelName, &errorMessage);

        if (levelPath.empty()) {
            std::cerr << errorMessage << "\n";
            return false;
        }

        return LoadLevelFromFile(levelPath);
    }

    bool LoadStartLevel(LevelSerialization::LevelExtraData* outExtraData) {
        if (const std::string startLevel = ProjectManager::GetStartLevelName(); !startLevel.empty()) {
            std::string errorMessage;
            const fs::path levelPath = LevelSerialization::FindLevelPath(startLevel, &errorMessage);

            if (!levelPath.empty()) return LoadLevelFromFile(levelPath, outExtraData);

            std::cerr << "Start level \"" << startLevel << "\" could not be found (" << errorMessage
                      << "). Using the first level instead.\n";
        }

        const std::vector<fs::path> levelFiles = LevelSerialization::ListLevelFiles();

        if (levelFiles.empty()) {
            std::cerr << "No .bson level files found in: "
                      << ProjectManager::GetAssetsPath().string()
                      << "\n";
            return false;
        }

        return LoadLevelFromFile(levelFiles.front(), outExtraData);
    }

    // The reason its here is for legacy reasons
    // todo TILKY_TODO put this in a place it makes sense
    void TriangulateCurrentLevelSectors() {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (Sector& sector : level.sectors) {
            sector.triangles.clear();

            if (sector.vertices.size() < 3) continue;

            sector.triangles = Geometry::Triangulate(sector.vertices, sector.innerLoops);
        }
    }

    // Calls f(dispatch) for every dispatch in the level: walls, linedefs,
    // sectors, entities and the level's own.
    template <typename F>
    void ForEachDispatch(Level& level, F&& f) {
        for (Wall& wall : level.walls) for (Dispatch& dispatch : wall.dispatches) f(dispatch);
        for (Linedef& linedef : level.linedefs) for (Dispatch& dispatch : linedef.dispatches) f(dispatch);
        for (Sector& sector : level.sectors) for (Dispatch& dispatch : sector.dispatches) f(dispatch);
        for (Entity& entity : level.entities) for (Dispatch& dispatch : entity.dispatches) f(dispatch);
        for (Dispatch& dispatch : level.worldSettings.dispatches) f(dispatch);
    }

    void RenameTextureReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (Wall& wall : level.walls) {
            if (wall.top.texture == oldReference) wall.top.texture = newReference;
            if (wall.bottom.texture == oldReference) wall.bottom.texture = newReference;
            if (wall.middle.texture == oldReference) wall.middle.texture = newReference;
        }

        for (Sector& sector : level.sectors)
            for (SectorFloor& floor : sector.floors) {
                if (floor.floor.texture == oldReference) floor.floor.texture = newReference;
                if (floor.ceiling.texture == oldReference) floor.ceiling.texture = newReference;
            }

        for (ComponentSprite& sprite : level.sprites.components)
            for (std::string& fileName : sprite.textureFileNames)
                if (fileName == oldReference) fileName = newReference;

        for (ComponentUISprite& uiSprite : level.ui_sprites.components)
            if (uiSprite.texture == oldReference) uiSprite.texture = newReference;

        ForEachDispatch(level, [&](Dispatch& dispatch) {
            if (auto* line = std::get_if<PayloadDispatchLineSetTexture>(&dispatch.payload); line && line->texture == oldReference)
                line->texture = newReference;
            if (auto* sector = std::get_if<PayloadDispatchSectorSetTexture>(&dispatch.payload); sector && sector->texture == oldReference)
                sector->texture = newReference;
        });
    }

    void RenameSoundReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (ComponentAudioSource& audioSource : level.audioSources.components)
            if (audioSource.soundFileName == oldReference) audioSource.soundFileName = newReference;

        ForEachDispatch(level, [&](Dispatch& dispatch) {
            if (auto* play = std::get_if<PayloadDispatchPlaySound>(&dispatch.payload); play && play->sound == oldReference)
                play->sound = newReference;
        });
    }

    void RenameScriptReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (ComponentScript& script : level.scripts.components)
            if (script.fileName == oldReference) script.fileName = newReference;

        for (Sector& sector : level.sectors)
            for (SectorScript& script : sector.scripts)
                if (script.fileName == oldReference) script.fileName = newReference;
    }

    void RenameModelReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (ComponentModel& model : level.models.components)
            if (model.fileName == oldReference) model.fileName = newReference;
    }

    void RenameFontReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (ComponentUIText& text : level.ui_texts.components)
            if (text.font == oldReference) text.font = newReference;
    }

    void RenamePrefabReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (Entity& entity : level.entities)
            if (entity.prefabLink.prefab == oldReference) entity.prefabLink.prefab = newReference;

        // Entity fields holding the prefab.
        const auto renameInScript = [&](ScriptAttachmentData& script) {
            for (auto& [name, value] : script.publicValues)
                if (EntityRefValue* entityRef = std::get_if<EntityRefValue>(&value); entityRef != nullptr && entityRef->prefab == oldReference)
                    entityRef->prefab = newReference;
        };

        for (ComponentScript& script : level.scripts.components) renameInScript(script);

        for (Sector& sector : level.sectors)
            for (SectorScript& script : sector.scripts) renameInScript(script);

        ForEachDispatch(level, [&](Dispatch& dispatch) {
            if (auto* spawn = std::get_if<PayloadDispatchSpawnPrefab>(&dispatch.payload); spawn && spawn->prefab == oldReference)
                spawn->prefab = newReference;
        });
    }

    void RenameFlipbookReference(const std::string& oldReference, const std::string& newReference) {
        if (!HasCurrentLevel()) return;

        Level& level = CurrentLevel();

        for (ComponentFlipbook& flipbook : level.flipbooks.components)
            if (flipbook.flipbookFileName == oldReference) flipbook.flipbookFileName = newReference;

        // Scripts hold flipbooks in FlipbookAsset fields. Only a path ending
        // in .fpk can be one, so a texture field with the same text can't match.
        const auto renameInScript = [&](ScriptAttachmentData& script) {
            for (auto& [name, value] : script.publicValues)
                if (AssetRefValue* asset = std::get_if<AssetRefValue>(&value); asset != nullptr && asset->path == oldReference)
                    asset->path = newReference;
        };

        for (ComponentScript& script : level.scripts.components) renameInScript(script);

        for (Sector& sector : level.sectors)
            for (SectorScript& script : sector.scripts) renameInScript(script);
    }
}