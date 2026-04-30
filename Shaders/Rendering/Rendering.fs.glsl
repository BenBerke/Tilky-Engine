#version 430 core

#define SCREEN_HEIGHT 960.0

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3

#define MAX_WALL_TEXTURES 8

flat in int vTextureIndex;
flat in float fWallWorldHeight;

flat in float fWallBottomHeight;
flat in float fWallTopHeight;

uniform sampler2D wallTextures[MAX_WALL_TEXTURES];

flat in vec4 vWallColor;

flat in float fScreenXStart;
flat in float fScreenXEnd;

flat in float fWallTextureAnchorHeight;

flat in float fTopYStart;
flat in float fTopYEnd;
flat in float fBottomYStart;
flat in float fBottomYEnd;

flat in float fSStart;
flat in float fSEnd;
flat in float fZLeft;
flat in float fZRight;

noperspective in float vFlatInvZ;

noperspective in vec2 vFlatWorldOverZ;
flat in int vFlatTextureIndex;

noperspective in vec2 vSpriteUV;
flat in int vSpriteTextureIndex;
flat in float fSpriteViewDepth;
flat in vec4 vSpriteColor;

uniform int renderMode;

out vec4 FragColor;

const float nearPlane = 0.1;
const float farPlane = 10000.0;
const float tileSize = 32.0;

vec4 SampleWallTexture(int textureIndex, vec2 uv) {
    switch (textureIndex) {
        case 0:
        return texture(wallTextures[0], uv);
        case 1:
        return texture(wallTextures[1], uv);
        case 2:
        return texture(wallTextures[2], uv);
        case 3:
        return texture(wallTextures[3], uv);
        case 4:
        return texture(wallTextures[4], uv);
        case 5:
        return texture(wallTextures[5], uv);
        case 6:
        return texture(wallTextures[6], uv);
        case 7:
        return texture(wallTextures[7], uv);
        default:
        return vec4(1.0);
    }
}

void main() {
    if (renderMode == RENDER_SPRITE) {
        float spriteDepth01 = clamp(
        (fSpriteViewDepth - nearPlane) / (farPlane - nearPlane),
        0.0,
        1.0
        );

        gl_FragDepth = spriteDepth01;

        vec4 texColor = SampleWallTexture(vSpriteTextureIndex, vSpriteUV);

        if (texColor.a < 0.1) {
            discard;
        }

        FragColor = texColor * vSpriteColor;
        return;
    }
    else if (renderMode == RENDER_FLAT) {
        float flatViewDepth = 1.0 / vFlatInvZ;

        float flatDepth01 = (flatViewDepth - nearPlane) / (farPlane - nearPlane);
        flatDepth01 = clamp(flatDepth01, 0.0, 0.99999);

        gl_FragDepth = flatDepth01;;

        vec2 worldPos = vFlatWorldOverZ / vFlatInvZ;
        vec2 flatUV = worldPos / tileSize;

        if (vFlatTextureIndex >= 0 && vFlatTextureIndex < MAX_WALL_TEXTURES) {
            vec4 texColor = SampleWallTexture(vFlatTextureIndex, flatUV);

            if (texColor.a < 0.1) {
                discard;
            }

            FragColor = texColor * vWallColor;
        }
        else {
            FragColor = vWallColor;
        }

        return;
    }
    else if (renderMode == RENDER_DECAL) {
        float x = gl_FragCoord.x;

        float denominator = fScreenXEnd - fScreenXStart;

        if (abs(denominator) < 0.00001) {
            discard;
        }

        float across = (x - fScreenXStart) / denominator;
        across = clamp(across, 0.0, 1.0);

        float invZ = mix(1.0 / fZLeft, 1.0 / fZRight, across);
        float decalViewDepth = 1.0 / invZ;

        float decalDepth01 = clamp(
        (decalViewDepth - nearPlane) / (farPlane - nearPlane),
        0.0,
        1.0
        );

        // Pull the decal slightly forward to avoid z-fighting with the wall.
        gl_FragDepth = max(decalDepth01 - 0.00002, 0.0);

        float topY = mix(fTopYStart, fTopYEnd, across);
        float bottomY = mix(fBottomYStart, fBottomYEnd, across);

        float heightDenominator = bottomY - topY;

        if (abs(heightDenominator) < 0.00001) {
            discard;
        }

        float fragYTopOrigin = SCREEN_HEIGHT - gl_FragCoord.y;
        float v = (fragYTopOrigin - topY) / heightDenominator;
        v = clamp(v, 0.0, 1.0);

        float s = mix(
        fSStart / fZLeft,
        fSEnd / fZRight,
        across
        ) / invZ;

        float decalWidth = max(abs(fSEnd - fSStart), 0.0001);
        float u = s / decalWidth;

        vec2 uv = vec2(u, v);

        vec4 texColor = SampleWallTexture(vTextureIndex, uv);

        if (texColor.a < 0.1) {
            discard;
        }

        FragColor = texColor * vWallColor;
        return;
    }

    float x = gl_FragCoord.x;

    float denominator = fScreenXEnd - fScreenXStart;

    if (abs(denominator) < 0.00001) {
        discard;
    }

    float across = (x - fScreenXStart) / denominator;
    across = clamp(across, 0.0, 1.0);

    float invZ = mix(1.0 / fZLeft, 1.0 / fZRight, across);
    float wallViewDepth = 1.0 / invZ;

    float wallDepth01 = clamp(
    (wallViewDepth - nearPlane) / (farPlane - nearPlane),
    0.0,
    1.0
    );

    gl_FragDepth = max(wallDepth01 - 0.00001, 0.0);

    float s = mix(fSStart / fZLeft, fSEnd / fZRight, across) / invZ;

    float topY = mix(fTopYStart, fTopYEnd, across);
    float bottomY = mix(fBottomYStart, fBottomYEnd, across);

    float heightDenominator = bottomY - topY;

    if (abs(heightDenominator) < 0.00001) {
        discard;
    }

    float fragYTopOrigin = SCREEN_HEIGHT - gl_FragCoord.y;
    float v = (fragYTopOrigin - topY) / heightDenominator;
    v = clamp(v, 0.0, 1.0);

    float u = s / tileSize;

    // World-space vertical texture coordinate.
    // This stops the texture from resetting when the wall moves.
    float worldHeightAtFragment = mix(
    fWallTopHeight,
    fWallBottomHeight,
    v
    );

    float texV = -worldHeightAtFragment / tileSize;

    if (vTextureIndex >= 0 && vTextureIndex < MAX_WALL_TEXTURES) {
        vec4 texColor = SampleWallTexture(vTextureIndex, vec2(u, texV));

        if (texColor.a < 0.1) {
            discard;
        }

        FragColor = texColor * vWallColor;
    }
    else {
        FragColor = vWallColor;
    }
}