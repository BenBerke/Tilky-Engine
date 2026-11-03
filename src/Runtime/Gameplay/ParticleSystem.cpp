#include "Headers/Runtime/Gameplay/ParticleSystem.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <random>
#include <unordered_map>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/RayHit.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

namespace {
    // Lifted off the surface, so it never fights the surface for depth. Stains
    // climb a little each so that overlapping ones don't fight each other.
    constexpr float STAIN_LIFT = 0.04f;
    constexpr float STAIN_LIFT_STEP = 0.003f;
    constexpr int STAIN_LIFT_STEPS = 16;
    // Slower than this after a bounce off a floor, it stays there.
    constexpr float REST_SPEED = 12.0f;
    constexpr float SURFACE_GAP = 0.05f;
    // A looping cycle is at least this long, so Duration 0 can't burst every frame.
    constexpr float SHORTEST_LOOP = 0.05f;

    std::vector<ParticleSystem::Particle> particles;
    std::vector<ParticleSystem::Stain> stains; // oldest first
    std::size_t stainsEverMade = 0;
    std::unordered_map<ComponentInstanceID, ParticleEmitterClock> previews;

    std::mt19937& Rng() {
        static std::mt19937 rng{std::random_device{}()};
        return rng;
    }

    float Random(const float low, const float high) {
        if (high <= low) return low;
        return std::uniform_real_distribution<float>(low, high)(Rng());
    }

    int RandomInt(const int low, const int high) {
        if (high <= low) return low;
        return std::uniform_int_distribution<int>(low, high)(Rng());
    }

    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    float Dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // A random direction within `degrees` of `axis`, evenly over the cone's area.
    Vector3 InCone(const Vector3& axis, const float degrees) {
        const Vector3 forward = Vector3Math::Normalized(axis);
        if (degrees <= 0.0f) return forward;

        const Vector3 helper = std::abs(forward.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        const Vector3 right = Vector3Math::Normalized(Cross(helper, forward));
        const Vector3 up = Cross(forward, right);

        const float cosSpread = std::cos(std::min(degrees, 180.0f) * Constants::DegToRad);
        const float cosAngle = Random(cosSpread, 1.0f);
        const float sinAngle = std::sqrt(std::max(0.0f, 1.0f - cosAngle * cosAngle));
        const float turn = Random(0.0f, Constants::TwoPi);

        return forward * cosAngle + right * (std::cos(turn) * sinAngle) + up * (std::sin(turn) * sinAngle);
    }

    // The rotation that takes local X to `right` and local Y to `up` (both unit,
    // at right angles), the axes a static sprite is drawn along.
    Quaternion FromAxes(const Vector3& right, const Vector3& up) {
        const Vector3 forward = Cross(right, up);
        const float m00 = right.x, m10 = right.y, m20 = right.z;
        const float m01 = up.x, m11 = up.y, m21 = up.z;
        const float m02 = forward.x, m12 = forward.y, m22 = forward.z;

        const float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            return {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
        }
        if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            return {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
        }
        if (m11 > m22) {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            return {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
        }
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        return {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }

    const Sector* SectorByID(const Level& level, const ID id) {
        const auto found = level.sectorIDToIndex.find(id);
        if (found == level.sectorIDToIndex.end()) return nullptr;
        return &level.sectors[found->second];
    }

    bool SectorHasTag(const Sector& sector, const TagId tag) {
        return tag != NO_TAG && std::ranges::find(sector.tagIds, tag) != sector.tagIds.end();
    }

    // Which interval of a sector a height belongs to, the nearest if it's in none.
    const SectorFloor* IntervalAt(const Sector& sector, const float height) {
        const SectorFloor* best = nullptr;
        float bestGap = std::numeric_limits<float>::max();

        for (const SectorFloor& floor : sector.floors) {
            const float gap = height < floor.floor.height ? floor.floor.height - height
                            : height > floor.ceiling.height ? height - floor.ceiling.height : 0.0f;
            if (gap < bestGap) {
                bestGap = gap;
                best = &floor;
            }
        }
        return best;
    }

    // The height a stain follows, at its reference point. NaN-free: a sector
    // that's gone gives the last height, so the stain just stops moving.
    float FollowedHeight(const Level& level, const ParticleSystem::Stain& stain, const float height, const float fallback) {
        using Follows = ParticleSystem::Stain::Follows;

        const Sector* a = SectorByID(level, stain.sectorA);
        const Sector* b = SectorByID(level, stain.sectorB);
        const SectorFloor* floorA = a != nullptr ? IntervalAt(*a, height) : nullptr;
        const SectorFloor* floorB = b != nullptr ? IntervalAt(*b, height) : nullptr;

        switch (stain.follows) {
            case Follows::FLOOR: return floorA != nullptr ? floorA->floor.height : fallback;
            case Follows::CEILING: return floorA != nullptr ? floorA->ceiling.height : fallback;
            case Follows::LOWEST_CEILING:
                if (floorA == nullptr || floorB == nullptr) return fallback;
                return std::min(floorA->ceiling.height, floorB->ceiling.height);
            case Follows::HIGHEST_FLOOR:
                if (floorA == nullptr || floorB == nullptr) return fallback;
                return std::max(floorA->floor.height, floorB->floor.height);
            case Follows::NOTHING: break;
        }
        return fallback;
    }

    // Does a flat stain centred at `point` stay on this surface? Its corners must
    // be in the same sector at about the same height, or it would hang in the air
    // over a step or stick out through a wall.
    bool FlatStainFits(const Level& level, const Vector3& point, const float halfSize, const int sectorIndex) {
        const Vector2 corners[] = {
            {point.x - halfSize, point.z - halfSize}, {point.x + halfSize, point.z - halfSize},
            {point.x - halfSize, point.z + halfSize}, {point.x + halfSize, point.z + halfSize},
        };
        for (const Vector2& corner : corners)
            if (MapQueries::FindSectorContainingPoint(level.sectors, corner, sectorIndex) != sectorIndex) return false;
        return true;
    }

    bool PlaceStain(Level& level, const RayHit& hit, const std::string& texture, float size, const Vector4& color,
                    const float lifetime, const TagId noStainTag,
                    const ComponentInstanceID previewOwner = INVALID_COMPONENT_INSTANCE_ID) {
        if (texture.empty() || size <= 0.0f) return false;

        ParticleSystem::Stain stain;
        Vector3 centre = hit.position;
        const Vector3 normal = hit.normal;

        if (hit.HitWall()) {
            const Wall& wall = *hit.wall;

            // Whichever sector the side we hit faces: its light, and no stains on
            // a liquid's walls would be wrong, so only flats check the tag.
            const Vector2 besideWall = {centre.x + normal.x * 0.5f, centre.z + normal.z * 0.5f};
            stain.sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, besideWall);

            // Keep it on the wall: slide it along, and shrink it if the piece is
            // smaller than the stain. The stain can turn, so measure with its diagonal.
            const float reach = size * 0.7f;
            const float along = Vector3Math::Length(centre - Vector3{wall.start.x, centre.y, wall.start.y});
            if (wall.length < reach * 2.0f) {
                size *= wall.length / (reach * 2.0f);
            }
            const float shrunkReach = size * 0.7f;
            const float slide = std::clamp(along, shrunkReach, std::max(shrunkReach, wall.length - shrunkReach)) - along;
            centre.x += wall.dir.x * slide;
            centre.z += wall.dir.y * slide;

            const float pieceHeight = hit.pieceTop - hit.pieceBottom;
            if (pieceHeight <= 0.0f) return false;
            if (pieceHeight < shrunkReach * 2.0f) size *= pieceHeight / (shrunkReach * 2.0f);
            const float finalReach = size * 0.7f;
            centre.y = std::clamp(centre.y, hit.pieceBottom + finalReach,
                                  std::max(hit.pieceBottom + finalReach, hit.pieceTop - finalReach));

            // A step or a lintel rides whichever floor or ceiling moves it. A solid
            // wall stays where it is.
            if (wall.frontSector != INVALID_ID && wall.backSector != INVALID_ID) {
                stain.sectorA = wall.frontSector;
                stain.sectorB = wall.backSector;
                stain.follows = hit.pieceSlot == WallSurfaceSlot::Bottom ? ParticleSystem::Stain::Follows::HIGHEST_FLOOR
                                                                         : ParticleSystem::Stain::Follows::LOWEST_CEILING;
            }
        }
        else if (hit.HitSectorFloor() || hit.HitSectorCeiling()) {
            if (SectorHasTag(*hit.sector, noStainTag)) return false;

            stain.sectorIndex = static_cast<int>(hit.sector - level.sectors.data());
            stain.sectorA = hit.sector->id;
            stain.follows = hit.HitSectorFloor() ? ParticleSystem::Stain::Follows::FLOOR
                                                 : ParticleSystem::Stain::Follows::CEILING;

            // Too near an edge: try a smaller one before giving up.
            float halfSize = size * 0.5f;
            if (!FlatStainFits(level, centre, halfSize, stain.sectorIndex)) {
                halfSize *= 0.5f;
                size *= 0.5f;
                if (!FlatStainFits(level, centre, halfSize, stain.sectorIndex)) return false;
            }
        }
        else return false;

        // A random turn in the surface's plane, so repeats don't look stamped.
        const Vector3 helper = std::abs(normal.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        const Vector3 tangent = Vector3Math::Normalized(Cross(helper, normal));
        const Vector3 bitangent = Cross(normal, tangent);
        const float turn = Random(0.0f, Constants::TwoPi);
        const Vector3 right = tangent * std::cos(turn) + bitangent * std::sin(turn);
        const Vector3 up = Cross(normal, right);

        const float lift = STAIN_LIFT + STAIN_LIFT_STEP * static_cast<float>(stainsEverMade % STAIN_LIFT_STEPS);
        ++stainsEverMade;

        stain.bottomCentre = centre + normal * lift - up * (size * 0.5f);
        stain.rotation = FromAxes(right, up);
        stain.texture = texture;
        stain.color = color;
        stain.size = size;
        stain.lifetime = lifetime;
        stain.previewOwner = previewOwner;
        stain.followedHeight = FollowedHeight(level, stain, centre.y, 0.0f);

        // The cap removes the oldest first.
        const std::size_t cap = static_cast<std::size_t>(std::max(level.rendererSettings.maxStains, 0));
        if (cap == 0) return false;
        if (stains.size() >= cap) stains.erase(stains.begin(), stains.begin() + static_cast<std::ptrdiff_t>(stains.size() - cap + 1));
        stains.push_back(std::move(stain));
        return true;
    }

    void LandParticle(Level& level, const ParticleSystem::Particle& particle, const RayHit& hit) {
        const ParticleSystem::Style& style = *particle.style;
        if (style.stainTextures.empty() || Random(0.0f, 1.0f) >= style.stainChance) return;

        const std::string& texture = style.stainTextures[RandomInt(0, static_cast<int>(style.stainTextures.size()) - 1)];
        PlaceStain(level, hit, texture, Random(style.stainSizeMin, style.stainSizeMax), style.stainColor,
                   style.stainLifetime, style.noStainTag, style.previewOwner);
    }

    // Moves one particle along its path. False when it should go away.
    bool MoveParticle(Level& level, ParticleSystem::Particle& particle, const float deltaTime) {
        const ParticleSystem::Style& style = *particle.style;

        if (particle.resting) {
            // Keeps sitting on a floor that moves.
            if (particle.sectorIndex >= 0) {
                const Sector& sector = level.sectors[particle.sectorIndex];
                if (const SectorFloor* floor = IntervalAt(sector, particle.position.y)) {
                    particle.position.y = MapQueries::SurfaceHeightAt(sector, floor->floor, {particle.position.x, particle.position.z})
                                        + particle.size * 0.5f;
                }
            }
            return true;
        }

        particle.velocity.y -= style.gravity * deltaTime;
        if (style.drag > 0.0f) particle.velocity = particle.velocity * std::max(0.0f, 1.0f - style.drag * deltaTime);

        const Vector3 step = particle.velocity * deltaTime;
        const float stepLength = Vector3Math::Length(step);

        if (style.collision != ParticleCollision::NONE && stepLength > 0.0f) {
            if (const std::optional<RayHit> hit = GameFunctions::RaycastGeometry(level, particle.position, step, stepLength)) {
                LandParticle(level, particle, *hit);
                if (style.collision == ParticleCollision::DIE) return false;

                const float intoSurface = Dot(particle.velocity, hit->normal);
                const Vector3 bounced = particle.velocity - hit->normal * (2.0f * intoSurface);
                // The bounce loses speed straight off the surface and some along it.
                const Vector3 off = hit->normal * Dot(bounced, hit->normal);
                particle.velocity = off * style.bounciness + (bounced - off) * (0.5f + 0.5f * style.bounciness);
                particle.position = hit->position + hit->normal * SURFACE_GAP;

                if (hit->normal.y > 0.5f && Vector3Math::Length(particle.velocity) < REST_SPEED) {
                    particle.velocity = {0.0f, 0.0f, 0.0f};
                    particle.resting = true;
                    particle.position.y = hit->position.y + particle.size * 0.5f;
                }
                particle.sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, {particle.position.x, particle.position.z}, particle.sectorIndex);
                return true;
            }
        }

        particle.position = particle.position + step;
        particle.sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, {particle.position.x, particle.position.z}, particle.sectorIndex);

        // Only something that can leave the map through nothing gets here with no
        // sector: it would otherwise fall for the rest of its life unseen.
        return particle.sectorIndex >= 0 || style.collision == ParticleCollision::NONE;
    }
}

namespace {
    int EmitBurst(Level& level, const ComponentParticleEmitter& emitter, const Vector3& position, const Vector3& direction,
                  int count, float scale, ComponentInstanceID previewOwner);

    int EmitFromOwner(Level& level, const ComponentParticleEmitter& emitter, const int count, const ComponentInstanceID previewOwner) {
        const ComponentTransform* transform = level.transforms.Get(emitter.ownerID);
        if (transform == nullptr) return 0;
        return EmitBurst(level, emitter, transform->LocalToWorld(emitter.offset), {0.0f, 0.0f, 0.0f}, count, 1.0f, previewOwner);
    }

    // One step of an emitter's cycle: the burst that starts it, the stream, and
    // what happens at its end. The game and the editor's preview share this.
    void RunCycle(Level& level, const ComponentParticleEmitter& emitter, ParticleEmitterClock& clock, const float deltaTime,
                  const ComponentInstanceID previewOwner) {
        if (!clock.playing) return;

        if (clock.burstDue) {
            clock.burstDue = false;
            clock.cycleTime = 0.0f;
            EmitFromOwner(level, emitter, -1, previewOwner);
        }
        if (deltaTime <= 0.0f) return;

        if (emitter.rate > 0.0f) {
            clock.spawnDebt += emitter.rate * deltaTime;
            const int due = static_cast<int>(clock.spawnDebt);
            if (due > 0) {
                clock.spawnDebt -= static_cast<float>(due);
                EmitFromOwner(level, emitter, due, previewOwner);
            }
        }

        clock.cycleTime += deltaTime;
        if (clock.cycleTime < (emitter.loop ? std::max(emitter.duration, SHORTEST_LOOP) : emitter.duration)) return;

        if (emitter.loop) clock.burstDue = true;
        else {
            clock.playing = false;
            clock.spawnDebt = 0.0f;
        }
    }

    // Moves what's alive. Landing can add stains and erases nothing else, so a
    // plain compact is safe.
    void UpdateLive(Level& level, const float deltaTime) {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < particles.size(); ++i) {
            ParticleSystem::Particle& particle = particles[i];
            particle.age += deltaTime;
            if (particle.age >= particle.lifetime) continue;
            if (!MoveParticle(level, particle, deltaTime)) continue;

            if (kept != i) particles[kept] = std::move(particle);
            ++kept;
        }
        particles.resize(kept);

        kept = 0;
        for (std::size_t i = 0; i < stains.size(); ++i) {
            ParticleSystem::Stain& stain = stains[i];
            stain.age += deltaTime;
            if (stain.lifetime > 0.0f && stain.age >= stain.lifetime) continue;

            if (stain.follows != ParticleSystem::Stain::Follows::NOTHING) {
                const float height = FollowedHeight(level, stain, stain.bottomCentre.y, stain.followedHeight);
                stain.bottomCentre.y += height - stain.followedHeight;
                stain.followedHeight = height;
            }

            if (kept != i) stains[kept] = std::move(stain);
            ++kept;
        }
        stains.resize(kept);
    }

    void RemovePreviewOutput(const ComponentInstanceID emitter) {
        std::erase_if(particles, [emitter](const ParticleSystem::Particle& particle) { return particle.style->previewOwner == emitter; });
        std::erase_if(stains, [emitter](const ParticleSystem::Stain& stain) { return stain.previewOwner == emitter; });
    }

    int EmitBurst(Level& level, const ComponentParticleEmitter& emitter, const Vector3& position, const Vector3& direction,
                  int count, const float scale, const ComponentInstanceID previewOwner) {
        using namespace ParticleSystem;
        if (emitter.textures.empty()) return 0;

        if (count < 0) {
            const float wanted = static_cast<float>(RandomInt(emitter.burstMin, emitter.burstMax)) * std::max(scale, 0.0f);
            count = static_cast<int>(wanted);
            // The fraction left over is a chance of one more.
            if (Random(0.0f, 1.0f) < wanted - static_cast<float>(count)) ++count;
        }
        if (count <= 0) return 0;

        Vector3 axis = direction;
        if (axis.IsZero()) {
            axis = emitter.direction;
            if (const ComponentTransform* transform = level.transforms.Get(emitter.ownerID))
                axis = QuaternionMath::Rotate(transform->rotation, axis);
            if (axis.IsZero()) axis = {0.0f, 1.0f, 0.0f};
        }

        auto style = std::make_shared<Style>();
        style->textures = emitter.textures;
        style->animate = emitter.animate;
        style->gravity = emitter.gravity;
        style->drag = emitter.drag;
        style->sizeEnd = emitter.sizeEnd;
        style->colorStart = emitter.colorStart;
        style->colorEnd = emitter.colorEnd;
        style->collision = emitter.collision;
        style->bounciness = emitter.bounciness;
        style->stainTextures = emitter.stainTextures;
        style->stainChance = emitter.stainChance;
        style->stainSizeMin = emitter.stainSizeMin;
        style->stainSizeMax = emitter.stainSizeMax;
        style->stainLifetime = emitter.stainLifetime;
        style->stainColor = emitter.stainColor;
        style->noStainTag = emitter.noStainTag;
        style->previewOwner = previewOwner;

        const std::size_t cap = static_cast<std::size_t>(std::max(level.rendererSettings.maxParticles, 0));
        const int startSector = MapQueries::FindSectorContainingPoint(level.sectors, {position.x, position.z});

        int made = 0;
        for (int i = 0; i < count; ++i) {
            if (cap == 0) break;
            // Over the cap the oldest make way, as stains do.
            if (particles.size() >= cap) particles.erase(particles.begin());

            Particle particle;
            particle.style = style;
            particle.position = position;
            particle.velocity = InCone(axis, emitter.spread) * Random(emitter.speedMin, emitter.speedMax);
            particle.lifetime = std::max(Random(emitter.lifetimeMin, emitter.lifetimeMax), 0.01f);
            particle.size = Random(emitter.sizeMin, emitter.sizeMax);
            particle.textureIndex = RandomInt(0, static_cast<int>(emitter.textures.size()) - 1);
            particle.sectorIndex = startSector;
            particles.push_back(std::move(particle));
            ++made;
        }
        return made;
    }
}

namespace ParticleSystem {
    void Start() {
        particles.clear();
        stains.clear();
        previews.clear();
    }

    void Stop() {
        Start();
    }

    int Emit(Level& level, const ComponentParticleEmitter& emitter, const Vector3& position, const Vector3& direction,
             const int count, const float scale) {
        return EmitBurst(level, emitter, position, direction, count, scale, INVALID_COMPONENT_INSTANCE_ID);
    }

    int EmitAtOwner(Level& level, const ComponentParticleEmitter& emitter, const int count) {
        return EmitFromOwner(level, emitter, count, INVALID_COMPONENT_INSTANCE_ID);
    }

    void PlayEmitter(ComponentParticleEmitter& emitter) {
        emitter.clock = {};
        emitter.clock.playing = true;
        emitter.clock.burstDue = true;
    }

    void StopEmitter(ComponentParticleEmitter& emitter) {
        emitter.clock = {};
    }

    void PreviewPlay(const ComponentParticleEmitter& emitter) {
        RemovePreviewOutput(emitter.instanceID);
        ParticleEmitterClock clock;
        clock.playing = true;
        clock.burstDue = true;
        previews[emitter.instanceID] = clock;
    }

    void PreviewStop(const ComponentInstanceID emitter) {
        previews.erase(emitter);
        RemovePreviewOutput(emitter);
    }

    bool IsPreviewing(const ComponentInstanceID emitter) {
        return previews.contains(emitter);
    }

    void UpdatePreview(Level& level, const float deltaTime) {
        if (previews.empty()) return;

        // The level can be edited under a preview: a deleted sector must not leave
        // an index that's past the end.
        for (Particle& particle : particles)
            if (particle.sectorIndex >= static_cast<int>(level.sectors.size())) particle.sectorIndex = -1;

        for (auto it = previews.begin(); it != previews.end();) {
            const ComponentParticleEmitter* emitter = level.particleEmitters.GetInstance(it->first);
            if (emitter == nullptr) {
                RemovePreviewOutput(it->first);
                it = previews.erase(it);
                continue;
            }
            RunCycle(level, *emitter, it->second, deltaTime, it->first);
            ++it;
        }

        if (deltaTime > 0.0f) UpdateLive(level, deltaTime);
    }

    bool StainSurface(Level& level, const Vector3& origin, const Vector3& direction, const float length,
                      const std::string& texture, const float size, const Vector4& color, const float lifetime,
                      const TagId noStainTag) {
        const std::optional<RayHit> hit = GameFunctions::RaycastGeometry(level, origin, direction, length);
        if (!hit.has_value()) return false;
        return PlaceStain(level, *hit, texture, size, color, lifetime, noStainTag);
    }

    void ClearStains() {
        stains.clear();
    }

    void Update(Level& level, const float deltaTime) {
        for (ComponentParticleEmitter& emitter : level.particleEmitters.components) {
            if (!emitter.isActive || !emitter.ownerEnabled) continue;

            if (!emitter.started) {
                emitter.started = true;
                if (emitter.playOnStart) PlayEmitter(emitter);
            }
            RunCycle(level, emitter, emitter.clock, deltaTime, INVALID_COMPONENT_INSTANCE_ID);
        }

        if (deltaTime > 0.0f) UpdateLive(level, deltaTime);
    }

    const std::vector<Particle>& Particles() { return particles; }
    const std::vector<Stain>& Stains() { return stains; }
}
