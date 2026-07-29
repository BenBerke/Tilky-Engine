#version 430 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D backgroundTexture;

// Camera rotation in degrees.
uniform float playerAngle;
uniform float playerPitch = 0.0;

uniform float horizontalFov = 90.0;

// 1.0 keeps the panorama fixed in the world. Lower values produce a
// deliberately slower, stylized parallax response.
uniform float parallaxStrength = 1.0;

// Additional texture-space scrolling, useful for animated backgrounds.
uniform vec2 backgroundScroll = vec2(0.0);

const float PI = 3.14159265358979323846;
const float TWO_PI = PI * 2.0;

void main() {
    vec2 ndc = vec2(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0);

    // Recover the viewport aspect ratio from the interpolated full-screen UVs.
    float uvStepX = max(abs(dFdx(vUV.x)), 0.000001);
    float uvStepY = max(abs(dFdy(vUV.y)), 0.000001);
    float aspect = uvStepY / uvStepX;

    float halfHorizontalFov = radians(clamp(horizontalFov, 1.0, 179.0)) * 0.5;
    float tanHalfHorizontalFov = tan(halfHorizontalFov);
    float tanHalfVerticalFov = tanHalfHorizontalFov / aspect;

    // Perspective-correct view ray for this fragment.
    vec3 direction = normalize(vec3(
    ndc.x * tanHalfHorizontalFov,
    ndc.y * tanHalfVerticalFov,
    1.0
    ));

    float pitch = radians(playerPitch * parallaxStrength);
    float pitchSin = sin(pitch);
    float pitchCos = cos(pitch);

    direction = vec3(
    direction.x,
    direction.y * pitchCos + direction.z * pitchSin,
    -direction.y * pitchSin + direction.z * pitchCos
    );

    float yaw = radians(playerAngle * parallaxStrength);
    float yawSin = sin(yaw);
    float yawCos = cos(yaw);

    direction = vec3(
    direction.x * yawCos + direction.z * yawSin,
    direction.y,
    -direction.x * yawSin + direction.z * yawCos
    );

    // Convert the rotated ray to equirectangular panorama coordinates.
    vec2 uv;
    uv.x = atan(direction.x, direction.z) / TWO_PI + 0.5;
    uv.y = 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI;

    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    duvdx.x -= floor(duvdx.x + 0.5);
    duvdy.x -= floor(duvdy.x + 0.5);

    uv += backgroundScroll;
    uv.x = fract(uv.x);
    uv.y = clamp(uv.y, 0.0, 1.0);
    FragColor = textureGrad(backgroundTexture, uv, duvdx, duvdy);
}
