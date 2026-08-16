#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

bool Vulkan::InitializeFont() {
    using namespace VulkanRendererInternal;

    if (FT_Init_FreeType(&ft)) {
        spdlog::critical("FT_Init_FreeType failed. FreeType could not be initialized.");
        return false;
    }

    const auto fontPath =
        ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

    if (FT_New_Face(ft, fontPath.string().c_str(), 0, &face)) {
        spdlog::critical(
            "FT_New_Face failed. Could not load font face. Tried path: {}",
            fontPath.string()
        );

        FT_Done_FreeType(ft);
        ft = nullptr;
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, 48);

    // Packs all 128 ASCII glyphs into one FONT_ATLAS_SIZE^2 single-channel
    // (R8, matching GL's GL_RED) image instead of GL's 128 individual
    // textures - see the Character/uvRect comment in Vulkan.hpp for why.
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(FONT_ATLAS_SIZE) * FONT_ATLAS_SIZE, 0);

    int cursorX = FONT_ATLAS_PADDING;
    int cursorY = FONT_ATLAS_PADDING;
    int shelfHeight = 0;
    int failedGlyphCount = 0;

    Characters.clear();

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            spdlog::warn(
                "Failed to load glyph '{}'. Text rendering may miss this character.",
                static_cast<char>(c)
            );

            failedGlyphCount++;
            continue;
        }

        const int glyphWidth = static_cast<int>(face->glyph->bitmap.width);
        const int glyphHeight = static_cast<int>(face->glyph->bitmap.rows);

        if (cursorX + glyphWidth + FONT_ATLAS_PADDING > FONT_ATLAS_SIZE) {
            cursorX = FONT_ATLAS_PADDING;
            cursorY += shelfHeight + FONT_ATLAS_PADDING;
            shelfHeight = 0;
        }

        if (cursorY + glyphHeight + FONT_ATLAS_PADDING > FONT_ATLAS_SIZE) {
            spdlog::error("Font atlas is full - could not pack glyph '{}'", static_cast<char>(c));
            continue;
        }

        // FT_LOAD_RENDER with the default 8-bit grayscale target produces
        // a tightly-packed bitmap (pitch == width) for essentially every
        // real-world font/size combination - same assumption the GL path
        // made implicitly by handing bitmap.buffer straight to
        // glTexImage2D with no separate row-stride parameter.
        if (glyphWidth > 0 && glyphHeight > 0) {
            for (int row = 0; row < glyphHeight; ++row) {
                const unsigned char* srcRow = face->glyph->bitmap.buffer + static_cast<size_t>(row) * glyphWidth;
                unsigned char* dstRow = atlasPixels.data() + static_cast<size_t>(cursorY + row) * FONT_ATLAS_SIZE + cursorX;
                std::memcpy(dstRow, srcRow, glyphWidth);
            }
        }

        // No half-texel inset here (unlike the main atlas in
        // BuildTextureAtlasFromLevel) - glyphs are small and rendered at
        // ~native size, so edge bleed from bilinear filtering is unlikely
        // to be visible. Add the same inset as the main atlas if it turns
        // out to matter for your font/size.
        const Vector4 uvRect{
            static_cast<float>(cursorX) / FONT_ATLAS_SIZE,
            static_cast<float>(cursorY) / FONT_ATLAS_SIZE,
            static_cast<float>(cursorX + glyphWidth) / FONT_ATLAS_SIZE,
            static_cast<float>(cursorY + glyphHeight) / FONT_ATLAS_SIZE
        };

        Character character{
            .uvRect = uvRect,
            .Size = Vector2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            .Bearing = Vector2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            .Advance = static_cast<unsigned int>(face->glyph->advance.x)
        };

        Characters.insert(std::pair<char, Character>(static_cast<char>(c), character));

        cursorX += glyphWidth + FONT_ATLAS_PADDING;
        shelfHeight = std::max(shelfHeight, glyphHeight);
    }

    GPUImageResources fontAtlas = CreateGPUImage(
        FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 1, vk::Format::eR8Unorm,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
    );

    fontAtlasImage = std::move(fontAtlas.image);
    fontAtlasMemory = std::move(fontAtlas.memory);

    UploadPixelsToImage(fontAtlasImage, atlasPixels.data(), FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 1, 1);

    fontAtlasView = device.createImageView(vk::ImageViewCreateInfo{
        .image = fontAtlasImage,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8Unorm,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    });

    // GL: GL_CLAMP_TO_EDGE wrap, GL_LINEAR min/mag, no mipmaps.
    fontAtlasSampler = device.createSampler(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 1.0f
    });

    const vk::DescriptorImageInfo fontImageInfo{
        .sampler = fontAtlasSampler,
        .imageView = fontAtlasView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    const vk::WriteDescriptorSet fontWrite{
        .dstSet = textSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &fontImageInfo
    };

    device.updateDescriptorSets(fontWrite, {});

    // Matches the GL version's placement exactly: FreeType is torn down
    // right after the glyphs are baked, not held for the renderer's
    // lifetime (see the comment in VulkanDestroy.cpp).
    FT_Done_Face(face);
    face = nullptr;

    FT_Done_FreeType(ft);
    ft = nullptr;

    if (failedGlyphCount > 0) {
        spdlog::warn(
            "Font initialized, but {} glyph(s) failed to load.",
            failedGlyphCount
        );
    } else {
        spdlog::info(
            "Font initialized successfully from: {}",
            fontPath.string()
        );
    }

    return true;
}

void Vulkan::RenderText(
    const std::string& text,
    float x,
    const float y,
    const Vector2 scale,
    const Vector3 color
) {
    using namespace VulkanRendererInternal;

    if (screenWidth <= 0 || screenHeight <= 0 || activeCommandBuffer == nullptr) {
        return;
    }

    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);

    // Same top-left-origin orthographic projection SetTextProjection()
    // built in OpenGLText.cpp, written out already column-major (GL
    // uploaded this row-major array with glUniformMatrix4fv(..., GL_TRUE,
    // ...), i.e. asked GL to transpose it on the way in - push constants
    // have no such flag, so the transpose has to happen here instead).
    TextPushConstants pushConstants{};
    pushConstants.projection[0] = 2.0f / w;
    pushConstants.projection[5] = -2.0f / h;
    pushConstants.projection[10] = -1.0f;
    pushConstants.projection[12] = -1.0f;
    pushConstants.projection[13] = 1.0f;
    pushConstants.projection[15] = 1.0f;
    // all other entries stay 0.0f from TextPushConstants{} value-init

    pushConstants.textColor[0] = color.x / 255.0f;
    pushConstants.textColor[1] = color.y / 255.0f;
    pushConstants.textColor[2] = color.z / 255.0f;

    vk::raii::CommandBuffer& cmd = *activeCommandBuffer;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, textPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, textPipelineLayout, 0, *textSet, {});
    cmd.pushConstants<TextPushConstants>(
        textPipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        pushConstants
    );

    // One draw call per glyph, same as the GL version's per-character
    // glBufferSubData+glDrawArrays loop - now that every glyph reads from
    // the same atlas+descriptor set, pipeline/descriptor binding above
    // only has to happen once per RenderText call rather than per
    // character (GL only rebound per-character because each glyph had
    // its own texture; the projection/color uniforms were already
    // hoisted outside its loop the same way).
    for (const char c : text) {
        const auto characterIt = Characters.find(c);

        if (characterIt == Characters.end()) {
            spdlog::warn(
                "Tried to render missing glyph '{}'. Skipping character.",
                c
            );
            continue;
        }

        const auto& [uvRect, size, bearing, advance] = characterIt->second;

        const float xPos = x + bearing.x * scale.x;
        const float yPos = y - bearing.y * scale.y;

        const float glyphW = size.x * scale.x;
        const float glyphH = size.y * scale.y;

        const float vertices[6][4] = {
            {xPos,           yPos,           uvRect.x, uvRect.y},
            {xPos,           yPos + glyphH,  uvRect.x, uvRect.w},
            {xPos + glyphW,  yPos + glyphH,  uvRect.z, uvRect.w},

            {xPos,           yPos,           uvRect.x, uvRect.y},
            {xPos + glyphW,  yPos + glyphH,  uvRect.z, uvRect.w},
            {xPos + glyphW,  yPos,           uvRect.z, uvRect.y}
        };

        std::memcpy(glyphVertexBuffers[currentFrame].mapped, vertices, sizeof(vertices));

        cmd.bindVertexBuffers(0, {*glyphVertexBuffers[currentFrame].buffer}, {vk::DeviceSize{0}});
        cmd.draw(6, 1, 0, 0);

        x += static_cast<float>(advance >> 6) * scale.x;
    }
}

void Vulkan::RenderTextRaw(
    const std::string& text,
    const Vector2 position,
    const Vector2 scale,
    const Vector3 color
) {
    RenderText(text, position.x, position.y, scale, color);
}

void Vulkan::RenderUIText(
    const ComponentUIText& text,
    const ComponentUITransform& transform
) {
    constexpr float fontPixelSize = 48.0f;
    constexpr float padding = 8.0f;
    constexpr float fontScale = 1.0f;

    const Vector2 textPosition = {
        transform.resolvedPosition.x + padding,
        transform.resolvedPosition.y + padding + fontPixelSize * fontScale
    };

    RenderTextRaw(
        text.text,
        textPosition,
        {fontScale, fontScale},
        {255.0f, 255.0f, 255.0f}
    );
}