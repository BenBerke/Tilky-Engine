#version 430 core

#define SCREEN_HEIGHT 960.0

#define RENDER_WALL 0
#define RENDER_FLAT 1

flat in vec4 vWallColor;

flat in float fScreenXStart;
flat in float fScreenXEnd;

flat in float fTopYStart;
flat in float fTopYEnd;
flat in float fBottomYStart;
flat in float fBottomYEnd;

flat in float fSStart;
flat in float fSEnd;
flat in float fZLeft;
flat in float fZRight;

noperspective in float vFlatInvZ;

uniform int renderMode;

out vec4 FragColor;

const float nearPlane = 0.01;
const float farPlane = 1000.0;
const float tileSize = 32.0;

void main() {
    if (renderMode == RENDER_FLAT) {
        float flatViewDepth = 1.0 / vFlatInvZ;

        float flatDepth01 = clamp(
        (flatViewDepth - nearPlane) / (farPlane - nearPlane),
        0.0,
        1.0
        );

        gl_FragDepth = flatDepth01;
        FragColor = vWallColor;
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

    gl_FragDepth = wallDepth01;

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

    float checker = mod(floor(u) + floor(v * 8.0), 2.0);

    vec3 dark = vec3(0.15);
    vec3 light = vec3(0.95);
    vec3 checkerColor = mix(dark, light, checker);

    FragColor = vec4(checkerColor * vWallColor.rgb, vWallColor.a);
}