#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3

#define MAX_WALL_TEXTURES 8

in vec2 vDecalUV;

in vec2 vSpriteUV;
flat in int vSpriteTextureIndex;

in vec2 vWallUV;
in vec2 vFlatUV;

flat in int vTextureIndex;
flat in int vFlatTextureIndex;
flat in vec4 vColor;

uniform sampler2D wallTextures[MAX_WALL_TEXTURES];
uniform int renderMode;

out vec4 FragColor;

vec4 SampleWallTexture(int textureIndex, vec2 uv) {
    switch (textureIndex) {
        case 0: return texture(wallTextures[0], uv);
        case 1: return texture(wallTextures[1], uv);
        case 2: return texture(wallTextures[2], uv);
        case 3: return texture(wallTextures[3], uv);
        case 4: return texture(wallTextures[4], uv);
        case 5: return texture(wallTextures[5], uv);
        case 6: return texture(wallTextures[6], uv);
        case 7: return texture(wallTextures[7], uv);
        default: return vec4(1.0);
    }
}

void main() {
    if (renderMode == RENDER_FLAT) {
        if (vFlatTextureIndex >= 0 && vFlatTextureIndex < MAX_WALL_TEXTURES) {
            vec4 texColor = SampleWallTexture(vFlatTextureIndex, vFlatUV);

            if (texColor.a < 0.1) {
                discard;
            }

            FragColor = texColor * vColor;
        }
        else {
            FragColor = vColor;
        }

        return;
    }

    if (renderMode == RENDER_WALL) {
        if (vTextureIndex >= 0 && vTextureIndex < MAX_WALL_TEXTURES) {
            vec4 texColor = SampleWallTexture(vTextureIndex, vWallUV);

            if (texColor.a < 0.1) {
                discard;
            }

            FragColor = texColor * vColor;
        }
        else {
            FragColor = vColor;
        }

        return;
    }
    if (renderMode == RENDER_SPRITE) {
        if (vSpriteTextureIndex >= 0 && vSpriteTextureIndex < MAX_WALL_TEXTURES) {
            vec4 texColor = SampleWallTexture(vSpriteTextureIndex, vSpriteUV);

            if (texColor.a < 0.1) {
                discard;
            }

            FragColor = texColor * vColor;
        }
        else {
            FragColor = vColor;
        }

        return;
    }
    if (renderMode == RENDER_DECAL) {
        if (vTextureIndex >= 0 && vTextureIndex < MAX_WALL_TEXTURES) {
            vec4 texColor = SampleWallTexture(vTextureIndex, vDecalUV);

            if (texColor.a < 0.1) {
                discard;
            }

            FragColor = texColor * vColor;
        }
        else {
            FragColor = vColor;
        }

        return;
    }

    discard;
}