#version 430 core

// The level's sky (SkySettings, Headers/Objects/Sky.hpp), drawn behind everything.
//
// World directions: Front = +Z, Right = -X, Up = +Y. Azimuth is measured like
// camera yaw: atan(x, z), 0 at Front, +90 at Left (+X).

in vec2 vNdc;
out vec4 FragColor;

// Matches SkyMode. -1 = nothing usable to draw, show the fallback color.
const int SKY_NONE = -1;
const int SKY_CUBEMAP = 0;
const int SKY_EQUIRECTANGULAR = 1;
const int SKY_CYLINDER = 2;

uniform int skyMode = SKY_NONE;

// Camera basis and projection, so every pixel gets its exact view ray.
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform float tanHalfFovY;
uniform float aspect;

uniform float rotation;      // radians, sky yaw (rotation + spin)
uniform float horizonOffset; // radians, positive raises the horizon

uniform vec3 tint;           // 0..1
uniform vec3 fallbackColor;  // 0..1

// Equirectangular and Cylinder.
uniform sampler2D skyTexture;
uniform vec2 skyTextureSize; // pixels

// Cubemap, one sampler per SkyFace.
uniform sampler2D faceRight;
uniform sampler2D faceLeft;
uniform sampler2D faceUp;
uniform sampler2D faceDown;
uniform sampler2D faceFront;
uniform sampler2D faceBack;

const float PI = 3.14159265358979323846;
const float TWO_PI = PI * 2.0;
const float HALF_PI = PI * 0.5;

float Azimuth(vec3 direction) {
    return atan(direction.x, direction.z);
}

// The view direction rotated into sky space: spin about Y, then the horizon
// shifted by changing the elevation.
vec3 SkyDirection(vec3 direction) {
    float azimuth = Azimuth(direction) - rotation;
    float elevation = asin(clamp(direction.y, -1.0, 1.0)) - horizonOffset;
    elevation = clamp(elevation, -HALF_PI, HALF_PI);

    return vec3(cos(elevation) * sin(azimuth), sin(elevation), cos(elevation) * cos(azimuth));
}

vec4 SampleEquirectangular(vec3 direction) {
    vec3 sky = SkyDirection(direction);

    // Front in the middle of the image, reading left to right.
    vec2 uv = vec2(0.5 - Azimuth(sky) / TWO_PI, 0.5 - asin(clamp(sky.y, -1.0, 1.0)) / PI);

    // The u seam jumps from 1 to 0; keep the derivatives continuous across it.
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    duvdx.x -= floor(duvdx.x + 0.5);
    duvdy.x -= floor(duvdy.x + 0.5);

    uv.x = fract(uv.x);
    uv.y = clamp(uv.y, 0.0, 1.0);

    return textureGrad(skyTexture, uv, duvdx, duvdy);
}

// Each face is seen from inside the cube, upright, reading left to right.
// Up has Front at its bottom edge, Down has Front at its top edge.
vec4 SampleCubemap(vec3 direction) {
    vec3 d = SkyDirection(direction);
    vec3 a = abs(d);

    if (a.x >= a.y && a.x >= a.z) {
        vec3 p = d / a.x;
        if (d.x < 0.0) // Right (-X): its right-hand side is -Z
            return textureLod(faceRight, vec2(0.5 - 0.5 * p.z, 0.5 - 0.5 * p.y), 0.0);
        // Left (+X): its right-hand side is +Z
        return textureLod(faceLeft, vec2(0.5 + 0.5 * p.z, 0.5 - 0.5 * p.y), 0.0);
    }

    if (a.y >= a.z) {
        vec3 p = d / a.y;
        if (d.y > 0.0) // Up: right-hand side -X, Front at the bottom
            return textureLod(faceUp, vec2(0.5 - 0.5 * p.x, 0.5 + 0.5 * p.z), 0.0);
        // Down: right-hand side -X, Front at the top
        return textureLod(faceDown, vec2(0.5 - 0.5 * p.x, 0.5 - 0.5 * p.z), 0.0);
    }

    vec3 p = d / a.z;
    if (d.z > 0.0) // Front (+Z): right-hand side -X
        return textureLod(faceFront, vec2(0.5 - 0.5 * p.x, 0.5 - 0.5 * p.y), 0.0);
    // Back (-Z): right-hand side +X
    return textureLod(faceBack, vec2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y), 0.0);
}

// DOOM's sky: 1024 texture pixels around 360 degrees (a 256 wide sky repeats
// 4 times), and rows tied to the screen rather than the view angle: 200 rows
// from top to bottom with row 100 on the screen's centre, whatever the FOV.
// It ignores pitch, like DOOM, and repeats vertically.
vec4 SampleCylinder() {
    float tanHalfFovX = tanHalfFovY * aspect;

    vec3 forward = normalize(vec3(cameraForward.x, 0.0, cameraForward.z));
    vec3 right = normalize(vec3(cameraRight.x, 0.0, cameraRight.z));
    vec3 ray = forward + vNdc.x * tanHalfFovX * right;

    float azimuth = Azimuth(ray) - rotation;

    // The offset is an angle at the screen's centre, turned into rows.
    float offsetRows = 100.0 * tan(clamp(horizonOffset, -HALF_PI + 0.01, HALF_PI - 0.01)) / tanHalfFovY;

    vec2 texel = vec2(-degrees(azimuth) * (1024.0 / 360.0), 100.0 - 100.0 * vNdc.y + offsetRows);
    vec2 uv = texel / max(skyTextureSize, vec2(1.0));

    // Derivatives before wrapping, so the repeat seams don't pick a tiny mip.
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    duvdx.x -= floor(duvdx.x + 0.5);
    duvdy.x -= floor(duvdy.x + 0.5);

    return textureGrad(skyTexture, fract(uv), duvdx, duvdy);
}

void main() {
    if (skyMode == SKY_NONE) {
        FragColor = vec4(fallbackColor, 1.0);
        return;
    }

    float tanHalfFovX = tanHalfFovY * aspect;
    vec3 direction = normalize(
        cameraForward +
        vNdc.x * tanHalfFovX * cameraRight +
        vNdc.y * tanHalfFovY * cameraUp
    );

    vec4 color;
    if (skyMode == SKY_CUBEMAP) color = SampleCubemap(direction);
    else if (skyMode == SKY_CYLINDER) color = SampleCylinder();
    else color = SampleEquirectangular(direction);

    // Transparent parts of the image show the fallback color.
    FragColor = vec4(mix(fallbackColor, color.rgb * tint, color.a), 1.0);
}
