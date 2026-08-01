#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_COLLIDER 4

in vec2 vSpriteUV;
flat in int vSpriteTextureIndex;

in vec2 vWallUV;
in vec2 vFlatUV;

flat in int vTextureIndex;
flat in int vFlatTextureIndex;
flat in vec4 vColor;

in vec3 vWorldPos;
in vec2 vSurfaceCoord;
flat in vec2 vSurfaceSize;

uniform sampler2D uAtlas;
uniform int uTextureCount;

uniform int renderMode;
uniform vec3 uCameraWorldPos;

// Shader-only lighting controls. Values are in world units.
const float DISTANCE_LIGHT_START = 256.0;
const float DISTANCE_LIGHT_END = 1536.0;
const float DISTANCE_MIN_LIGHT = 0.18;

// Approximate contact occlusion along wall boundaries.
const float WALL_AO_DISTANCE = 12.0;
const float WALL_AO_STRENGTH = 0.28;

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

    vec2 localUV = repeatUV ? fract(uv) : clamp(uv, vec2(0.0), vec2(1.0));

    vec2 atlasUV = regionMin + localUV * regionSize;

    if (repeatUV) {
        vec2 dx = dFdx(uv) * regionSize;
        vec2 dy = dFdy(uv) * regionSize;

        return textureGrad(uAtlas, atlasUV, dx, dy);
    }

    return texture(uAtlas, atlasUV);
}

float GetDistanceLight() {
    float cameraDistance = length(vWorldPos - uCameraWorldPos);
    float fade = smoothstep(DISTANCE_LIGHT_START, DISTANCE_LIGHT_END, cameraDistance);

    return mix(1.0, DISTANCE_MIN_LIGHT, fade);
}

float GetWallAmbientOcclusion() {
    vec2 edgeDistance = min(vSurfaceCoord, vSurfaceSize - vSurfaceCoord);
    float nearestEdge = min(edgeDistance.x, edgeDistance.y);
    float visibility = smoothstep(0.0, WALL_AO_DISTANCE, nearestEdge);

    return mix(1.0 - WALL_AO_STRENGTH, 1.0, visibility);
}

vec4 ApplyLighting(vec4 color, bool applyWallAO) {
    float light = GetDistanceLight();

    if (applyWallAO) {
        light *= GetWallAmbientOcclusion();
    }

    return vec4(color.rgb * light, color.a);
}

void main() {
    if (renderMode == RENDER_FLAT) {
        vec4 texColor = SampleTexture(vFlatTextureIndex, vFlatUV, true);

        if (texColor.a < 0.1) discard;

        FragColor = ApplyLighting(texColor * vColor, false);
        return;
    }
    else if (renderMode == RENDER_WALL) {
        vec4 texColor = SampleTexture(vTextureIndex, vWallUV, true);

        if (texColor.a < 0.1) discard;

        FragColor = ApplyLighting(texColor * vColor, true);
        return;
    }
    else if (renderMode == RENDER_SPRITE) {
        vec4 texColor = SampleTexture(vSpriteTextureIndex, vSpriteUV, false);

        if (texColor.a < 0.1) discard;

        FragColor = ApplyLighting(texColor * vColor, false);
        return;
    }
    else if (renderMode == RENDER_COLLIDER){
        FragColor = vColor;

        return;
    }

    discard;
}