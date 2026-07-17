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
             SoundManager::SetSourceLooping(audio.name, audio.looping);;

             const ComponentTransform* transform = level.transforms.Get(audio.ownerID);

             if (transform != nullptr)
                 SoundManager::SetSourcePosition(audio.name, {transform->position.x, transform->position.y, transform->position.z});


             if (audio.playOnStart && audio.soundIndex >= 0)
                 SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundIndex);
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

            if (transform != nullptr)
                SoundManager::SetSourcePosition(audio.name, {transform->position.x, transform->position.y, transform->position.z});

            SoundManager::SetSourcePitch(audio.name, audio.pitch);
            SoundManager::SetSourceGain(audio.name, audio.gain);
            SoundManager::SetSourceLooping(audio.name, audio.looping);

            if (audio.looping && audio.soundIndex >= 0)
                SoundManager::PlaySoundOnSourceIfNotPlaying(audio.name, audio.soundIndex);
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