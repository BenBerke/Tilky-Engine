//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Sound/AudioSystem.hpp"

#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/Level.hpp"

namespace {
    std::string MakeAudioSourceName(const ID ownerID) {
        return "entity_" + std::to_string(ownerID) + "_audio";
    }

    // Puts the source on its entity, facing the entity's map forward, and
    // pushes every component setting to OpenAL.
    void ApplySourceSettings(const Level& level, const ComponentAudioSource& audio) {
        if (const ComponentTransform* transform = level.transforms.Get(audio.ownerID)) {
            SoundManager::SetSourcePosition(audio.name, transform->position);
            SoundManager::SetSourceDirection(audio.name, {transform->forward.x, 0.0f, transform->forward.y});
        }

        SoundManager::SetSourcePitch(audio.name, audio.pitch);
        SoundManager::SetSourceGain(audio.name, audio.gain);
        SoundManager::SetSourceLooping(audio.name, audio.looping);
        SoundManager::SetSourceReferenceDistance(audio.name, audio.referenceDistance);
        SoundManager::SetSourceMaxDistance(audio.name, audio.maxDistance);
        SoundManager::SetSourceRollOffFactor(audio.name, audio.rollOffFactor);
        SoundManager::SetSourceInnerConeAngle(audio.name, audio.innerConeAngle);
        SoundManager::SetSourceOuterConeAngle(audio.name, audio.outerConeAngle);
        SoundManager::SetSourceOuterGain(audio.name, audio.outerGain);
    }
}

namespace AudioSystem {
    void Start(Level& level) {
        for (ComponentAudioSource& audio : level.audioSources.components) StartSource(level, audio);

        spdlog::info("Audio system started");
    }

    void StartSource(Level& level, ComponentAudioSource& audio) {
        if (audio.ownerID == static_cast<ID>(-1)) {
            spdlog::error("Audio source has no valid owner");
            return;
        }

        audio.name = MakeAudioSourceName(audio.ownerID);

        if (!SoundManager::CreateSource(audio.name)) {
            spdlog::error("Failed to create audio source: {}", audio.name);
            return;
        }

        ApplySourceSettings(level, audio);

        if (audio.playOnStart && !audio.soundFileName.empty()) SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundFileName);
    }

    void DestroySource(ComponentAudioSource& audio) {
        if (audio.name.empty()) return;

        SoundManager::DestroySource(audio.name);
        audio.name.clear();
    }

    void Update(Level& level) {
        for (ComponentAudioSource& audio : level.audioSources.components) {
            if (audio.ownerID == static_cast<ID>(-1)) {
                spdlog::error("Audio source has no valid owner");
                continue;
            }

            // A component added without StartSource() (an entity copy, for
            // example) gets its OpenAL source here.
            if (audio.name.empty()) {
                audio.name = MakeAudioSourceName(audio.ownerID);
                if (!SoundManager::CreateSource(audio.name)) continue;
            }

            ApplySourceSettings(level, audio);

            if (audio.looping && !audio.soundFileName.empty()) SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundFileName);
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