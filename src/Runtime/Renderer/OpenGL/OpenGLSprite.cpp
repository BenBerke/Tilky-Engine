#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Runtime/Gameplay/ParticleSystem.hpp"

void OpenGL::BuildGpuSprites() {
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

        if (transform == nullptr) [[unlikely]] continue;
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

    spriteCount = static_cast<GLsizei>(gpuSprites.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, spriteSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuSprites.size() * sizeof(GpuSprite),
        gpuSprites.empty() ? nullptr : gpuSprites.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,spriteSSBO);
}