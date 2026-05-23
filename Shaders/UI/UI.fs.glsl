#version 430 core

in vec2 vUV;

uniform vec4 uColor;
uniform int uUseTexture;

uniform sampler2D uAtlas;
uniform int uTextureIndex;
uniform int uTextureCount;

struct TextureRegion {
    vec4 uvRect;
    vec4 data;
};

layout(std430, binding = 5) readonly buffer TextureRegionBuffer {
    TextureRegion textureRegions[];
};

out vec4 FragColor;

vec4 SampleAtlas(int textureIndex, vec2 uv) {
    if (textureIndex < 0 || textureIndex >= uTextureCount) {
        return vec4(1.0, 0.0, 1.0, 1.0); // magenta = invalid texture
    }

    TextureRegion region = textureRegions[textureIndex];

    if (region.data.x < 0.5) {
        return vec4(1.0, 0.0, 1.0, 1.0); // magenta = unloaded texture
    }

    vec2 localUV = clamp(uv, vec2(0.0), vec2(1.0));

    vec2 atlasUV = mix(
    region.uvRect.xy,
    region.uvRect.zw,
    localUV
    );

    return texture(uAtlas, atlasUV);
}

void main() {
    vec4 baseColor = vec4(1.0);

    if (uUseTexture == 1) {
        baseColor = SampleAtlas(uTextureIndex, vUV);

        if (baseColor.a < 0.1) {
            discard;
        }
    }

    FragColor = baseColor * uColor;
}