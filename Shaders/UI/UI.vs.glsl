#version 430 core

layout(location = 0) in vec2 aLocalPos;
layout(location = 1) in vec2 aUV;

uniform vec2 uScreenSize;
uniform vec2 uPosition;
uniform vec2 uSize;

out vec2 vUV;

void main() {
    vec2 local01 = aLocalPos + vec2(0.5);

    vec2 pixelPos = uPosition + local01 * uSize;

    vec2 ndc;
    ndc.x = (pixelPos.x / uScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (pixelPos.y / uScreenSize.y) * 2.0;

    gl_Position = vec4(ndc, 0.0, 1.0);

    vUV = aUV;
}