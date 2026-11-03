#ifndef TILKY_ENGINE_PARTICLESYSTEM_HPP
#define TILKY_ENGINE_PARTICLESYSTEM_HPP

#include <memory>
#include <string>
#include <vector>

#include "Headers/Objects/Components.hpp"

struct Level;

// Particles and stains: what Particle Emitters (ComponentParticleEmitter) throw
// out. Neither is an entity: they have no scripts, events or colliders, so a
// burst costs a copy of the emitter's settings, not a registry rebuild. They
// belong to the running level and are forgotten when it stops. The renderer
// draws them in its sprite batch, stains first (OpenGL::BuildGpuSprites).
namespace ParticleSystem {
    // What every particle of one burst shares: the emitter's settings at the
    // moment it fired, so changing or destroying the emitter leaves live
    // particles alone.
    struct Style {
        std::vector<std::string> textures;
        bool animate = false;
        float gravity = 0.0f;
        float drag = 0.0f;
        float sizeEnd = 1.0f;
        Vector4 colorStart;
        Vector4 colorEnd;
        ParticleCollision collision = ParticleCollision::NONE;
        float bounciness = 0.0f;
        std::vector<std::string> stainTextures;
        float stainChance = 0.0f;
        float stainSizeMin = 0.0f;
        float stainSizeMax = 0.0f;
        float stainLifetime = 0.0f;
        Vector4 stainColor;
        std::string noStainTag;
    };

    struct Particle {
        std::shared_ptr<const Style> style;
        Vector3 position; // the sprite's centre
        Vector3 velocity;
        float age = 0.0f;
        float lifetime = 1.0f;
        float size = 1.0f;
        int textureIndex = 0; // into style->textures, ignored when it animates
        int sectorIndex = -1;
        bool resting = false; // a bouncing particle that came to rest on a floor
    };

    struct Stain {
        Vector3 bottomCentre; // of the flat sprite
        Quaternion rotation;
        std::string texture;
        Vector4 color;
        float size = 1.0f;
        float age = 0.0f;
        float lifetime = 0.0f; // 0 = until the cap removes it
        int sectorIndex = -1;  // whose light it takes

        // What it's stuck to, so it rides a door or a lift.
        enum class Follows { NOTHING, FLOOR, CEILING, LOWEST_CEILING, HIGHEST_FLOOR } follows = Follows::NOTHING;
        ID sectorA = INVALID_ID;
        ID sectorB = INVALID_ID;
        float followedHeight = 0.0f;
    };

    // Forgets everything from the last level.
    void Start();
    void Stop();

    // Scaled time, so all of it holds while timeScale is 0. After scripts, so
    // what they emit this frame moves this frame.
    void Update(Level& level, float deltaTime);

    // Fires one burst. `count` below 0 uses the emitter's burst numbers, which
    // `scale` then multiplies. `direction` zero uses the emitter's own, turned
    // with the entity's rotation when there is one. Returns how many it made.
    int Emit(Level& level, const ComponentParticleEmitter& emitter, const Vector3& position, const Vector3& direction,
             int count = -1, float scale = 1.0f);

    // Emit at the emitter's own place. A prefab's emitters (Game.EmitParticles)
    // pass their own place instead.
    int EmitAtOwner(Level& level, const ComponentParticleEmitter& emitter, int count = -1);

    // Leaves a stain where a line first meets a wall, floor or ceiling. False
    // if it meets nothing, or the surface has no room for it, or its sector has
    // `noStainTag`. Entities don't stop the line.
    bool StainSurface(Level& level, const Vector3& origin, const Vector3& direction, float length,
                      const std::string& texture, float size, const Vector4& color, float lifetime = 0.0f,
                      const std::string& noStainTag = {});

    void ClearStains();

    [[nodiscard]] const std::vector<Particle>& Particles();
    [[nodiscard]] const std::vector<Stain>& Stains();
}

#endif //TILKY_ENGINE_PARTICLESYSTEM_HPP
