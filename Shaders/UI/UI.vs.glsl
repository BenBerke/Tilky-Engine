#version 430 core

layout(location = 0) in vec2 aLocalPos;
layout(location = 1) in vec2 aUV;

uniform vec2 uScreenSize;
uniform vec2 uPosition;
uniform vec2 uSize;

uniform float rotation;

out vec2 vUV;

void main() {
    float s = sin(rotation);
    float c = cos(rotation);

    vec2 rotatedLocalPos;
    rotatedLocalPos.x = aLocalPos.x * c - aLocalPos.y * s;
    rotatedLocalPos.y = aLocalPos.x * s + aLocalPos.y * c;

    vec2 local01 = rotatedLocalPos + vec2(0.5);

    vec2 pixelPos = uPosition + local01 * uSize;

    vec2 ndc;
    ndc.x = (pixelPos.x / uScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (pixelPos.y / uScreenSize.y) * 2.0;

    gl_Position = vec4(ndc, 0.0, 1.0);

    vUV = vec2(aUV.x, 1.0 - aUV.y);
}