#pragma once

#include <string>

#include <SDL3/SDL_render.h>

// A simple lazy, filename-keyed texture cache for the editor's own preview
// rendering (the Asset Browser's thumbnails, and every wall/sector/sprite
// texture-field preview box). There is no permanent index and no manifest
// file anymore - a texture is identified purely by its path relative to
// the project's Textures folder, which is exactly what the engine now
// stores in Wall/Sector/ComponentSprite fields.
namespace EditorTextureCache {
    // Returns a cached SDL_Texture* for a file under the project's
    // Textures folder, given its path relative to that folder (e.g.
    // "brick01.png" or "ui/icon.png"). Loads and caches lazily on first
    // request. Returns nullptr - never crashes - if the file is missing,
    // unreadable, or fails to decode; a failed load is cached too, so a
    // broken reference isn't retried every frame.
    SDL_Texture* Get(SDL_Renderer* renderer, const std::string& relativeFileName);

    // Drops one cached texture so the next Get() call reloads it from
    // disk - e.g. after the user replaces a file via drag-and-drop import.
    void Invalidate(const std::string& relativeFileName);

    // Frees every cached texture. Call once, BEFORE destroying the
    // SDL_Renderer these textures were created against.
    void Destroy();
}