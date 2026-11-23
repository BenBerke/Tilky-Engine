#pragma once

#include <string>

#include <imgui.h>

class IRenderer;

// A lazy, filename-keyed texture loader for the map and UI editors: the
// canvas and every preview (the Asset Browser's thumbnails, the texture-field
// boxes). A texture is identified by its path relative to the project's
// Assets folder, which is what Wall/Sector/ComponentSprite fields store.
//
// Loading is asynchronous: a small pool of worker threads decodes image
// files, and Update() hands the decoded images to the renderer on the main
// thread (GL only allows that there), which keeps them with the game's own
// textures. Until a texture is ready, Get() returns nullptr, so it draws the
// same as a missing texture for a few frames. One the renderer already has
// (loaded by the game or the runtime editor) is ready at once.
namespace EditorTextureCache {
    struct Texture {
        ImTextureID imguiID{};
        unsigned glID = 0;
        int width = 0;
        int height = 0;
    };

    // The renderer textures are made in. Call before Get()/Update().
    void SetRenderer(IRenderer* renderer);

    // The texture for a file under the project's Assets folder, given its
    // path relative to it (e.g. "Textures/brick01.png"). The first request
    // queues the file for loading and returns nullptr; later calls return the
    // texture once Update() has made it. Returns nullptr - never crashes - if
    // the file is missing, unreadable, or fails to decode; a failed load is
    // remembered too, so a broken reference isn't retried every frame. The
    // pointer stays valid until Invalidate() or Destroy().
    const Texture* Get(const std::string& relativeFileName);

    // Makes textures for images the workers have finished decoding, until
    // about 4 ms have passed (always at least one). Call once per frame on
    // the main thread, with the renderer's GL context current.
    void Update();

    // Drops one cached texture so the next Get() call asks again - e.g.
    // after the user replaces a file via drag-and-drop import. A load
    // already in flight for it is discarded when it finishes. (The renderer
    // reloads a changed file's texture by itself.)
    void Invalidate(const std::string& relativeFileName);

    // Stops the worker threads and frees every decoded image. The textures
    // belong to the renderer, which frees them. Call once, before the
    // renderer shuts down.
    void Destroy();
}
