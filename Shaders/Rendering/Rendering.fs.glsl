#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3
#define RENDER_COLLIDER 4

in vec2 vDecalUV;

in vec2 vSpriteUV;
flat in int vSpriteTextureIndex;

in vec2 vWallUV;
in vec2 vFlatUV;

flat in int vTextureIndex;
flat in int vFlatTextureIndex;
flat in vec4 vColor;

uniform sampler2D uAtlas;
uniform int uTextureCount;

uniform int renderMode;

struct TextureRegion {
    vec4 uvRect;
    vec4 data;
};

layout(std430, binding = 5) readonly buffer TextureRegionBuffer {
    TextureRegion textureRegions[];
};

out vec4 FragColor;

vec4 SampleTexture(int textureIndex, vec2 uv, bool repeatUV) {
    if (textureIndex < 0 || textureIndex >= uTextureCount) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    TextureRegion region = textureRegions[textureIndex];

    if (region.data.x < 0.5) {
        return vec4(1.0);
    }

    vec2 regionMin = region.uvRect.xy;
    vec2 regionMax = region.uvRect.zw;
    vec2 regionSize = regionMax - regionMin;

    vec2 localUV = repeatUV
    ? fract(uv)
    : clamp(uv, vec2(0.0), vec2(1.0));

    vec2 atlasUV = regionMin + localUV * regionSize;

    if (repeatUV) {
        vec2 dx = dFdx(uv) * regionSize;
        vec2 dy = dFdy(uv) * regionSize;

        return textureGrad(uAtlas, atlasUV, dx, dy);
    }

    return texture(uAtlas, atlasUV);
}

void main() {
    if (renderMode == RENDER_FLAT) {
        vec4 texColor = SampleTexture(vFlatTextureIndex, vFlatUV, true);

        if (texColor.a < 0.1) discard;

        FragColor = texColor * vColor;
        return;
    }
    else if (renderMode == RENDER_WALL) {
        vec4 texColor = SampleTexture(vTextureIndex, vWallUV, true);

        if (texColor.a < 0.1) discard;

        FragColor = texColor * vColor;
        return;
    }
    else if (renderMode == RENDER_SPRITE) {
        vec4 texColor = SampleTexture(vSpriteTextureIndex, vSpriteUV, false);

        if (texColor.a < 0.1) discard;

        FragColor = texColor * vColor;
        return;
    }
    else if (renderMode == RENDER_DECAL) {
        vec4 texColor = SampleTexture(vTextureIndex, vDecalUV, false);

        if (texColor.a < 0.1) discard;

        FragColor = texColor * vColor;
        return;
    }
    else if (renderMode == RENDER_COLLIDER){
        FragColor = vColor;

        return;
    }

    discard;
}