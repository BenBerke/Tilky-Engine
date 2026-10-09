//
// Created by berke on 10/9/2026.
//

#ifndef TILKY_ENGINE_FLIPBOOKSYSTEM_HPP
#define TILKY_ENGINE_FLIPBOOKSYSTEM_HPP

#include <functional>
#include <string>

#include "Headers/Objects/Level.hpp"

/// Plays every ComponentFlipbook: advances frames and copies the current
/// frame's textures into the driven sprite - a Sprite on a world entity, or
/// a UI Sprite (slot 0 only) on a UI entity. Only runs during Play and
/// Standalone (LevelSystem), never in the editors, so the sprite textures it
/// overwrites are never saved - Play works on a copy of the level.
namespace FlipbookSystem {
    // Calls `functionName(frameName)` on the scripts of `entityID`. Returns
    // false if none of them defines the function.
    using EventDispatcher = std::function<bool(ID entityID, const std::string& functionName, const std::string& frameName)>;

    // Resets every flipbook, shows its first frame and starts the ones whose
    // file has playOnStart. Call once the level's scripts have started.
    void Start(Level& level);

    // After scripts, before rendering. Fires frame events through `dispatch`
    // once every flipbook has been advanced.
    void Update(Level& level, float deltaTime, const EventDispatcher& dispatch);

    // True if the flipbook's owner is a UI entity (it has a UITransform), so
    // it drives a UI Sprite instead of a Sprite.
    bool DrivesUISprite(const Level& level, const ComponentFlipbook& flipbook);

    // The sprite `flipbook` drives: its chosen one, or the owner's first.
    // Only one of these applies, depending on DrivesUISprite.
    ComponentSprite* FindDrivenSprite(Level& level, const ComponentFlipbook& flipbook);
    ComponentUISprite* FindDrivenUISprite(Level& level, const ComponentFlipbook& flipbook);

    // --- Playback control, shared by the Lua API ---------------------------

    // Plays `fileName` (empty = the current file). Does nothing if that file
    // is already playing, unless `restart`. A different file starts from its
    // first frame; the current one resumes where it is (a finished Once
    // flipbook starts over).
    void Play(ComponentFlipbook& flipbook, const std::string& fileName, bool restart);
    void Pause(ComponentFlipbook& flipbook);
    void Resume(ComponentFlipbook& flipbook);

    // Stops and rewinds to the first frame, which the sprite then shows.
    void Stop(ComponentFlipbook& flipbook);

    // Switches the file without changing whether it plays. Rewinds to the
    // first frame.
    void SetFlipbookFileName(ComponentFlipbook& flipbook, const std::string& fileName);

    // Jumps to the frame called `frameName` without firing its event. False
    // (and nothing changes) if there's no such frame.
    bool SetFrame(ComponentFlipbook& flipbook, const std::string& frameName);

    // Name of the frame being shown, or empty if there's no loaded flipbook.
    std::string GetFrameName(const ComponentFlipbook& flipbook);
}

#endif //TILKY_ENGINE_FLIPBOOKSYSTEM_HPP
