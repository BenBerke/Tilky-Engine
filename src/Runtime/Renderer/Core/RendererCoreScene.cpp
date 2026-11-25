// The CPU side of drawing a level, shared by every backend: the per-frame
// GPU arrays, visibility culling, the camera's eye, gateway view math, the
// UI draw order and the 2D editors' label layout.

#include "Headers/Runtime/Renderer/RendererCore.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/WallPieces.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/Gameplay/ParticleSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/UISystem.hpp"

using namespace RendererGpu;

// ============================================================================
// Walls and flats
// ============================================================================

namespace {
    // Everything baked into the flat triangle instances: how many exist,
    // which (sector, floor) each one points at, and where its corners are -
    // so a vertex the 2D editor moves shows in its 3D view at once. Cheap
    // enough to compare every frame.
    size_t ComputeFlatLayoutSignature(const Level& level) {
        constexpr size_t PRIME = 1000003u;

        const auto mixPoint = [](size_t signature, const Vector2 point) {
            signature = signature * PRIME + std::bit_cast<std::uint32_t>(point.x);
            return signature * PRIME + std::bit_cast<std::uint32_t>(point.y);
        };

        size_t signature = level.sectors.size();

        for (const Sector& sector : level.sectors) {
            signature = signature * PRIME + sector.floors.size();
            signature = signature * PRIME + sector.triangles.size();

            for (const Triangle& triangle : sector.triangles) {
                signature = mixPoint(signature, triangle.a);
                signature = mixPoint(signature, triangle.b);
                signature = mixPoint(signature, triangle.c);
            }
        }

        return signature;
    }

    // The piece is lit by the sector it is seen from, with the same
    // subtractive rule as floors and ceilings (light 255 = unlit color).
    // Fake contrast nudges that light by the wall's direction: +contrast for
    // a wall along Z, -contrast along X, smoothly in between (dz^2 - dx^2 of
    // the unit direction), clamped so it never goes past full or zero light.
    Vector4 LitWallColor(const Wall& wall, const WallPieces::WallPiece& piece, const float contrast, const bool fullbright) {
        if (piece.facingSector == nullptr || fullbright) return wall.color;

        Vector3 light = piece.facingSector->light;

        const float dx = wall.end.x - wall.start.x;
        const float dz = wall.end.y - wall.start.y;
        const float lengthSq = dx * dx + dz * dz;

        if (contrast != 0.0f && lengthSq > 0.0f) {
            const float adjustment = contrast * (dz * dz - dx * dx) / lengthSq;

            light = {
                std::clamp(light.x + adjustment, 0.0f, 255.0f),
                std::clamp(light.y + adjustment, 0.0f, 255.0f),
                std::clamp(light.z + adjustment, 0.0f, 255.0f)
            };
        }

        return {
            wall.color.x - (255.0f - light.x) / 255.0f,
            wall.color.y - (255.0f - light.y) / 255.0f,
            wall.color.z - (255.0f - light.z) / 255.0f,
            wall.color.w
        };
    }

    void PushGpuWallPiece(
        std::vector<GpuWall>& gpuWalls,
        const Wall& wall,
        const WallPieces::WallPiece& piece,
        const float textureRegionIndex,
        const Vector2 scroll,
        const float wallContrast,
        const bool fullbright,
        const uint8_t metaData
    ) {
        // The showing frame's placement.
        const TextureFrame& frame = wall.Surface(piece.slot).texture.Shown();

        GpuWall gpuWall{};

        gpuWall.data = {
            textureRegionIndex,
            piece.frontFacing ? 0.0f : 1.0f,
            piece.anchorHeight,
            static_cast<float>(metaData)
        };

        // Only the part of the wall the piece covers.
        const Vector2 pieceStart = piece.Start(wall);
        const Vector2 pieceEnd = piece.End(wall);

        gpuWall.startEnd = {
            pieceStart.x,
            pieceStart.y,
            pieceEnd.x,
            pieceEnd.y
        };

        gpuWall.color = LitWallColor(wall, piece, wallContrast, fullbright);

        // heights.xy = bottom/top at the start point
        // heights.zw = bottom/top at the end point
        gpuWall.heights = {
            piece.bottomStart,
            piece.topStart,
            piece.bottomEnd,
            piece.topEnd
        };

        // data2.xy = texture offset; data2.zw = independent X/Y scale.
        gpuWall.data2 = {
            frame.textureOffset.x + scroll.x,
            frame.textureOffset.y + scroll.y,
            frame.textureScale.x,
            frame.textureScale.y
        };

        // Store flip flags as floats to match the GPU vec4 layout. zw place
        // the piece along its wall, so a wall cut into pieces keeps one
        // continuous texture.
        const float wallLength = wall.length;

        gpuWall.data3 = {
            frame.flipTextureX ? 1.0f : 0.0f,
            frame.flipTextureY ? 1.0f : 0.0f,
            piece.startT * wallLength,
            wallLength
        };

        gpuWalls.push_back(gpuWall);
    }
}

void RendererCore::BuildGpuWallsFromMap() {
    Level& level = LevelManager::CurrentLevel();

    gpuWalls.clear();

    WallPieces::SectorBoundsCache boundsCache;
    std::vector<WallPieces::WallPiece> pieces;

    const float wallContrast = level.rendererSettings.wallContrast;

    for (int wallIndex = 0; wallIndex < static_cast<int>(level.walls.size()); ++wallIndex) {
        const Wall& wall = level.walls[wallIndex];
        if (IsWallCulled(level, wall)) continue;

        // The frame showing right now.
        const float topTextureIndex = static_cast<float>(GetTextureRegionIndex(wall.top.texture.Current()));
        const float bottomTextureIndex = static_cast<float>(GetTextureRegionIndex(wall.bottom.texture.Current()));
        const float middleTextureIndex = wall.HasMiddle() ? static_cast<float>(GetTextureRegionIndex(wall.middle.texture.Current())) : 0.0f;

        pieces.clear();
        WallPieces::Build(level, wall, boundsCache, pieces);

        for (const WallPieces::WallPiece& piece : pieces) {
            const float textureIndex = piece.slot == WallSurfaceSlot::Bottom ? bottomTextureIndex
                : piece.slot == WallSurfaceSlot::Middle ? middleTextureIndex
                : topTextureIndex;

            PushGpuWallPiece(
                gpuWalls,
                wall,
                piece,
                textureIndex,
                SurfaceScroll(wall.Surface(piece.slot).texture, static_cast<int>(textureIndex)),
                wallContrast,
                fullbright,
                WallPieceMetaData(wallIndex, piece.slot)
            );
        }
    }

    gpuWallCount = static_cast<int>(gpuWalls.size());
}

void RendererCore::BuildFlatTrianglesFromSectors() {
    Level& level = LevelManager::CurrentLevel();

    flatTriangles.clear();
    flatSectorRanges.assign(level.sectors.size(), {});
    flatSectorBounds.assign(level.sectors.size(), {});

    for (int sectorIndex = 0; sectorIndex < static_cast<int>(level.sectors.size()); ++sectorIndex) {
        const Sector& sector = level.sectors[sectorIndex];

        flatSectorRanges[sectorIndex].first = static_cast<int>(flatTriangles.size());

        if (const WallPieces::SectorBounds bounds = WallPieces::ComputeSectorBounds(sector); bounds.valid)
            flatSectorBounds[sectorIndex] = {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};

        for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
            for (const Triangle& triangle : sector.triangles) {
                {
                    GpuFlatTriangle flatTriangle;

                    flatTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                    flatTriangle.b = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};
                    flatTriangle.c = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};

                    flatTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                    flatTriangle.data = {
                        static_cast<float>(sectorIndex),
                        static_cast<float>(floorIndex),
                        0.0f, // floor surface
                        0.0f
                    };

                    flatTriangles.push_back(flatTriangle);
                }

                {
                    GpuFlatTriangle flatTriangle;

                    flatTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                    flatTriangle.b = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};
                    flatTriangle.c = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};

                    flatTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                    flatTriangle.data = {
                        static_cast<float>(sectorIndex),
                        static_cast<float>(floorIndex),
                        1.0f, // ceiling surface
                        0.0f
                    };

                    flatTriangles.push_back(flatTriangle);
                }
            }
        }

        flatSectorRanges[sectorIndex].count = static_cast<int>(flatTriangles.size()) - flatSectorRanges[sectorIndex].first;
    }

    flatTriangleCount = static_cast<int>(flatTriangles.size());
    flatLayoutSignature = ComputeFlatLayoutSignature(level);
}

bool RendererCore::FlatLayoutChanged() const {
    return ComputeFlatLayoutSignature(LevelManager::CurrentLevel()) != flatLayoutSignature;
}

// ============================================================================
// Sectors
// ============================================================================

void RendererCore::BuildGpuSectors() {
    const Level& level = LevelManager::CurrentLevel();

    gpuSectors.clear();
    gpuSectorFloors.clear();

    gpuSectors.reserve(level.sectors.size());

    size_t totalFloorCount = 0;
    for (const Sector& sector : level.sectors) totalFloorCount += sector.floors.size();

    gpuSectorFloors.reserve(totalFloorCount);

    for (size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex) {
        const Sector& sector = level.sectors[sectorIndex];
        GpuSector gpuSector{};

        gpuSector.floorData = {
            static_cast<float>(gpuSectorFloors.size()),
            static_cast<float>(sector.floors.size()),
            0.0f,
            0.0f
        };

        // Baked with the flat triangles; a sector added since then has no
        // triangles drawn yet either.
        if (sectorIndex < flatSectorBounds.size()) gpuSector.bounds = flatSectorBounds[sectorIndex];

        for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
            const SectorFloor& floor = sector.floors[floorIndex];
            GpuSectorFloor gpuFloor{};

            const Vector4 floorColor = floor.floor.color;
            const Vector4 ceilingColor = floor.ceiling.color;

            gpuFloor.heights = {
                floor.floor.height,
                floor.ceiling.height,
                0.0f,
                0.0f
            };

            gpuFloor.slopeData = {
                static_cast<float>(floor.floor.slopeDirection),
                floor.floor.SlopeGradient(),
                static_cast<float>(floor.ceiling.slopeDirection),
                floor.ceiling.SlopeGradient(),
            };

            const Vector3 light = SectorLight(sector);

            gpuFloor.floorColor = {
                floorColor.x - (255.0f - light.x) / 255.0f,
                floorColor.y - (255.0f - light.y) / 255.0f,
                floorColor.z - (255.0f - light.z) / 255.0f,
                floorColor.w
            };

            gpuFloor.ceilingColor = {
                ceilingColor.x - (255.0f - light.x) / 255.0f,
                ceilingColor.y - (255.0f - light.y) / 255.0f,
                ceilingColor.z - (255.0f - light.z) / 255.0f,
                ceilingColor.w
            };

            // The frame showing right now, its placement, and the scroll so far.
            const TextureFrame& floorFrame = floor.floor.texture.Shown();
            const TextureFrame& ceilingFrame = floor.ceiling.texture.Shown();
            const int floorTextureIndex = GetTextureRegionIndex(floor.floor.texture.Current());
            const int ceilingTextureIndex = GetTextureRegionIndex(floor.ceiling.texture.Current());
            const Vector2 floorScroll = SurfaceScroll(floor.floor.texture, floorTextureIndex);
            const Vector2 ceilingScroll = SurfaceScroll(floor.ceiling.texture, ceilingTextureIndex);

            gpuFloor.textureData = {
                static_cast<float>(floorTextureIndex),
                static_cast<float>(ceilingTextureIndex),
                static_cast<float>(FlatMetaData(static_cast<int>(sectorIndex), floorIndex, MarkedSurfaceKind::Floor)),
                static_cast<float>(FlatMetaData(static_cast<int>(sectorIndex), floorIndex, MarkedSurfaceKind::Ceiling))
            };

            gpuFloor.textureOffsets = {
                floorFrame.textureOffset.x + floorScroll.x,
                floorFrame.textureOffset.y + floorScroll.y,
                ceilingFrame.textureOffset.x + ceilingScroll.x,
                ceilingFrame.textureOffset.y + ceilingScroll.y
            };

            gpuFloor.textureFlip = {
                floorFrame.flipTextureX ? 1.0f : 0.0f,
                floorFrame.flipTextureY ? 1.0f : 0.0f,
                ceilingFrame.flipTextureX ? 1.0f : 0.0f,
                ceilingFrame.flipTextureY ? 1.0f : 0.0f
            };

            gpuFloor.textureScales = {
                floorFrame.textureScale.x,
                floorFrame.textureScale.y,
                ceilingFrame.textureScale.x,
                ceilingFrame.textureScale.y
            };

            gpuSectorFloors.push_back(gpuFloor);
        }

        gpuSectors.push_back(gpuSector);
    }
}

// ============================================================================
// Sprites, stains and particles
// ============================================================================

void RendererCore::BuildGpuSprites() {
    gpuSprites.clear();

    Level& level = LevelManager::CurrentLevel();

    // One flat or camera-facing image with a texture region already looked up.
    const auto addImage = [&](const Vector3& bottomCentre, const float size, const Vector4& color, const int sectorIndex,
                              const int region, const Quaternion* flatRotation) {
        GpuSprite gpuSprite{};
        gpuSprite.positionSize = {bottomCentre.x, bottomCentre.y, bottomCentre.z, size};
        gpuSprite.color = color;

        if (sectorIndex >= 0 && sectorIndex < static_cast<int>(level.sectors.size())) {
            const Vector3 light = SectorLight(level.sectors[sectorIndex]);
            gpuSprite.color.x -= (255.0f - light.x) / 255.0f;
            gpuSprite.color.y -= (255.0f - light.y) / 255.0f;
            gpuSprite.color.z -= (255.0f - light.z) / 255.0f;
        }

        gpuSprite.textureIndices0 = {region, region, region, region};
        gpuSprite.textureIndices1 = {region, region, region, region};
        gpuSprite.data = {size, static_cast<float>(SIDECOUNT_SINGLE), 0.0f, 1.0f};

        const Quaternion rotation = flatRotation != nullptr ? *flatRotation : Quaternion::Identity();
        gpuSprite.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
        gpuSprite.flags = {flatRotation != nullptr ? 1 : 0, 0, 0, 0};

        gpuSprites.push_back(gpuSprite);
    };

    // Stains first: they're on the surfaces, so everything else draws over them.
    for (const ParticleSystem::Stain& stain : ParticleSystem::Stains()) {
        const int region = GetTextureRegionIndex(stain.texture);
        if (region < 0) continue;

        Vector4 color = stain.color;
        // The last quarter of a timed stain's life fades it out.
        if (stain.lifetime > 0.0f) color.w *= std::min(1.0f, (stain.lifetime - stain.age) / (stain.lifetime * 0.25f));

        addImage(stain.bottomCentre, stain.size, color, stain.sectorIndex, region, &stain.rotation);
    }

    for (ComponentSprite& spriteComponent : level.sprites.components) {
        if (!spriteComponent.isActive) continue;

        // A disabled entity's sprites are hidden in game; the editor camera
        // still shows them, dimmed.
        if (!spriteComponent.ownerEnabled && !useEditorCamera) continue;

        ComponentTransform* transform = level.transforms.Get(spriteComponent.ownerID);

        if (IsEntityCulled(*transform)) continue;


        GpuSprite gpuSprite{};

        const Vector3 position = transform->LocalToWorld(spriteComponent.offset);

        gpuSprite.positionSize = {
            position.x,
            position.y,
            position.z,
            transform->scale.z
        };


        gpuSprite.color = spriteComponent.color;

        if (transform->sectorIndex >= 0 &&transform->sectorIndex < static_cast<int>(level.sectors.size())) {
            const Vector3 light = SectorLight(level.sectors[transform->sectorIndex]);

            gpuSprite.color = {
                spriteComponent.color.x - (255.0f - light.x) / 255.0f,
                spriteComponent.color.y - (255.0f - light.y) / 255.0f,
                spriteComponent.color.z - (255.0f - light.z) / 255.0f,
                spriteComponent.color.w
            };
        }

        if (!spriteComponent.ownerEnabled) {
            constexpr float DISABLED_DIM = 0.35f;
            gpuSprite.color.x *= DISABLED_DIM;
            gpuSprite.color.y *= DISABLED_DIM;
            gpuSprite.color.z *= DISABLED_DIM;
        }

        gpuSprite.textureIndices0 = {
            GetTextureRegionIndex(spriteComponent.textureFileNames[0]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[1]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[2]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[3])
        };

        gpuSprite.textureIndices1 = {
            GetTextureRegionIndex(spriteComponent.textureFileNames[4]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[5]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[6]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[7])
        };

        // Directional sprites face the rotation's local +Z, flattened onto the map.
        const Vector3 facing = QuaternionMath::Rotate(transform->rotation, QuaternionMath::LocalForward());

        gpuSprite.data = {
            transform->scale.x,
            static_cast<float>(spriteComponent.sideCount),
            facing.x,
            facing.z
        };

        Quaternion rotation = Quaternion::Identity();

        if (spriteComponent.isStatic) rotation = transform->rotation.Normalized();

        gpuSprite.rotation = {
            rotation.x,
            rotation.y,
            rotation.z,
            rotation.w
        };

        gpuSprite.flags = {
            spriteComponent.isStatic ? 1 : 0,
            EntityMetaData(spriteComponent.ownerID),
            0,
            0
        };

        gpuSprites.push_back(gpuSprite);
    }

    // Particles after the sprites, so their faded edges blend over them.
    for (const ParticleSystem::Particle& particle : ParticleSystem::Particles()) {
        const ParticleSystem::Style& style = *particle.style;
        const float life = std::min(particle.age / particle.lifetime, 1.0f);

        const std::size_t frame = style.animate
            ? std::min(static_cast<std::size_t>(life * static_cast<float>(style.textures.size())), style.textures.size() - 1)
            : static_cast<std::size_t>(particle.textureIndex);
        const int region = GetTextureRegionIndex(style.textures[frame]);
        if (region < 0) continue;

        const float size = particle.size * (1.0f + (style.sizeEnd - 1.0f) * life);
        const Vector4 color = {
            style.colorStart.x + (style.colorEnd.x - style.colorStart.x) * life,
            style.colorStart.y + (style.colorEnd.y - style.colorStart.y) * life,
            style.colorStart.z + (style.colorEnd.z - style.colorStart.z) * life,
            style.colorStart.w + (style.colorEnd.w - style.colorStart.w) * life
        };
        const Vector3 bottomCentre = {particle.position.x, particle.position.y - size * 0.5f, particle.position.z};
        addImage(bottomCentre, size, color, particle.sectorIndex, region, nullptr);
    }

    spriteCount = static_cast<int>(gpuSprites.size());
}

// ============================================================================
// Collider debug view, entity markers, runtime editor gizmos
// ============================================================================

void RendererCore::BuildGpuColliders() {
    const Level& level = LevelManager::CurrentLevel();

    gpuColliders.clear();

    for (const ComponentCollider& collider : level.colliders.ActiveColliders()) {
        const ComponentTransform* transform = level.transforms.Get(collider.ownerID);

        GpuCollider gpuCollider;

        // The collider's bottom, as physics and Raycast place it: a sphere or
        // capsule rests on it, a box stands on it. The shader draws them
        // around a centre, so lift that by half the height.
        const Vector3 position = collider.Base(*transform);
        const float height = collider.Height(*transform);
        const float radius = collider.Radius(*transform);
        const Vector3 size = collider.WorldSize(*transform);

        gpuCollider.positionType.x = position.x;
        gpuCollider.positionType.y = position.y + height * 0.5f;
        gpuCollider.positionType.z = position.z;

        // The ColliderType: 0 sphere, 1 box, 2 capsule.
        gpuCollider.positionType.w = static_cast<float>(collider.type);

        // World sizes: x is a sphere's or capsule's radius, xyz a box's size.
        gpuCollider.scale.x = collider.IsRound() ? radius : size.x;
        gpuCollider.scale.y = size.y;
        gpuCollider.scale.z = size.z;
        // A capsule's caps sit this far above and below the centre.
        gpuCollider.scale.w = collider.type == COLLIDERTYPE_CAPSULE ? height * 0.5f - radius : 0.0f;

        gpuColliders.push_back(gpuCollider);
    }

    colliderCount = static_cast<int>(gpuColliders.size());
}

void RendererCore::BuildGpuEntityMarkers() {
    const Level& level = LevelManager::CurrentLevel();

    gpuEntityMarkers.clear();

    for (const ComponentTransform& transform : level.transforms.components)
        gpuEntityMarkers.push_back({transform.position.x, transform.position.y, transform.position.z, 0.0f});

    entityMarkerCount = static_cast<int>(gpuEntityMarkers.size());
}

/// Runtime editor gizmos: a facing arrow on every entity and a frustum on
/// every camera. Drawn in the scene rather than over it, so they read apart
/// from the transform gizmo's screen-sized axis arrows.

namespace {
    // Lengths in world units; entities face local +Z. Thickness is in screen
    // pixels, so a gizmo up close doesn't swell - capped in world units so a
    // far one doesn't become a blob either.
    constexpr float FACING_ARROW_LENGTH = 8.0f;
    constexpr float FACING_ARROW_LIFT = 1.0f; // off the feet, so it clears the floor
    constexpr float FACING_ARROW_HEAD_LENGTH = 2.0f;
    constexpr float FACING_ARROW_SHAFT_PIXELS = 10.0f; // width
    constexpr float FACING_ARROW_HEAD_PIXELS = 25.0f;
    constexpr float FACING_ARROW_MAX_SHAFT_RADIUS = 0.4f;
    constexpr float FACING_ARROW_MAX_HEAD_RADIUS = 1.0f;
    constexpr int FACING_ARROW_SIDES = 6;

    // Yellow: none of the axis colours, and solid where colliders are outlines.
    const Vector3 FACING_ARROW_COLOR = {1.0f, 0.82f, 0.1f};

    // Edges are solid beams rather than lines: a 1 pixel line got lost
    // against busy textures.
    constexpr float CAMERA_FRUSTUM_DEPTH = 8.0f;
    constexpr float CAMERA_FRUSTUM_EDGE_PIXELS = 10.0f; // width
    constexpr float CAMERA_FRUSTUM_MAX_EDGE_RADIUS = 0.4f;
    constexpr int CAMERA_FRUSTUM_EDGE_SIDES = 6;
    const Vector3 CAMERA_FRUSTUM_COLOR = {1.0f, 0.3f, 0.85f};

    // Fixed light, so the gizmos' shapes and directions read at a glance.
    const Vector3 GIZMO_LIGHT_DIRECTION = Vector3Math::Normalized({0.4f, 1.0f, 0.3f});

    void AddShadedTriangle(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const Vector3& a,
        const Vector3& b,
        const Vector3& c,
        const Vector3& normal,
        const Vector3& baseColor
    ) {
        const float light = 0.45f + 0.55f * std::max(0.0f, Vector3Math::Dot(normal, GIZMO_LIGHT_DIRECTION));
        const Vector4 color = {baseColor.x * light, baseColor.y * light, baseColor.z * light, 1.0f};

        for (const Vector3& point : {a, b, c}) vertices.push_back({{point.x, point.y, point.z, 0.0f}, color});
    }

    // Turns a width in pixels into a radius in world units at a point, for
    // the view the gizmos are drawn through.
    struct PixelScale {
        Vector3 eye;
        Vector3 forward; // unit
        float worldPerPixelPerDepth = 0.0f;

        [[nodiscard]] float Radius(const Vector3& point, const float pixels, const float maxRadius) const {
            constexpr float MIN_DEPTH = 0.05f;
            const float depth = std::max(Vector3Math::Dot(point - eye, forward), MIN_DEPTH);
            return std::min(depth * worldPerPixelPerDepth * pixels * 0.5f, maxRadius);
        }
    };

    // Two unit vectors perpendicular to `axis` and to each other.
    void PerpendicularBasis(const Vector3& axis, Vector3& u, Vector3& v) {
        const Vector3 helper = std::abs(axis.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        u = Vector3Math::Normalized(Vector3Math::Cross(axis, helper));
        v = Vector3Math::Cross(axis, u);
    }

    // An open prism from a to b, sized per end so it stays the same width on
    // screen along its length. Each end is padded by its radius so beams
    // meeting at a corner close the gap between them.
    void AddBeam(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const PixelScale& scale,
        const Vector3& a,
        const Vector3& b,
        const Vector3& color
    ) {
        const Vector3 span = b - a;
        if (Vector3Math::LengthSquared(span) <= Constants::Epsilon) return;

        const float startRadius = scale.Radius(a, CAMERA_FRUSTUM_EDGE_PIXELS, CAMERA_FRUSTUM_MAX_EDGE_RADIUS);
        const float endRadius = scale.Radius(b, CAMERA_FRUSTUM_EDGE_PIXELS, CAMERA_FRUSTUM_MAX_EDGE_RADIUS);

        const Vector3 axis = Vector3Math::Normalized(span);
        const Vector3 start = a - axis * startRadius;
        const Vector3 end = b + axis * endRadius;

        Vector3 u, v;
        PerpendicularBasis(axis, u, v);

        for (int side = 0; side < CAMERA_FRUSTUM_EDGE_SIDES; ++side) {
            const float angle0 = 2.0f * Constants::Pi * static_cast<float>(side) / CAMERA_FRUSTUM_EDGE_SIDES;
            const float angle1 = 2.0f * Constants::Pi * static_cast<float>(side + 1) / CAMERA_FRUSTUM_EDGE_SIDES;
            const float angleMid = (angle0 + angle1) * 0.5f;

            const Vector3 radial0 = u * std::cos(angle0) + v * std::sin(angle0);
            const Vector3 radial1 = u * std::cos(angle1) + v * std::sin(angle1);
            const Vector3 normal = u * std::cos(angleMid) + v * std::sin(angleMid);

            const Vector3 start0 = start + radial0 * startRadius;
            const Vector3 start1 = start + radial1 * startRadius;
            const Vector3 end0 = end + radial0 * endRadius;
            const Vector3 end1 = end + radial1 * endRadius;

            AddShadedTriangle(vertices, start0, start1, end1, normal, color);
            AddShadedTriangle(vertices, start0, end1, end0, normal, color);
        }
    }

    void AddFacingArrow(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const PixelScale& scale,
        const Vector3& origin,
        const Vector3& direction
    ) {
        Vector3 u, v;
        PerpendicularBasis(direction, u, v);

        const Vector3 headBase = origin + direction * (FACING_ARROW_LENGTH - FACING_ARROW_HEAD_LENGTH);
        const Vector3 tip = origin + direction * FACING_ARROW_LENGTH;
        const Vector3 backward = direction * -1.0f;

        // One size for the whole arrow, measured at its middle - it is short
        // enough that the taper wouldn't show.
        const Vector3 middle = origin + direction * (FACING_ARROW_LENGTH * 0.5f);
        const float shaftRadius = scale.Radius(middle, FACING_ARROW_SHAFT_PIXELS, FACING_ARROW_MAX_SHAFT_RADIUS);
        const float headRadius = scale.Radius(middle, FACING_ARROW_HEAD_PIXELS, FACING_ARROW_MAX_HEAD_RADIUS);

        // Slant of the head's sides, for their normals.
        const float headSlope = headRadius / FACING_ARROW_HEAD_LENGTH;

        for (int side = 0; side < FACING_ARROW_SIDES; ++side) {
            const float angle0 = 2.0f * Constants::Pi * static_cast<float>(side) / FACING_ARROW_SIDES;
            const float angle1 = 2.0f * Constants::Pi * static_cast<float>(side + 1) / FACING_ARROW_SIDES;
            const float angleMid = (angle0 + angle1) * 0.5f;

            const Vector3 radial0 = u * std::cos(angle0) + v * std::sin(angle0);
            const Vector3 radial1 = u * std::cos(angle1) + v * std::sin(angle1);
            const Vector3 radialMid = u * std::cos(angleMid) + v * std::sin(angleMid);

            // Shaft side and back cap.
            const Vector3 shaftBack0 = origin + radial0 * shaftRadius;
            const Vector3 shaftBack1 = origin + radial1 * shaftRadius;
            const Vector3 shaftFront0 = headBase + radial0 * shaftRadius;
            const Vector3 shaftFront1 = headBase + radial1 * shaftRadius;

            AddShadedTriangle(vertices, shaftBack0, shaftBack1, shaftFront1, radialMid, FACING_ARROW_COLOR);
            AddShadedTriangle(vertices, shaftBack0, shaftFront1, shaftFront0, radialMid, FACING_ARROW_COLOR);
            AddShadedTriangle(vertices, origin, shaftBack1, shaftBack0, backward, FACING_ARROW_COLOR);

            // Head side and base.
            const Vector3 headEdge0 = headBase + radial0 * headRadius;
            const Vector3 headEdge1 = headBase + radial1 * headRadius;

            AddShadedTriangle(vertices, headEdge0, headEdge1, tip, Vector3Math::Normalized(radialMid + direction * headSlope), FACING_ARROW_COLOR);
            AddShadedTriangle(vertices, headBase, headEdge1, headEdge0, backward, FACING_ARROW_COLOR);
        }
    }

    // A pyramid from the eye along the view, with a triangle past its top
    // edge marking up.
    void AddCameraFrustum(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const PixelScale& scale,
        const Vector3& eye,
        const ComponentCamera& camera,
        const float aspectRatio
    ) {
        const Vector3 forward = Vector3Math::Normalized(CameraSystem::GetCameraForwardEngineSpace(camera.yaw, camera.pitch));
        const float yawRadians = camera.yaw * Constants::DegToRad;
        const Vector3 left = {std::cos(yawRadians), 0.0f, -std::sin(yawRadians)};
        const Vector3 up = Vector3Math::Normalized(Vector3Math::Cross(forward, left));

        const float halfHeight = CAMERA_FRUSTUM_DEPTH * std::tan(camera.fov * Constants::DegToRad * 0.5f);
        const float halfWidth = halfHeight * aspectRatio;
        const Vector3 farCentre = eye + forward * CAMERA_FRUSTUM_DEPTH;

        const Vector3 corners[4] = {
            farCentre + left * halfWidth + up * halfHeight,
            farCentre - left * halfWidth + up * halfHeight,
            farCentre - left * halfWidth - up * halfHeight,
            farCentre + left * halfWidth - up * halfHeight
        };

        for (int i = 0; i < 4; ++i) {
            AddBeam(vertices, scale, eye, corners[i], CAMERA_FRUSTUM_COLOR);
            AddBeam(vertices, scale, corners[i], corners[(i + 1) % 4], CAMERA_FRUSTUM_COLOR);
        }

        const Vector3 upTip = farCentre + up * (halfHeight * 1.6f);
        const Vector3 upLeft = farCentre + left * (halfWidth * 0.4f) + up * (halfHeight * 1.1f);
        const Vector3 upRight = farCentre - left * (halfWidth * 0.4f) + up * (halfHeight * 1.1f);

        AddBeam(vertices, scale, upLeft, upTip, CAMERA_FRUSTUM_COLOR);
        AddBeam(vertices, scale, upTip, upRight, CAMERA_FRUSTUM_COLOR);
        AddBeam(vertices, scale, upRight, upLeft, CAMERA_FRUSTUM_COLOR);
    }
}

void RendererCore::BuildGpuEditorGizmos(
    const ID cameraOwner,
    const ComponentTransform& viewTransform,
    const ComponentCamera& viewCamera
) {
    const Level& level = LevelManager::CurrentLevel();

    gpuEditorGizmoTriangles.clear();

    PixelScale scale;
    scale.eye = viewTransform.position;
    scale.forward = Vector3Math::Normalized(CameraSystem::GetCameraForwardEngineSpace(viewCamera.yaw, viewCamera.pitch));
    scale.worldPerPixelPerDepth = screenHeight > 0
        ? 2.0f * std::tan(viewCamera.fov * Constants::DegToRad * 0.5f) / static_cast<float>(screenHeight)
        : 0.0f;

    for (const ComponentTransform& transform : level.transforms.components) {
        if (transform.ownerID == cameraOwner) continue;

        const Vector3 facing = QuaternionMath::Rotate(transform.rotation, {0.0f, 0.0f, 1.0f});
        if (Vector3Math::LengthSquared(facing) <= Constants::Epsilon) continue;

        AddFacingArrow(
            gpuEditorGizmoTriangles,
            scale,
            transform.position + Vector3{0.0f, FACING_ARROW_LIFT, 0.0f},
            Vector3Math::Normalized(facing)
        );
    }

    // The aspect the camera would get if it were the one drawing.
    const float aspectRatio = screenHeight > 0 ? static_cast<float>(screenWidth) / static_cast<float>(screenHeight) : 1.0f;

    for (const ComponentCamera& camera : level.cameras.components) {
        if (camera.ownerID == cameraOwner) continue;

        const ComponentTransform* transform = level.transforms.Get(camera.ownerID);

        // Where the renderer puts the eye: the feet plus an active
        // controller's eye height.
        Vector3 eye = transform->position;

        for (const ComponentPlayerController& controller : level.playerControllers.components) {
            if (controller.ownerID != camera.ownerID || !controller.isActive) continue;

            eye.y += controller.eyeHeight;
            break;
        }

        AddCameraFrustum(gpuEditorGizmoTriangles, scale, eye, camera, aspectRatio);
    }

    editorGizmoVertexCount = static_cast<int>(gpuEditorGizmoTriangles.size());
}

// ============================================================================
// Visibility culling
// ============================================================================

void RendererCore::UpdateVisibleSectors(
    const Level& level,
    const ComponentCamera& camera,
    const ComponentTransform& renderCameraTransform
) {
    // The camera entity's own sector as the hint - it's almost always right.
    const int cameraSector = MapQueries::FindSectorContainingPoint(
        level.sectors,
        {renderCameraTransform.position.x, renderCameraTransform.position.z},
        renderCameraTransform.sectorIndex
    );

    UpdateVisibleSectorsFrom(level, cameraSector, camera.projection * camera.view);
}

void RendererCore::UpdateVisibleSectorsFrom(const Level& level, const int startSector, const Matrix4& viewProjection) {
    const size_t sectorCount = level.sectors.size();

    MapQueries::GetVisibleSectors(
        level,
        startSector,
        viewProjection,
        static_cast<float>(screenWidth),
        static_cast<float>(screenHeight),
        visibleSectors
    );

    sectorVisible.assign(sectorCount, 0);
    sectorShowsEntities.assign(sectorCount, 0);

    for (const Sector* sector : visibleSectors) {
        const size_t index = static_cast<size_t>(sector - level.sectors.data());

        sectorVisible[index] = 1;
        sectorShowsEntities[index] = 1;

        for (const Sector* neighbour : sector->neighbors)
            if (neighbour != nullptr) sectorShowsEntities[static_cast<size_t>(neighbour - level.sectors.data())] = 1;
    }

    visibilityCulling = true;
}

// Indices the culling data doesn't cover (outside every sector, or a level
// that changed size since the last frame) are drawn, never hidden.
bool RendererCore::IsSectorCulled(const int sectorIndex) const {
    return visibilityCulling &&
           sectorIndex >= 0 &&
           sectorIndex < static_cast<int>(sectorVisible.size()) &&
           !sectorVisible[sectorIndex];
}

// A wall is seen from one of its sectors, so it is hidden only when every
// sector it has is. A wall with no sector at all is always drawn.
bool RendererCore::IsWallCulled(const Level& level, const Wall& wall) const {
    if (!visibilityCulling) return false;

    bool hasSector = false;

    for (const ID sectorID : {wall.frontSector, wall.backSector}) {
        const Sector* sector = MapQueries::GetSectorByID(level, sectorID);
        if (sector == nullptr) continue;

        hasSector = true;
        if (!IsSectorCulled(static_cast<int>(sector - level.sectors.data()))) return false;
    }

    return hasSector;
}

bool RendererCore::IsEntityCulled(const ComponentTransform& transform) const {
    return visibilityCulling &&
           transform.sectorIndex >= 0 &&
           transform.sectorIndex < static_cast<int>(sectorShowsEntities.size()) &&
           !sectorShowsEntities[transform.sectorIndex];
}

// ============================================================================
// Camera
// ============================================================================

RendererCore::FrameCamera RendererCore::ResolveFrameCamera(Level& level, ComponentCamera*& camera, ComponentTransform*& cameraTransform) {
    camera = nullptr;
    cameraTransform = nullptr;

    if (useEditorCamera) {
        camera = GetEditorCamera();
        cameraTransform = GetEditorCameraTransform();

        if (camera == nullptr || cameraTransform == nullptr) [[unlikely]] {
            spdlog::error("{}::Update failed: editor camera was not created", GetName());
            return FrameCamera::Failed;
        }

        return FrameCamera::Ready;
    }

    camera = LevelSystem::GetActiveCamera(level);

    if (camera == nullptr || !camera->ownerEnabled) [[unlikely]] return FrameCamera::NoCamera;

    cameraTransform = level.transforms.Get(camera->ownerID);
    return FrameCamera::Ready;
}

ComponentTransform RendererCore::ComputeRenderCameraTransform(Level& level, ComponentCamera& cameraRef, const ComponentTransform& cameraTransformRef) {
    ComponentCamera* camera = &cameraRef;
    const ComponentTransform* cameraTransform = &cameraTransformRef;

    ComponentTransform renderCameraTransform = *cameraTransform;

    if (!useEditorCamera) {
        float eyeHeight = 0.0f;

        for (const ComponentPlayerController &controller:
             level.playerControllers.components) {
            if (!controller.isActive) continue;

            if (controller.ownerID == camera->ownerID) {
                eyeHeight = controller.eyeHeight;
                break;
            }
        }

        const float actualTransformY = cameraTransform->position.y;

        if (!camera->hasPreviousTransformY) {
            camera->previousTransformY = actualTransformY;
            camera->hasPreviousTransformY = true;
        }

        const float transformDeltaY = actualTransformY - camera->previousTransformY;

        const ComponentCollider *collider = level.colliders.Get(camera->ownerID);

        const ComponentRigidbody *rigidbody = level.rigidbodies.Get(camera->ownerID);

        // A floor rising under the body lifts it every frame: that's the
        // lift's own motion, not a step, so it isn't smoothed.
        bool onRisingFloor = false;
        const int cameraSectorIndex = cameraTransform->sectorIndex;

        if (cameraSectorIndex >= 0 && cameraSectorIndex < static_cast<int>(level.sectors.size())) {
            const Sector &cameraSector = level.sectors[cameraSectorIndex];
            const int floorIndex = MapQueries::FindFloorAtHeight(
                cameraSector, {cameraTransform->position.x, cameraTransform->position.z}, actualTransformY);

            onRisingFloor = floorIndex >= 0 && cameraSector.floors[floorIndex].floorDelta > 0.0f;
        }

        // Detect the upward teleport performed by PhysicsSystem.
        const bool steppedUp =
                camera->smoothStep &&
                !onRisingFloor &&
                collider != nullptr &&
                rigidbody != nullptr &&
                rigidbody->isGrounded &&
                collider->stepSize > 0.0f &&
                transformDeltaY > 0.01f &&
                transformDeltaY <=
                collider->stepSize + Constants::Epsilon;

        if (steppedUp) {
            float previousVisualY = camera->previousTransformY;

            // Preserve an unfinished smoothing animation.
            if (camera->isStepping)
                previousVisualY = camera->smoothStepStartY + camera->stepOffsetY +
                    (camera->previousTransformY - camera->smoothStepTargetY);


            camera->smoothStepStartY = previousVisualY;
            camera->smoothStepTargetY = actualTransformY;
            camera->stepOffsetY = 0.0f;
            camera->isStepping = true;
        }

        float currentCameraY = actualTransformY;

        if (camera->smoothStep && camera->isStepping) {
            const float targetOffset = camera->smoothStepTargetY - camera->smoothStepStartY;

            const float interpolation = 1.0f - std::exp(-std::max(camera->smoothingStrength,0.01f) *GameTime::deltaTime);

            camera->stepOffsetY += (targetOffset - camera->stepOffsetY) * interpolation;

            // Apply later jumping/falling directly.
            const float movementAfterStep = actualTransformY - camera->smoothStepTargetY;

            currentCameraY = camera->smoothStepStartY + camera->stepOffsetY + movementAfterStep;

            if (std::abs(targetOffset - camera->stepOffsetY) <= 0.001f) {
                currentCameraY = actualTransformY;

                camera->smoothStepStartY = actualTransformY;
                camera->smoothStepTargetY = actualTransformY;
                camera->stepOffsetY = 0.0f;
                camera->isStepping = false;
            }
        }
        else if (!camera->smoothStep) {
            camera->smoothStepStartY = actualTransformY;
            camera->smoothStepTargetY = actualTransformY;
            camera->stepOffsetY = 0.0f;
            camera->isStepping = false;
        }

        camera->previousTransformY = actualTransformY;

        renderCameraTransform.position.y = currentCameraY + eyeHeight;
    }

    return renderCameraTransform;
}

// ============================================================================
// Gateways
// ============================================================================

namespace RendererGpu::GatewayMath {
    // How far the box reaches behind its wall: more than the near plane, so
    // a camera standing in the opening still sees through it.
    float BoxDepth(const ComponentCamera& camera) {
        return std::max(camera.nearPlane * 4.0f, 0.25f);
    }

    // Only a camera this near the opening needs the whole box. Further away
    // its sides could show beside the opening (on a wall in line with them,
    // past a two-sided gateway), where the view through it has nothing.
    bool NeedsWholeBox(const Gateways::Opening& opening, const Vector2 eye, const float boxDepth) {
        return opening.SideDistance(eye) < boxDepth * 2.0f;
    }

    // Opening::MapPoint undone: a point past the exit wall to where it is
    // seen, beyond the opening.
    Matrix4 ExitToEntry(const Gateways::Opening& opening) {
        const float c = opening.cosTurn;
        const float s = opening.sinTurn;
        const Vector2 entry = opening.centre;
        const Vector2 exit = opening.exitCentre;

        Matrix4 matrix = Matrix4::Identity();
        matrix.m[0][0] = c;
        matrix.m[0][2] = s;
        matrix.m[0][3] = entry.x - c * exit.x - s * exit.y;
        matrix.m[1][3] = -opening.heightOffset;
        matrix.m[2][0] = -s;
        matrix.m[2][2] = c;
        matrix.m[2][3] = entry.y + s * exit.x - c * exit.y;
        return matrix;
    }

    std::array<Vector3, 4> OpeningCorners(const Gateways::Opening& opening) {
        const Vector2 start = opening.StartPoint();
        const Vector2 end = opening.EndPoint();

        return {
            Vector3{start.x, opening.bottom, start.y},
            Vector3{end.x, opening.bottom, end.y},
            Vector3{end.x, opening.top, end.y},
            Vector3{start.x, opening.top, start.y}
        };
    }

    // The pixels (x, y, width, height from the bottom left) the opening can
    // cover, within `bounds`. Corners at or behind the eye could land
    // anywhere, so then it is all of `bounds`. False when it is off them.
    bool OpeningScreenRect(const Gateways::Opening& opening, const Matrix4& viewProjection,
                           const int screenWidth, const int screenHeight, const int bounds[4], int out[4]) {
        const Matrix4& m = viewProjection;

        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
        bool any = false;
        bool behindEye = false;

        for (const Vector3& corner : OpeningCorners(opening)) {
            const float x = m.m[0][0] * corner.x + m.m[0][1] * corner.y + m.m[0][2] * corner.z + m.m[0][3];
            const float y = m.m[1][0] * corner.x + m.m[1][1] * corner.y + m.m[1][2] * corner.z + m.m[1][3];
            const float w = m.m[3][0] * corner.x + m.m[3][1] * corner.y + m.m[3][2] * corner.z + m.m[3][3];

            if (w <= 0.0001f) {
                behindEye = true;
                break;
            }

            const float px = (x / w + 1.0f) * 0.5f * static_cast<float>(screenWidth);
            const float py = (y / w + 1.0f) * 0.5f * static_cast<float>(screenHeight);

            if (!any) {
                minX = maxX = px;
                minY = maxY = py;
                any = true;
                continue;
            }

            minX = std::min(minX, px);
            maxX = std::max(maxX, px);
            minY = std::min(minY, py);
            maxY = std::max(maxY, py);
        }

        int x0 = bounds[0];
        int y0 = bounds[1];
        int x1 = bounds[0] + bounds[2];
        int y1 = bounds[1] + bounds[3];

        if (!behindEye && any) {
            // Clamped before the int cast; a pixel of padding for rounding.
            const auto clampPixel = [](const float value, const int limit) {
                return static_cast<int>(std::clamp(value, -2.0f, static_cast<float>(limit) + 2.0f));
            };

            x0 = std::max(x0, clampPixel(std::floor(minX), screenWidth) - 1);
            y0 = std::max(y0, clampPixel(std::floor(minY), screenHeight) - 1);
            x1 = std::min(x1, clampPixel(std::ceil(maxX), screenWidth) + 1);
            y1 = std::min(y1, clampPixel(std::ceil(maxY), screenHeight) + 1);
        }

        if (x1 <= x0 || y1 <= y0) return false;

        out[0] = x0;
        out[1] = y0;
        out[2] = x1 - x0;
        out[3] = y1 - y0;
        return true;
    }

    std::array<float, 8 * 4> BoxCorners(const Gateways::Opening& opening, const float depthBehind) {
        const std::array<Vector3, 4> front = OpeningCorners(opening);
        const Vector3 back = {-opening.inward.x * depthBehind, 0.0f, -opening.inward.y * depthBehind};

        std::array<float, 8 * 4> corners{};
        for (int i = 0; i < 4; ++i) {
            const Vector3& corner = front[i];
            const Vector3 behind = corner + back;

            corners[i * 4 + 0] = corner.x;
            corners[i * 4 + 1] = corner.y;
            corners[i * 4 + 2] = corner.z;
            corners[i * 4 + 3] = 1.0f;

            corners[(i + 4) * 4 + 0] = behind.x;
            corners[(i + 4) * 4 + 1] = behind.y;
            corners[(i + 4) * 4 + 2] = behind.z;
            corners[(i + 4) * 4 + 3] = 1.0f;
        }

        return corners;
    }
}

std::vector<RendererCore::SeenGateway> RendererCore::CollectSeenGateways(const GatewayView& view) const {
    const Matrix4 viewProjection = view.camera.projection * view.camera.view;
    const Vector2 eye = {view.eye.x, view.eye.z};

    std::vector<SeenGateway> seen;

    for (const Gateways::Opening& opening : gatewayOpenings) {
        // Looked out of, so it is behind this view.
        if (opening.wall == view.exitWall) continue;
        if (IsSectorCulled(opening.sectorIndex)) continue;

        // Only its room's side shows the view; behind it is the void.
        if (opening.SideDistance(eye) <= 0.0f) continue;

        if (view.clipped) {
            bool inFront = false;
            for (const Vector3& corner : GatewayMath::OpeningCorners(opening))
                inFront = inFront || corner.x * view.clipPlane.x + corner.y * view.clipPlane.y +
                                     corner.z * view.clipPlane.z + view.clipPlane.w > 0.0f;
            if (!inFront) continue;
        }

        SeenGateway entry{&opening, {}};
        if (!GatewayMath::OpeningScreenRect(opening, viewProjection, screenWidth, screenHeight, view.scissor, entry.scissor)) continue;

        seen.push_back(entry);
    }

    return seen;
}

RendererCore::GatewayView RendererCore::ViewThrough(const GatewayView& view, const SeenGateway& seen) {
    const Gateways::Opening& opening = *seen.opening;

    GatewayView through;
    through.camera = view.camera;
    through.camera.yaw = opening.MapYaw(view.camera.yaw);
    through.camera.forward = opening.MapDirection(view.camera.forward);
    through.eye = opening.MapPoint(view.eye);

    // This view's matrix composed with the gateway's, not rebuilt from
    // the moved eye and yaw: that differs by a few thousandths, enough
    // for the opening's edges and the view's to miss by a pixel.
    through.camera.view = view.camera.view * GatewayMath::ExitToEntry(opening);

    through.exitWall = opening.exit;
    through.clipPlane = {
        opening.exitInward.x,
        0.0f,
        opening.exitInward.y,
        -(opening.exitInward.x * opening.exitCentre.x + opening.exitInward.y * opening.exitCentre.y) + GatewayMath::CLIP_EPSILON
    };
    through.clipped = true;
    through.depth = view.depth + 1;
    std::copy(std::begin(seen.scissor), std::end(seen.scissor), through.scissor);

    return through;
}

// ============================================================================
// Models
// ============================================================================

namespace RendererGpu::Models {
    void ToColumnMajor(const Matrix4& matrix, float out[16]) {
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                out[column * 4 + row] = matrix.m[row][column];
    }

    // Inverse transpose of the upper 3x3, column major. It is the cofactor
    // matrix divided by the determinant; the shader normalizes the result, so
    // a degenerate (zero scale) matrix keeps its cofactors instead of failing.
    void ToNormalMatrix(const Matrix4& matrix, float out[9]) {
        const auto& a = matrix.m;

        float cofactor[3][3];
        cofactor[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
        cofactor[0][1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
        cofactor[0][2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
        cofactor[1][0] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
        cofactor[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
        cofactor[1][2] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
        cofactor[2][0] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
        cofactor[2][1] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
        cofactor[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];

        const float determinant = a[0][0] * cofactor[0][0] + a[0][1] * cofactor[0][1] + a[0][2] * cofactor[0][2];
        const float inverse = std::abs(determinant) > 1e-12f ? 1.0f / determinant : 1.0f;

        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                out[column * 3 + row] = cofactor[row][column] * inverse;
    }

    GpuModelInstance BuildModelInstance(const ComponentTransform& transform, const Vector3& offset, const Level& level, const bool fullbright) {
        const Quaternion q = transform.rotation.Normalized();

        // Rotation matrix columns.
        const float rotation[3][3] = {
            {1.0f - 2.0f * (q.y * q.y + q.z * q.z), 2.0f * (q.x * q.y + q.z * q.w), 2.0f * (q.x * q.z - q.y * q.w)},
            {2.0f * (q.x * q.y - q.z * q.w), 1.0f - 2.0f * (q.x * q.x + q.z * q.z), 2.0f * (q.y * q.z + q.x * q.w)},
            {2.0f * (q.x * q.z + q.y * q.w), 2.0f * (q.y * q.z - q.x * q.w), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)}
        };

        const float scale[3] = {transform.scale.x, transform.scale.y, transform.scale.z};

        GpuModelInstance instance;

        for (int column = 0; column < 3; ++column) {
            // Normal matrix of rotation * scale is rotation * scale^-1, which
            // keeps normals perpendicular under nonuniform scale.
            const float inverseScale = std::abs(scale[column]) > Constants::Epsilon ? 1.0f / scale[column] : 0.0f;

            for (int row = 0; row < 3; ++row) {
                instance.modelMatrix[column * 4 + row] = rotation[column][row] * scale[column];
                instance.normalMatrix[column * 4 + row] = rotation[column][row] * inverseScale;
            }
        }

        const Vector3 position = transform.LocalToWorld(offset);

        instance.modelMatrix[12] = position.x;
        instance.modelMatrix[13] = position.y;
        instance.modelMatrix[14] = position.z;
        instance.modelMatrix[15] = 1.0f;

        instance.color = {1.0f, 1.0f, 1.0f, 1.0f};

        // Same sector light rule as sprites: light 255 = unlit color.
        if (transform.sectorIndex >= 0 && transform.sectorIndex < static_cast<int>(level.sectors.size())) {
            const Vector3 light = fullbright ? Vector3{255.0f, 255.0f, 255.0f} : level.sectors[transform.sectorIndex].light;

            instance.color = {
                std::max(light.x / 255.0f, 0.0f),
                std::max(light.y / 255.0f, 0.0f),
                std::max(light.z / 255.0f, 0.0f),
                1.0f
            };
        }

        return instance;
    }
}

// ============================================================================
// UI
// ============================================================================

void RendererCore::ForEachUIDraw(
    Level& level,
    const std::function<void(const ComponentUITransform& transform, const Vector4& color255, const std::string& texture)>& sprite,
    const std::function<void(const ComponentUIText& text, const ComponentUITransform& transform)>& text
) {
    // Every Sprite by Draw Order, then every Text.
    UISystem::ForEachInDrawOrder(level,
        [&](const ComponentUISprite& uiSprite) {
            // The owner's UI Button state look (white and empty without one).
            const Vector4 color = {
                uiSprite.color.x * uiSprite.buttonTint.x, uiSprite.color.y * uiSprite.buttonTint.y,
                uiSprite.color.z * uiSprite.buttonTint.z, uiSprite.color.w * uiSprite.buttonTint.w
            };

            if (!uiSprite.isActive || !uiSprite.ownerEnabled || color.w <= 0.0f) return;
            if (level.IsUIHidden(uiSprite.ownerID)) return;

            const std::string& texture = uiSprite.buttonTexture.empty() ? uiSprite.texture : uiSprite.buttonTexture;
            if (texture.empty() && !uiSprite.fillWhenEmpty) return;

            const ComponentUITransform* transform = level.ui_transforms.Get(uiSprite.ownerID);

            if (transform == nullptr) [[unlikely]] {
                spdlog::error("UI Sprite does not have UI transform");
                return;
            }

            sprite(*transform, {color.x * 255.0f, color.y * 255.0f, color.z * 255.0f, color.w * 255.0f}, texture);
        },
        [&](const ComponentUIText& uiText) {
            if (!uiText.ownerEnabled || level.IsUIHidden(uiText.ownerID)) return;

            const ComponentUITransform* transform = level.ui_transforms.Get(uiText.ownerID);

            if (transform == nullptr) [[unlikely]] {
                spdlog::error("UI Text does not have UI transform");
                return;
            }

            text(uiText, *transform);
        });
}

// ============================================================================
// 2D editor labels
// ============================================================================

void RendererCore::LayoutEditor2DLabels(const Editor2D::Frame& frame, const int layer, std::vector<PlacedEditor2DLabel>& placed,
                                        std::vector<Editor2D::GpuOverlayVertex>& pills) {
    placed.clear();
    pills.clear();

    if (frame.labels.empty() || defaultFontFace == nullptr) return;

    placed.reserve(frame.labels.size());

    const float width = static_cast<float>(frame.view.width);
    const float height = static_cast<float>(frame.view.height);

    const auto addRect = [&pills](const float x, const float y, const float w, const float h, const Vector4& color) {
        const Editor2D::GpuOverlayVertex v0{{x, y, 0, 0}, color};
        const Editor2D::GpuOverlayVertex v1{{x + w, y, 0, 0}, color};
        const Editor2D::GpuOverlayVertex v2{{x + w, y + h, 0, 0}, color};
        const Editor2D::GpuOverlayVertex v3{{x, y + h, 0, 0}, color};
        pills.insert(pills.end(), {v0, v1, v2, v0, v2, v3});
    };

    for (const Editor2D::Label& label : frame.labels) {
        if (label.text.empty() || static_cast<int>(label.layer) != layer) continue;

        const FT_Face face = GetFontFace(label.font);
        if (face == nullptr) continue;

        const unsigned pixelSize = PixelSizeFor(label.pixelSize);
        if (pixelSize == 0) continue;
        SetFontPixelSize(face, pixelSize);
        const float ascender = static_cast<float>(face->size->metrics.ascender) / 64.0f;
        const float descender = static_cast<float>(face->size->metrics.descender) / 64.0f;
        const float textHeight = std::ceil(ascender - descender);

        if (label.style == Editor2D::LabelStyle::TopLeft) {
            placed.push_back({&label, face, label.anchor.x, label.anchor.y + ascender});
            continue;
        }

        const float textWidth = std::ceil(MeasureTextLine(label.font, label.pixelSize, label.text));

        if (label.style == Editor2D::LabelStyle::Tag) {
            constexpr float PADDING = 4.0f;
            const float tagW = textWidth + PADDING * 2.0f;
            const float tagH = textHeight + PADDING * 2.0f;
            const float x = std::clamp(label.anchor.x, 2.0f, std::max(2.0f, width - tagW - 2.0f));
            const float y = std::clamp(label.anchor.y, 2.0f, std::max(2.0f, height - tagH - 2.0f));

            addRect(x, y, tagW, tagH, {20.0f / 255.0f, 20.0f / 255.0f, 26.0f / 255.0f, 210.0f / 255.0f});
            placed.push_back({&label, face, x + PADDING, y + PADDING + ascender});
            continue;
        }

        if (label.style == Editor2D::LabelStyle::LengthPill) {
            constexpr float PAD_X = 5.0f;
            constexpr float PAD_Y = 1.0f;

            const Vector2 mid = label.anchor;
            if (mid.x < 0.0f || mid.y < 0.0f || mid.x > width || mid.y > height) continue;

            const float pillW = textWidth + PAD_X * 2.0f;
            const float pillH = textHeight + PAD_Y * 2.0f;
            const float x = std::floor(mid.x - pillW * 0.5f);
            const float y = std::floor(mid.y - pillH * 0.5f);

            // Corners cut by one pixel.
            const Vector4 pillColor = {18.0f / 255.0f, 18.0f / 255.0f, 22.0f / 255.0f, 200.0f / 255.0f};
            addRect(x, y + 1.0f, pillW, pillH - 2.0f, pillColor);
            addRect(x + 1.0f, y, pillW - 2.0f, 1.0f, pillColor);
            addRect(x + 1.0f, y + pillH - 1.0f, pillW - 2.0f, 1.0f, pillColor);

            placed.push_back({&label, face, x + PAD_X, y + PAD_Y + ascender});
            continue;
        }

        // Centred just above the anchor, kept on screen.
        const float maxX = std::max(2.0f, width - textWidth - 2.0f);
        const float maxY = std::max(2.0f, height - textHeight - 2.0f);
        const float x = std::clamp(label.anchor.x - textWidth * 0.5f, 2.0f, maxX);
        const float y = std::clamp(label.anchor.y - textHeight - 8.0f, 2.0f, maxY);
        placed.push_back({&label, face, x, y + ascender});
    }
}

// The background image, tiled from the world origin so the tiles stay put on
// the map as the view moves.
bool RendererCore::BuildEditor2DBackground(const Editor2D::Frame& frame, const GPUTexture& texture,
                                           std::vector<Editor2D::GpuOverlayVertex>& quad) {
    const Editor2D::TiledBackground& background = frame.background;
    const Vector2 screen = {static_cast<float>(frame.view.width), static_cast<float>(frame.view.height)};

    const float tileWidth = static_cast<float>(texture.width) * background.tileScale;
    const float tileHeight = static_cast<float>(texture.height) * background.tileScale;

    // Tiles this small would read as a flat colour anyway.
    constexpr float MIN_TILE_PIXELS = 2.0f;

    if (tileWidth < MIN_TILE_PIXELS || tileHeight < MIN_TILE_PIXELS) return false;

    const float startX = background.origin.x - std::ceil(background.origin.x / tileWidth) * tileWidth;
    const float startY = background.origin.y - std::ceil(background.origin.y / tileHeight) * tileHeight;
    const float endU = (screen.x - startX) / tileWidth;
    const float endV = (screen.y - startY) / tileHeight;
    const Vector4 color = {1.0f, 1.0f, 1.0f, std::clamp(background.opacity, 0.0f, 1.0f)};

    const Editor2D::GpuOverlayVertex v0{{startX, startY, 0.0f, 0.0f}, color};
    const Editor2D::GpuOverlayVertex v1{{screen.x, startY, endU, 0.0f}, color};
    const Editor2D::GpuOverlayVertex v2{{screen.x, screen.y, endU, endV}, color};
    const Editor2D::GpuOverlayVertex v3{{startX, screen.y, 0.0f, endV}, color};
    quad = {v0, v1, v2, v0, v2, v3};
    return true;
}
