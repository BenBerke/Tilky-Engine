//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Sound/AudioSystem.hpp"

#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"

namespace {
    // One OpenAL source per component: an entity can have several.
    std::string MakeAudioSourceName(const ComponentAudioSource& audio) {
        return "entity_" + std::to_string(audio.ownerID) + "_audio_" + std::to_string(audio.instanceID);
    }

    // Puts the source on its entity (plus its offset), facing the rotation's local +Z, and
    // pushes the component's settings to OpenAL - only the ones that changed
    // since the last send (see ComponentAudioSource::applied).
    void ApplySourceSettings(const Level& level, const ComponentAudioSource& audio) {
        ComponentAudioSource::AppliedSettings& applied = audio.applied;
        const bool sendAll = !applied.valid;

        const auto send = [&](const auto value, auto& last, void (*setter)(const std::string&, decltype(value))) {
            if (!sendAll && value == last) return;
            setter(audio.name, value);
            last = value;
        };

        if (const ComponentTransform* transform = level.transforms.Get(audio.ownerID)) {
            send(transform->LocalToWorld(audio.offset), applied.position, &SoundManager::SetSourcePosition);
            send(QuaternionMath::Rotate(transform->rotation, QuaternionMath::LocalForward()), applied.direction, &SoundManager::SetSourceDirection);
        }

        send(audio.pitch, applied.pitch, &SoundManager::SetSourcePitch);
        send(audio.gain, applied.gain, &SoundManager::SetSourceGain);
        send(audio.looping, applied.looping, &SoundManager::SetSourceLooping);
        send(audio.referenceDistance, applied.referenceDistance, &SoundManager::SetSourceReferenceDistance);
        send(audio.maxDistance, applied.maxDistance, &SoundManager::SetSourceMaxDistance);
        send(audio.rollOffFactor, applied.rollOffFactor, &SoundManager::SetSourceRollOffFactor);
        send(audio.innerConeAngle, applied.innerConeAngle, &SoundManager::SetSourceInnerConeAngle);
        send(audio.outerConeAngle, applied.outerConeAngle, &SoundManager::SetSourceOuterConeAngle);
        send(audio.outerGain, applied.outerGain, &SoundManager::SetSourceOuterGain);

        applied.valid = true;
    }

    // The listener hears from the active camera: its entity's position (at
    // eye height when that entity is the active player), facing where it
    // looks. With no active camera the listener stays where it was.
    void UpdateListener(const Level& level) {
        const ComponentCamera* camera = nullptr;

        for (const ComponentCamera& candidate : level.cameras.components) {
            if (!candidate.isActive) continue;
            camera = &candidate;
            break;
        }

        if (camera == nullptr) return;

        const ComponentTransform* transform = level.transforms.Get(camera->ownerID);
        if (transform == nullptr) return;

        Vector3 position = transform->position;

        const ComponentPlayerController* controller = level.playerControllers.Get(camera->ownerID);
        if (controller != nullptr && controller->isActive) position.y += controller->eyeHeight;

        const ComponentRigidbody* rigidbody = level.rigidbodies.Get(camera->ownerID);

        SoundManager::SetListenerPosition(position);
        SoundManager::SetListenerOrientation(CameraSystem::GetCameraForwardEngineSpace(camera->yaw, camera->pitch));
        SoundManager::SetListenerVelocity(rigidbody != nullptr ? rigidbody->velocity : Vector3{0.0f, 0.0f, 0.0f});
    }
}

namespace AudioSystem {
    void Start(Level& level) {
        UpdateListener(level);

        for (ComponentAudioSource& audio : level.audioSources.components) StartSource(level, audio);

        spdlog::info("Audio system started");
    }

    void StartSource(Level& level, ComponentAudioSource& audio) {
        if (audio.ownerID == static_cast<ID>(-1)) {
            spdlog::error("Audio source has no valid owner");
            return;
        }

        audio.name = MakeAudioSourceName(audio);

        if (!SoundManager::CreateSource(audio.name)) {
            spdlog::error("Failed to create audio source: {}", audio.name);
            return;
        }

        audio.applied.valid = false;

        ApplySourceSettings(level, audio);

        if (audio.playOnStart && !audio.soundFileName.empty()) SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundFileName);
    }

    void DestroySource(ComponentAudioSource& audio) {
        if (audio.name.empty()) return;

        SoundManager::DestroySource(audio.name);
        audio.name.clear();
    }

    void Update(Level& level) {
        UpdateListener(level);

        for (ComponentAudioSource& audio : level.audioSources.components) {
            if (audio.ownerID == static_cast<ID>(-1)) {
                spdlog::error("Audio source has no valid owner");
                continue;
            }

            // A component added without StartSource() (an entity copy, for
            // example) gets its OpenAL source here.
            if (audio.name.empty()) {
                audio.name = MakeAudioSourceName(audio);
                if (!SoundManager::CreateSource(audio.name)) continue;
                audio.applied.valid = false;
            }

            ApplySourceSettings(level, audio);

            if (audio.looping && !audio.isHeld && !audio.soundFileName.empty()) SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundFileName);
        }
    }

    void Shutdown(Level& level) {
        for (ComponentAudioSource& audio : level.audioSources.components) DestroySource(audio);
    }

    void ApplyListenerSettings(const Level& level) {
        const ListenerSettings& settings = level.listenerSettings;

        SoundManager::SetListenerGain(settings.masterGain);
        SoundManager::SetListenerDopplerFactor(settings.dopplerFactor);
        SoundManager::SetListenerSpeedOfSound(settings.speedOfSound);
        SoundManager::SetListenerDistanceModel(settings.distanceModel);
    }
}