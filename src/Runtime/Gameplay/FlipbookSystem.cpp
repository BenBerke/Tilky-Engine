//
// Created by berke on 10/9/2026.
//

#include "Headers/Runtime/Gameplay/FlipbookSystem.hpp"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Objects/FlipbookAsset.hpp"

namespace {
    // A frame event waiting to be fired once every flipbook has advanced.
    // Fired afterwards rather than mid-loop: a handler can add components,
    // which would move the flipbook being advanced out from under the loop.
    struct PendingFlipbookEvent {
        ID entityID;
        std::string functionName;
        std::string frameName;
    };

    // (entity, function) pairs already reported as missing, so a typo is
    // logged once rather than on every loop of the animation.
    std::set<std::pair<ID, std::string>> reportedMissingEvents;

    // Never more frame steps than this in one update. Only matters for a
    // tiny frame duration after a long stall; the rest of the time is dropped.
    constexpr int MAX_FLIPBOOK_STEPS_PER_UPDATE = 256;

    void RewindFlipbook(ComponentFlipbook& flipbook) {
        flipbook.currentFrame = 0;
        flipbook.frameTime = 0.0f;
        flipbook.pingPongDirection = 1;
        flipbook.applyPending = true;
    }

    void QueueFrameEvent(std::vector<PendingFlipbookEvent>& events, const ComponentFlipbook& flipbook,
                         const FlipbookAsset& asset) {
        const FlipbookFrame& frame = asset.frames[flipbook.currentFrame];
        if (frame.eventFunction.empty()) return;

        events.push_back({flipbook.ownerID, frame.eventFunction, frame.name});
    }

    std::vector<PendingFlipbookEvent> pendingFlipbookEvents;
}

namespace FlipbookSystem {
    bool DrivesUISprite(const Level& level, const ComponentFlipbook& flipbook) {
        return level.ui_transforms.Has(flipbook.ownerID);
    }

    ComponentUISprite* FindDrivenUISprite(Level& level, const ComponentFlipbook& flipbook) {
        if (flipbook.spriteInstanceID != INVALID_COMPONENT_INSTANCE_ID) {
            ComponentUISprite* chosen = level.ui_sprites.GetInstance(flipbook.spriteInstanceID);
            if (chosen != nullptr && chosen->ownerID == flipbook.ownerID) return chosen;
        }

        return level.ui_sprites.Get(flipbook.ownerID);
    }

    ComponentSprite* FindDrivenSprite(Level& level, const ComponentFlipbook& flipbook) {
        if (flipbook.spriteInstanceID != INVALID_COMPONENT_INSTANCE_ID) {
            ComponentSprite* chosen = level.sprites.GetInstance(flipbook.spriteInstanceID);
            if (chosen != nullptr && chosen->ownerID == flipbook.ownerID) return chosen;
        }

        return level.sprites.Get(flipbook.ownerID);
    }

    void Start(Level& level) {
        reportedMissingEvents.clear();

        // Reload from disk, so files changed outside the editor (or missing
        // last time) are picked up.
        FlipbookLibrary::Clear();

        for (ComponentFlipbook& flipbook : level.flipbooks.components) {
            RewindFlipbook(flipbook);
            flipbook.playing = false;
            flipbook.eventPending = false;

            const FlipbookAsset* asset = FlipbookLibrary::Get(flipbook.flipbookFileName);
            if (asset == nullptr || !asset->playOnStart) continue;

            flipbook.playing = true;
            flipbook.eventPending = true;
        }
    }

    void Update(Level& level, const float deltaTime, const EventDispatcher& dispatch) {
        pendingFlipbookEvents.clear();

        for (ComponentFlipbook& flipbook : level.flipbooks.components) {
            const FlipbookAsset* asset = FlipbookLibrary::Get(flipbook.flipbookFileName);
            if (asset == nullptr || asset->frames.empty()) continue;

            const int frameCount = static_cast<int>(asset->frames.size());

            // The file can change under a running flipbook (switched from Lua,
            // or saved shorter in the editor).
            if (flipbook.currentFrame < 0 || flipbook.currentFrame >= frameCount) RewindFlipbook(flipbook);

            if (flipbook.eventPending) {
                flipbook.eventPending = false;
                QueueFrameEvent(pendingFlipbookEvents, flipbook, *asset);
            }

            if (flipbook.playing && flipbook.speed > 0.0f) {
                flipbook.frameTime += deltaTime * flipbook.speed;

                for (int steps = 0; flipbook.frameTime >= asset->FrameDuration(flipbook.currentFrame); ++steps) {
                    if (steps >= MAX_FLIPBOOK_STEPS_PER_UPDATE) {
                        flipbook.frameTime = 0.0f;
                        break;
                    }

                    flipbook.frameTime -= asset->FrameDuration(flipbook.currentFrame);

                    if (!asset->StepFrame(flipbook.currentFrame, flipbook.pingPongDirection)) {
                        flipbook.playing = false;
                        flipbook.frameTime = 0.0f;
                        break;
                    }

                    flipbook.applyPending = true;

                    // Every frame passed fires, not just the one landed on,
                    // so a frame skipped by a slow update still gets its event.
                    QueueFrameEvent(pendingFlipbookEvents, flipbook, *asset);
                }
            }

            if (flipbook.applyPending) {
                const FlipbookFrame& frame = asset->frames[flipbook.currentFrame];

                // A UI Sprite has one picture: the frame's first (N) slot.
                if (DrivesUISprite(level, flipbook)) {
                    if (ComponentUISprite* uiSprite = FindDrivenUISprite(level, flipbook)) uiSprite->texture = frame.textures[0];
                }
                else if (ComponentSprite* sprite = FindDrivenSprite(level, flipbook)) {
                    sprite->textureFileNames = frame.textures;
                }

                flipbook.applyPending = false;
            }
        }

        if (!dispatch) return;

        for (const PendingFlipbookEvent& event : pendingFlipbookEvents) {
            if (dispatch(event.entityID, event.functionName, event.frameName)) continue;

            if (reportedMissingEvents.emplace(event.entityID, event.functionName).second)
                spdlog::warn(
                    "Flipbook frame '{}' on entity {} calls '{}', but none of the entity's scripts defines it",
                    event.frameName,
                    event.entityID,
                    event.functionName
                );
        }
    }

    void Play(ComponentFlipbook& flipbook, const std::string& fileName, const bool restart) {
        if (!fileName.empty() && fileName != flipbook.flipbookFileName) {
            flipbook.flipbookFileName = fileName;
            RewindFlipbook(flipbook);
            flipbook.playing = true;
            flipbook.eventPending = true;
            return;
        }

        if (restart) {
            RewindFlipbook(flipbook);
            flipbook.playing = true;
            flipbook.eventPending = true;
            return;
        }

        if (flipbook.playing) return;

        // A Once flipbook that already finished plays again from the start.
        if (const FlipbookAsset* asset = FlipbookLibrary::Get(flipbook.flipbookFileName);
            asset != nullptr && asset->loopMode == FlipbookLoopMode::Once &&
            flipbook.currentFrame >= static_cast<int>(asset->frames.size()) - 1)
            RewindFlipbook(flipbook);

        // From a full stop the first frame is being reached for the first
        // time, so it fires. Resuming mid-animation doesn't re-fire.
        if (flipbook.currentFrame == 0 && flipbook.frameTime == 0.0f) flipbook.eventPending = true;

        flipbook.playing = true;
    }

    void Pause(ComponentFlipbook& flipbook) {
        flipbook.playing = false;
    }

    void Resume(ComponentFlipbook& flipbook) {
        flipbook.playing = true;
    }

    void Stop(ComponentFlipbook& flipbook) {
        flipbook.playing = false;
        flipbook.eventPending = false;
        RewindFlipbook(flipbook);
    }

    void SetFlipbookFileName(ComponentFlipbook& flipbook, const std::string& fileName) {
        if (fileName == flipbook.flipbookFileName) return;

        flipbook.flipbookFileName = fileName;
        flipbook.eventPending = false;
        RewindFlipbook(flipbook);
    }

    bool SetFrame(ComponentFlipbook& flipbook, const std::string& frameName) {
        const FlipbookAsset* asset = FlipbookLibrary::Get(flipbook.flipbookFileName);
        if (asset == nullptr) return false;

        const int index = asset->FindFrame(frameName);
        if (index == -1) return false;

        flipbook.currentFrame = index;
        flipbook.frameTime = 0.0f;
        flipbook.applyPending = true;
        flipbook.eventPending = false;
        return true;
    }

    std::string GetFrameName(const ComponentFlipbook& flipbook) {
        const FlipbookAsset* asset = FlipbookLibrary::Get(flipbook.flipbookFileName);
        if (asset == nullptr || asset->frames.empty()) return {};

        const int index = std::clamp(flipbook.currentFrame, 0, static_cast<int>(asset->frames.size()) - 1);
        return asset->frames[index].name;
    }
}
