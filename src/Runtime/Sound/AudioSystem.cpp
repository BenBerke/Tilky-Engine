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
}

namespace AudioSystem {
    void Start(Level& level) {
        for (ComponentAudioSource& audio : level.audioSources.components) {
            if (audio.ownerID == static_cast<ID>(-1)) {
                spdlog::error("Audio source has no valid owner");
                continue;
            }

            audio.name = MakeAudioSourceName(audio.ownerID);

            if (!SoundManager::CreateSource(audio.name)) {
                spdlog::error("Failed to create audio source: {}", audio.name);
                continue;
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

            const ComponentTransform* transform = level.transforms.Get(audio.ownerID);

            if (transform != nullptr) SoundManager::SetSourcePosition(audio.name, transform->position);
            if (audio.playOnStart && !audio.soundFileName.empty()) SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundFileName);
        }

        spdlog::info("Audio system started");
    }

    void Update(Level& level) {
        for (ComponentAudioSource& audio : level.audioSources.components) {
            if (audio.ownerID == static_cast<ID>(-1)) {
                spdlog::error("Audio source has no valid owner");
                continue;
            }

            if (audio.name.empty()) audio.name = MakeAudioSourceName(audio.ownerID);

            ComponentTransform* transform = level.transforms.Get(audio.ownerID);

            if (transform != nullptr) SoundManager::SetSourcePosition(audio.name, transform->position);

            SoundManager::SetSourcePitch(audio.name, audio.pitch);
            SoundManager::SetSourceGain(audio.name, audio.gain);
            SoundManager::SetSourceLooping(audio.name, audio.looping);
            SoundManager::SetSourceReferenceDistance(audio.name, audio.referenceDistance);
            SoundManager::SetSourceMaxDistance(audio.name, audio.maxDistance);
            SoundManager::SetSourceRollOffFactor(audio.name, audio.rollOffFactor);
            SoundManager::SetSourceInnerConeAngle(audio.name, audio.innerConeAngle);
            SoundManager::SetSourceOuterConeAngle(audio.name, audio.outerConeAngle);
            SoundManager::SetSourceOuterGain(audio.name, audio.outerGain);

            if (audio.looping && !audio.soundFileName.empty()) SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundFileName);
        }
    }

    void Shutdown(Level& level) {
        for (ComponentAudioSource& audio : level.audioSources.components) {
            if (!audio.name.empty()) {
                SoundManager::DestroySource(audio.name);
                audio.name.clear();
            }
        }
    }

    void ApplyListenerSettings(const Level& level) {
        const ListenerSettings& settings = level.listenerSettings;

        SoundManager::SetListenerGain(settings.masterGain);
        SoundManager::SetListenerDopplerFactor(settings.dopplerFactor);
        SoundManager::SetListenerSpeedOfSound(settings.speedOfSound);
        SoundManager::SetListenerDistanceModel(settings.distanceModel);
    }
}