#version 430 core

#define SCREEN_HEIGHT 960.0

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

out vec4 FragColor;

const float nearPlane = 0.01;
const float farPlane = 1000.0;
const float tileSize = 32.0;

void main() {
    float x = gl_FragCoord.x;

    float across = (x - fScreenXStart) / (fScreenXEnd - fScreenXStart);
    across = clamp(across, 0.0, 1.0);

    float invZ = mix(1.0 / fZLeft, 1.0 / fZRight, across);
    float viewDepth = 1.0 / invZ;

    float depth01 = clamp((viewDepth - nearPlane) / (farPlane - nearPlane), 0.0, 1.0);
    gl_FragDepth = depth01;

    float s = mix(fSStart / fZLeft, fSEnd / fZRight, across) / invZ;

    float topY    = mix(fTopYStart,    fTopYEnd,    across);
    float bottomY = mix(fBottomYStart, fBottomYEnd, across);

    float fragYTopOrigin = SCREEN_HEIGHT - gl_FragCoord.y;
    float v = (fragYTopOrigin - topY) / (bottomY - topY);
    v = clamp(v, 0.0, 1.0);

    float u = s / tileSize;

    float checker = mod(floor(u) + floor(v * 8.0), 2.0);

    vec3 dark = vec3(0.15);
    vec3 light = vec3(0.95);
    vec3 checkerColor = mix(dark, light, checker);

    FragColor = vec4(checkerColor * vWallColor.rgb, vWallColor.a);
}