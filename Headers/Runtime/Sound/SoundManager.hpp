//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_SOUNDMANAGER_H
#define TILKY_ENGINE_SOUNDMANAGER_H

#include <string>

#include <AL/al.h>
#include <AL/alc.h>

#include "Headers/Math/Vector/Vector3.hpp"

namespace SoundManager {
    bool InitializeOpenAL();
    void DestroyOpenAL();

    void GenerateSounds();

    bool CreateSource(const std::string& name);
    void DestroySource(const std::string& sourceName);

    void PlaySoundOnSource(const std::string& sourceName, int soundIndex);
    void PlaySoundOnSourceIfNotPlaying(const std::string& sourceName, int soundIndex);

    // The multiplier for the frequency. 1.0 is normal. 2.0 is an octave higher and double speed.
    void SetSourcePitch(const std::string& sourceName, float pitch);

    // The volume/amplitude multiplier. 1.0 is unity gain; 0.0 is silent.
    void SetSourceGain(const std::string& sourceName, float gain);

    void SetSourcePosition(const std::string& sourceName, Vector3 pos);

    // Set to true to make the sound repeat automatically when it reaches the end.
    void SetSourceLooping(const std::string& sourceName, bool looping);

    // The distance at which the listener experiences the source's full gain.
    void SetSourceReferenceDistance(const std::string& sourceName, float distance);

    // The distance beyond which the sound no longer gets quieter.
    void SetSourceMaxDistance(const std::string& sourceName, float maxDistance);

    // How quickly the sound fades with distance.
    void SetSourceRollOffFactor(const std::string& sourceName, float rollOffFactor);

    // The yaw in degrees where the directional sound is at full gain.
    void SetSourceInnerConeAngle(const std::string& sourceName, float innerConeAngle);

    // The yaw in degrees where the directional sound reaches outer gain.
    void SetSourceOuterConeAngle(const std::string& sourceName, float outerConeAngle);

    // The gain used outside the outer cone.
    void SetSourceOuterGain(const std::string& sourceName, float outerGain);

    // Sets the master volume of the audio context.
    void SetListenerGain(float gain);

    // Sets the 3D position of the listener in the world.
    void SetListenerPosition(Vector3 pos);

    // Sets the listener velocity. Used by OpenAL for Doppler shift.
    void SetListenerVelocity(Vector3 velocity);

    // Sets the listener orientation using the camera/player forward direction.
    void SetListenerOrientation(const Vector3& forward);

    // Sets the global distance attenuation model.
    void SetListenerDistanceModel(ALenum model);

    // Sets the global Doppler effect intensity.
    void SetListenerDopplerFactor(float factor);

    // Sets the speed of sound for Doppler calculations.
    void SetListenerSpeedOfSound(float speed);
}

#endif // TILKY_ENGINE_SOUNDMANAGER_H