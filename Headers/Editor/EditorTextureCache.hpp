#pragma once

#include <string>

#include <SDL3/SDL_render.h>

// A simple lazy, filename-keyed texture cache for the editor's own preview
// rendering (the Asset Browser's thumbnails, and every wall/sector/sprite
// texture-field preview box). There is no permanent index and no manifest
// file anymore - a texture is identified purely by its path relative to
// the project's Textures folder, which is exactly what the engine now
// stores in Wall/Sector/ComponentSprite fields.
//
// Loading is asynchronous: a small pool of worker threads decodes image
// files, and Update() turns the decoded images into SDL textures on the main
// thread (SDL only allows that there). Until a texture is ready, Get()
// returns nullptr, so it draws the same as a missing texture for a few frames.
namespace EditorTextureCache {
    // Returns a cached SDL_Texture* for a file under the project's
    // Textures folder, given its path relative to that folder (e.g.
    // "brick01.png" or "ui/icon.png"). The first request queues the file for
    // loading and returns nullptr; later calls return the texture once
    // Update() has created it. Returns nullptr - never crashes - if the file
    // is missing, unreadable, or fails to decode; a failed load is cached too,
    // so a broken reference isn't retried every frame.
    SDL_Texture* Get(SDL_Renderer* renderer, const std::string& relativeFileName);

    // Creates textures for images the workers have finished decoding, until
    // about 4 ms have passed (always at least one). Call once per frame on
    // the main thread, with the renderer passed to Get().
    void Update(SDL_Renderer* renderer);

    // Drops one cached texture so the next Get() call reloads it from
    // disk - e.g. after the user replaces a file via drag-and-drop import.
    // A load already in flight for it is discarded when it finishes.
    void Invalidate(const std::string& relativeFileName);

    // Stops the worker threads and frees every cached texture and decoded
    // image. Call once, BEFORE destroying the SDL_Renderer these textures
    // were created against.
    void Destroy();
}
