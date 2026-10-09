#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <array>
#include <cmath>

#include "Headers/Math/Vector/Vector3Math.hpp"

namespace {
    // Matches SKY_NONE in Background.fs.glsl.
    constexpr int SKY_NONE = -1;

    constexpr std::array<const char*, SKY_FACE_COUNT> FACE_UNIFORMS = {
        "faceRight", "faceLeft", "faceUp", "faceDown", "faceFront", "faceBack"
    };
}

// The standalone GL texture for an Assets-relative path, or 0 if it has none.
GLuint OpenGL::GetSkyTextureID(const std::string& fileName, const GLint wrapS, const GLint wrapT, Vector2* outSize) {
    const int textureIndex = GetOrCreateTextureIndex(fileName);
    if (textureIndex < 0 || textureIndex >= GetTextureCount()) return 0;

    const GPUTexture& texture = GetTexture(textureIndex);
    if (texture.id == 0) return 0;

    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);

    if (outSize != nullptr) *outSize = {static_cast<float>(texture.width), static_cast<float>(texture.height)};

    return texture.id;
}

void OpenGL::DrawBackground(const ComponentCamera& camera, const SkySettings& sky) {
    if (backgroundShader == nullptr) return;

    // Pick the textures first; anything missing falls back to the solid color.
    int mode = static_cast<int>(sky.mode);
    Vector2 textureSize = {1.0f, 1.0f};
    std::array<GLuint, SKY_FACE_COUNT> textureIDs = {};

    if (sky.mode == SkyMode::Cubemap) {
        for (int face = 0; face < SKY_FACE_COUNT; ++face) {
            textureIDs[face] = GetSkyTextureID(sky.cubemapFaces[face], GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, nullptr);
            if (textureIDs[face] == 0) mode = SKY_NONE;
        }
    }
    else {
        // Equirectangular clamps at the poles; DOOM's cylinder repeats both ways.
        const GLint wrapT = sky.mode == SkyMode::Cylinder ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        textureIDs[0] = GetSkyTextureID(sky.texture, GL_REPEAT, wrapT, &textureSize);
        if (textureIDs[0] == 0) mode = SKY_NONE;
    }

    // Same basis as Matrix4::LookAt, so the sky lines up with the world.
    const Vector3 forward = Vector3Math::Normalized(camera.forward);
    const Vector3 right = Vector3Math::Normalized(Vector3Math::Cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vector3 up = Vector3Math::Cross(right, forward);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    backgroundShader->use();
    const GLuint program = backgroundShader->ID;

    glUniform1i(glGetUniformLocation(program, "skyMode"), mode);
    glUniform3f(glGetUniformLocation(program, "cameraForward"), forward.x, forward.y, forward.z);
    glUniform3f(glGetUniformLocation(program, "cameraRight"), right.x, right.y, right.z);
    glUniform3f(glGetUniformLocation(program, "cameraUp"), up.x, up.y, up.z);
    glUniform1f(glGetUniformLocation(program, "tanHalfFovY"), std::tan(camera.fov * Constants::DegToRad * 0.5f));
    glUniform1f(glGetUniformLocation(program, "aspect"), camera.aspectRatio);
    glUniform1f(glGetUniformLocation(program, "rotation"), (sky.rotation + sky.spin) * Constants::DegToRad);
    glUniform1f(glGetUniformLocation(program, "horizonOffset"), sky.horizonOffset * Constants::DegToRad);
    glUniform3f(glGetUniformLocation(program, "tint"), sky.tint.x / 255.0f, sky.tint.y / 255.0f, sky.tint.z / 255.0f);
    glUniform3f(glGetUniformLocation(program, "fallbackColor"),
                sky.fallbackColor.x / 255.0f, sky.fallbackColor.y / 255.0f, sky.fallbackColor.z / 255.0f);
    glUniform2f(glGetUniformLocation(program, "skyTextureSize"), textureSize.x, textureSize.y);

    // Every sampler gets its own unit even when unused, so none alias.
    glUniform1i(glGetUniformLocation(program, "skyTexture"), 0);
    for (int face = 0; face < SKY_FACE_COUNT; ++face)
        glUniform1i(glGetUniformLocation(program, FACE_UNIFORMS[face]), face + 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sky.mode == SkyMode::Cubemap ? 0 : textureIDs[0]);
    for (int face = 0; face < SKY_FACE_COUNT; ++face) {
        glActiveTexture(GL_TEXTURE0 + face + 1);
        glBindTexture(GL_TEXTURE_2D, sky.mode == SkyMode::Cubemap ? textureIDs[face] : 0);
    }

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    for (int unit = SKY_FACE_COUNT; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glEnable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
}
