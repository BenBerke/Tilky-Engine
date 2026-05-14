//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_SOUNDMANAGER_H
#define TILKY_ENGINE_SOUNDMANAGER_H

#include <AL/al.h>
#include <AL/alc.h>

#include <chrono>

struct Vector3;

namespace SoundManager {
    bool InitializeOpenAL();
    void DestroyOpenAL();

    void PlaySoundOnSource(const std::string& sourceName, int soundIndex);

    void PlaySoundOnSourceIfNotPlaying(const std::string& sourceName, int soundIndex);

    bool CreateSource(const std::string& name);

    void DestroySource(const std::string& sourceName);

    // The multiplier for the frequency. 1.0 is normal. 2.0 is an octave higher and double speed.
    void SetSourcePitch(const std::string& sourceName, float pitch);

    // The volume/amplitude multiplier. 1.0 is unity gain; 0.0 is silent.
    void SetSourceGain(const std::string& sourceName, float gain);

    void SetSourcePosition(const std::string& sourceName, Vector3 pos);

    // Set to AL_TRUE to make the sound repeat automatically when it reaches the end.
    void SetSourceLooping(const std::string& sourceName, bool looping);

    // The distance at which the listener will experience the maximum gain (AL_GAIN).
    void SetSourceReferenceDistance(const std::string& sourceName, float distance);

    // The distance beyond which the sound will no longer get any quieter (it stays at a constant low volume).
    void SetSourceMaxDistance(const std::string& sourceName, float maxDistance);

    // How fast the sound fades. A factor of 1.0 is physically realistic. 0.0 disables distance attenuation entirely.
    void SetSourceRollOffFactor(const std::string& sourceName, float rollOffFactor);

    // The angle (in degrees) of the "inner cone" where the sound is at its full gain.
    void SetSourceInnerConeAngle(const std::string& sourceName, float innerConeAngle);

    // The angle (in degrees) of the "outer cone."
    void SetSourceOuterConeAngle(const std::string& sourceName, float outerConeAngle);

    // The volume of the sound when the listener is outside the outer cone.
    void SetSourceOuterGain(const std::string& sourceName, float outerGain);

    void SetListenerPosition(Vector3 pos);

    void SetListenerVelocity(Vector3 velocity);

    void SetListenerOrientation(float angleRadians);

    /**
 * Sets the master volume of the audio context.
 * @param gain: 1.0 is unity, 0.0 is silent. Values > 1.0 increase volume.
 */
    void SetListenerGain(float gain);

    /**
     * Sets the 3D position of the listener in the world.
     */
    void SetListenerPosition(const Vector3 pos);

    /**
     * Sets the velocity of the listener. Used by OpenAL to calculate Doppler shift.
     */
    void SetListenerVelocity(const Vector3 velocity);

    /**
     * Sets the listener orientation using a single angle (2D plane).
     * @param angleRadians: The rotation around the Y-axis.
     */
    void SetListenerOrientation(const float angleRadians);

    /**
     * Sets the listener orientation using 3D vectors.
     * @param forward: The "at" vector (where the listener is looking).
     * @param up: The "up" vector (top of the listener's head).
     */
    void SetListenerOrientation(const Vector3 forward, const Vector3 up);

    /**
     * Sets the global distance attenuation model (e.g., AL_INVERSE_DISTANCE_CLAMPED).
     */
    void SetDistanceModel(ALenum model);

    /**
     * Sets the global Doppler effect intensity. 1.0 is normal, 0.0 is off.
     */
    void SetDopplerFactor(float factor);

    /**
     * Sets the speed of sound for Doppler calculations. Default is 343.3.
     */
    void SetSpeedOfSound(float speed);
}
#endif //TILKY_ENGINE_SOUNDMANAGER_H