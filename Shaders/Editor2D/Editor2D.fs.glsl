#version 430 core
// See Editor2D.vs.glsl. Compiled with the same PASS_* define.

in vec4 vColor;
in vec2 vUV;
in vec2 vScreen;
flat in vec4 vRect;
flat in uint vFlags;
flat in vec4 vData;

uniform vec2 uScreen;
uniform int uTextured;
uniform sampler2D uTexture;

out vec4 FragColor;

// This pixel's centre in screen pixels (y down).
vec2 PixelCenter() {
    return vec2(gl_FragCoord.x, uScreen.y - gl_FragCoord.y);
}

#ifdef PASS_ENTITY
uniform vec4 uNormalEntity;
uniform vec4 uSpriteEntity;
uniform vec4 uDisabledEntity;
uniform vec4 uHighlightedEntity;
uniform vec4 uHoverColor;
uniform float uEntityAlpha;
uniform float uEntityFaintAlpha;

bool Inside(vec2 p, vec4 rect) {
    return p.x >= rect.x && p.x < rect.z && p.y >= rect.y && p.y < rect.w;
}

void main() {
    vec2 p = PixelCenter();
    float alpha = (vFlags & 1u) != 0u ? uEntityFaintAlpha : uEntityAlpha;
    bool hasSprite = vData.x > 0.5;
    bool enabled = vData.y > 0.5;

    if (Inside(p, vRect)) {
        // The 1 pixel black frame just inside the square.
        if (!Inside(p, vec4(vRect.xy + 1.0, vRect.zw - 1.0))) {
            FragColor = vec4(0.0, 0.0, 0.0, alpha);
            return;
        }

        if (uTextured != 0) {
            vec4 color = texture(uTexture, vUV);
            if (!enabled) color.rgb *= uDisabledEntity.rgb;
            FragColor = vec4(color.rgb, color.a * alpha);
            return;
        }

        vec4 color = uNormalEntity;
        if (hasSprite) color = uSpriteEntity;
        if (!enabled) color = uDisabledEntity;
        if ((vFlags & 2u) != 0u) color = uHighlightedEntity;
        FragColor = vec4(color.rgb, alpha);
        return;
    }

    // Hover: an amber frame 3 pixels wide around the square.
    if ((vFlags & 4u) != 0u && Inside(p, vec4(vRect.xy - 3.0, vRect.zw + 3.0))) {
        FragColor = uHoverColor;
        return;
    }

    discard;
}
#elif defined(PASS_COLLIDER)
uniform vec4 uColliderColor;
uniform vec4 uTriggerColor;
uniform float uColliderThickness;
uniform float uEntityAlpha;
uniform float uEntityFaintAlpha;

void main() {
    vec2 p = PixelCenter();
    vec2 center = vRect.xy;
    vec2 halfSize = vRect.zw;

    float edgeDistance;
    if (vData.x > 0.5) edgeDistance = abs(length(p - center) - halfSize.x);
    else {
        vec2 q = abs(p - center) - halfSize;
        edgeDistance = abs(length(max(q, 0.0)) + min(max(q.x, q.y), 0.0));
    }

    // A dark edge under the coloured line, both stepped like DrawThickLine.
    float colorWidth = 2.0 * floor(uColliderThickness * 0.5) + 1.0;
    float darkWidth = 2.0 * floor((uColliderThickness + 2.0) * 0.5) + 1.0;

    float alpha255 = floor(((vFlags & 1u) != 0u ? uEntityFaintAlpha : uEntityAlpha) * 255.0 + 0.5);
    if (vData.z < 0.5) alpha255 = floor(alpha255 * 0.5);
    float alpha = alpha255 / 255.0;

    if (edgeDistance <= colorWidth * 0.5) {
        // The colour lies over the dark edge, both at `alpha`: colour * a +
        // black * a(1 - a) + background * (1 - a)^2, as one fragment.
        vec4 color = vData.y > 0.5 ? uTriggerColor : uColliderColor;
        float combined = alpha * (2.0 - alpha);
        FragColor = vec4(combined > 0.0 ? color.rgb * alpha / combined : color.rgb, combined);
    }
    else if (edgeDistance <= darkWidth * 0.5) FragColor = vec4(0.0, 0.0, 0.0, alpha);
    else discard;
}
#else
void main() {
    if (uTextured != 0) FragColor = texture(uTexture, vUV) * vColor;
    else FragColor = vColor;
}
#endif
