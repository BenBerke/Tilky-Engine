#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

// No longer `const`: resolving backgroundTextureFileName can lazily load
// a texture on first use (see GetOrCreateTextureIndex), mirroring the
// editor-side EditorTextureCache. OpenGL.hpp's declaration needs the same
// `const` dropped - see the accompanying notes.
void OpenGL::DrawBackground(const float playerAngle) {
    if (!backgroundShader || backgroundTextureFileName.empty()) return;

    const int textureIndex = GetOrCreateTextureIndex(backgroundTextureFileName);
    if (textureIndex < 0 || textureIndex >= GetTextureCount()) return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    backgroundShader->use();

    glUniform1f(
        glGetUniformLocation(backgroundShader->ID, "playerAngle"),
        -playerAngle
    );

    glActiveTexture(GL_TEXTURE0);

    glBindTexture(
        GL_TEXTURE_2D,
        GetTexture(textureIndex).id
    );

    glBindVertexArray(VAO);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}