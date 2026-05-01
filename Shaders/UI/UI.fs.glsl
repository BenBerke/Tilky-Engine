#version 430 core

in vec2 vUV;

uniform vec4 uColor;
uniform sampler2D uTexture;
uniform int uUseTexture;

out vec4 FragColor;

void main() {
    vec4 baseColor = vec4(1.0);

    if (uUseTexture == 1) {
        baseColor = texture(uTexture, vUV);

        if (baseColor.a < 0.1) {
            discard;
        }
    }

    FragColor = baseColor * uColor;
}