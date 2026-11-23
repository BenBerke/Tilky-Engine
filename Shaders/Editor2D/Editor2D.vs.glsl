#version 430 core
// The 2D editors' canvas (OpenGLEditor2D.cpp). Compiled once per pass with
// one of PASS_SECTOR, PASS_WALL, PASS_LINEDEF, PASS_ENTITY, PASS_COLLIDER or
// PASS_OVERLAY defined after the #version line. Layouts mirror
// Headers/Runtime/Renderer/Editor2DTypes.hpp. Screen space is pixels, y down.

uniform vec2 uCamera;  // world point at the screen's centre
uniform float uZoom;   // pixels per world unit
uniform vec2 uScreen;  // pixels

out vec4 vColor;
out vec2 vUV;
out vec2 vScreen;      // this vertex in screen pixels
flat out vec4 vRect;   // entity rect / collider centre + size, screen pixels
flat out uint vFlags;
flat out vec4 vData;

vec2 WorldToScreen(vec2 world) {
    return (world - uCamera) * uZoom + uScreen * 0.5;
}

vec4 ScreenToClip(vec2 screen) {
    // GL's fill rule counts an edge through a pixel centre from the bottom
    // up; SDL's from the top down. A hair up keeps half-pixel edges (grid
    // dots, rects at x.5) on the same rows SDL filled.
    screen.y -= 1.0 / 256.0;
    return vec4(screen.x / uScreen.x * 2.0 - 1.0, 1.0 - screen.y / uScreen.y * 2.0, 0.0, 1.0);
}

// Two triangles over the unit square.
const vec2 CORNERS[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void Discard() {
    // Every corner at the same point: nothing is drawn.
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vColor = vec4(0.0);
}

// A quad `thickness` pixels wide along a -> b (screen), lengthened by
// `extend` pixels at both ends.
vec2 LineCorner(vec2 a, vec2 b, float thickness, float extend, vec2 corner) {
    vec2 delta = b - a;
    float len = length(delta);
    vec2 dir = len > 0.0001 ? delta / len : vec2(1.0, 0.0);
    vec2 normal = vec2(-dir.y, dir.x);

    a -= dir * extend;
    b += dir * extend;

    return mix(a, b, corner.x) + normal * (corner.y - 0.5) * thickness;
}

// SDL's DrawThickLine steps whole pixels either side of the centre line.
float SteppedThickness(float thickness) {
    return 2.0 * floor(thickness * 0.5) + 1.0;
}

#ifdef PASS_SECTOR
struct Sector {
    vec4 light;   // rgb 0..255, w = hue
    vec4 heights; // x floor, y ceiling, z has floor
};
struct SectorTriangle {
    vec4 ab;
    vec4 cSector; // xy = c, z = sector index
};
layout(std430, binding = 10) readonly buffer SectorBuffer { Sector sectors[]; };
layout(std430, binding = 11) readonly buffer SectorTriangleBuffer { SectorTriangle sectorTriangles[]; };
layout(std430, binding = 12) readonly buffer SectorFlagBuffer { uint sectorFlags[]; };

uniform int uTextured;
uniform int uViewMode;
uniform float uRangeLow;
uniform float uRangeHigh;
uniform float uPlayerHeight;
uniform int uSectorLayerOn;
uniform vec3 uHighlightedSector; // 0..1
uniform vec3 uHoveredSector;

const float VIEW_MODE_FILL_ALPHA = 0.75;

vec3 HSVtoRGB(float h, float s, float v) {
    int region = int(floor(h * 6.0));
    float fraction = h * 6.0 - float(region);
    float p = v * (1.0 - s);
    float q = v * (1.0 - fraction * s);
    float t = v * (1.0 - (1.0 - fraction) * s);

    switch (region % 6) {
        case 0: return vec3(v, t, p);
        case 1: return vec3(q, v, p);
        case 2: return vec3(p, v, t);
        case 3: return vec3(p, q, v);
        case 4: return vec3(t, p, v);
        case 5: return vec3(v, p, q);
    }
    return vec3(0.0);
}

// Blue at 0 through cyan, green and yellow to red at 1.
vec3 HeatColor(float t) {
    const vec3 STOPS[5] = vec3[](vec3(0.15, 0.30, 1.00), vec3(0.10, 0.85, 0.95), vec3(0.20, 0.85, 0.25),
                                 vec3(0.95, 0.90, 0.15), vec3(1.00, 0.35, 0.10));
    float scaled = clamp(t, 0.0, 1.0) * 4.0;
    int index = min(int(scaled), 3);
    float fraction = scaled - float(index);
    return mix(STOPS[index], STOPS[index + 1], fraction);
}

bool ViewModeColor(Sector sector, out vec4 color) {
    if (uViewMode == 3) { // Brightness
        color = vec4(clamp(sector.light.rgb / 255.0, 0.0, 1.0), VIEW_MODE_FILL_ALPHA);
        return true;
    }

    float value;
    if (uViewMode == 4) value = sector.heights.x;
    else if (uViewMode == 5) value = sector.heights.y;
    else if (uViewMode == 6) value = sector.heights.y - sector.heights.x;
    else return false;

    if (uViewMode == 6 && uPlayerHeight > 0.0 && value < uPlayerHeight) {
        color = vec4(0.85, 0.05, 0.05, VIEW_MODE_FILL_ALPHA);
        return true;
    }

    float span = uRangeHigh - uRangeLow;
    float t = span > 0.0 ? (value - uRangeLow) / span : 0.5;

    // Room heights run green to blue, so red is left for rooms that are too low.
    color = vec4(HeatColor(uViewMode == 6 ? 0.5 - t * 0.5 : t), VIEW_MODE_FILL_ALPHA);
    return true;
}

void main() {
    SectorTriangle triangle = sectorTriangles[gl_VertexID / 3];
    int corner = gl_VertexID % 3;
    vec2 world = corner == 0 ? triangle.ab.xy : corner == 1 ? triangle.ab.zw : triangle.cSector.xy;

    int sectorIndex = int(triangle.cSector.z);
    Sector sector = sectors[sectorIndex];
    uint flags = sectorFlags[sectorIndex];

    bool selected = (flags & 1u) != 0u;
    bool hovered = (flags & 2u) != 0u;
    bool layerDimmed = uSectorLayerOn == 0 && !selected;

    if (uTextured != 0) {
        float tint = layerDimmed ? 0.35 : hovered ? 0.5 : selected ? 0.65 : 1.0;
        vColor = vec4(tint, tint, tint, 1.0);
    }
    else {
        vec4 color = vec4(HSVtoRGB(sector.light.w, 0.7, 0.9), 0.55);

        if (selected) color = vec4(uHighlightedSector, 0.75);
        else if (hovered) color = vec4(uHoveredSector, 0.65);

        vec4 viewColor;
        if (!selected && !hovered && sector.heights.z > 0.5 && ViewModeColor(sector, viewColor)) color = viewColor;

        if (layerDimmed) color.a *= 0.35;
        vColor = color;
    }

    vec2 screen = WorldToScreen(world);
    vScreen = screen;
    vUV = world / 64.0;
    gl_Position = ScreenToClip(screen);
}
#endif

#ifdef PASS_WALL
struct Line {
    vec4 startEnd;
    vec4 data; // x = portal
};
layout(std430, binding = 10) readonly buffer LineBuffer { Line lines[]; };
layout(std430, binding = 11) readonly buffer LineFlagBuffer { uint lineFlags[]; };

uniform int uHighlight; // 0 = base pass, 1 = selection/hover pass
uniform vec4 uNormalWall;
uniform vec4 uPortalWall;
uniform vec4 uHighlightedWall;
uniform vec4 uHoverColor;
uniform float uWallAlpha;
uniform float uWallFaintAlpha;

void main() {
    int index = gl_VertexID / 6;
    vec2 corner = CORNERS[gl_VertexID % 6];
    Line line = lines[index];
    uint flags = lineFlags[index];

    vec2 a = WorldToScreen(line.startEnd.xy);
    vec2 b = WorldToScreen(line.startEnd.zw);

    float thickness;
    float extend = 0.0;
    vec4 color;

    if (uHighlight == 0) {
        if ((flags & 2u) != 0u) { Discard(); return; }

        bool portal = line.data.x > 0.5;
        thickness = portal ? 2.0 : 5.0;
        color = portal ? uPortalWall : uNormalWall;
        color.a = (flags & 1u) != 0u ? uWallFaintAlpha : uWallAlpha;
    }
    else {
        if ((flags & 4u) != 0u) color = uHighlightedWall;
        else if ((flags & 8u) != 0u) color = uHoverColor;
        else { Discard(); return; }

        thickness = 7.0;
        extend = 0.5;
    }

    if (distance(a, b) <= 0.0001) { Discard(); return; }

    vec2 screen = LineCorner(a, b, thickness, extend, corner);
    vColor = color;
    vScreen = screen;
    gl_Position = ScreenToClip(screen);
}
#endif

#ifdef PASS_LINEDEF
struct Line {
    vec4 startEnd;
    vec4 data;
};
layout(std430, binding = 10) readonly buffer LineBuffer { Line lines[]; };
layout(std430, binding = 11) readonly buffer LineFlagBuffer { uint lineFlags[]; };

uniform int uHighlight;
uniform vec4 uLinedefColor;
uniform vec4 uEndpointColor;
uniform float uLinedefAlpha;
uniform float uLinedefThickness;
uniform float uEndpointSize;
uniform vec4 uHighlightedWall;
uniform vec4 uHoverColor;

void main() {
    // Four quads per linedef: the line, the front tick, the two end squares.
    int index = gl_VertexID / 24;
    int part = (gl_VertexID % 24) / 6;
    vec2 corner = CORNERS[gl_VertexID % 6];
    Line line = lines[index];
    uint flags = lineFlags[index];

    vec2 start = line.startEnd.xy;
    vec2 end = line.startEnd.zw;
    vec2 a = WorldToScreen(start);
    vec2 b = WorldToScreen(end);

    vec2 screen;

    if (uHighlight != 0) {
        if (part != 0) { Discard(); return; }

        if ((flags & 4u) != 0u) vColor = uHighlightedWall;
        else if ((flags & 8u) != 0u) vColor = uHoverColor;
        else { Discard(); return; }

        if (distance(a, b) <= 0.0001) { Discard(); return; }
        screen = LineCorner(a, b, 7.0, 0.5, corner);
    }
    else if (part == 0) {
        if (distance(a, b) <= 0.0001) { Discard(); return; }
        vColor = vec4(uLinedefColor.rgb, uLinedefAlpha);
        screen = LineCorner(a, b, uLinedefThickness, 0.0, corner);
    }
    else if (part == 1) {
        // A tick from the middle on the front side, where an entity comes
        // from to cross it Forward.
        vec2 lineVector = end - start;
        float len = length(lineVector);
        if (len <= 0.0001) { Discard(); return; }

        vec2 front = vec2(-lineVector.y, lineVector.x) / len;
        vec2 middle = (start + end) * 0.5;
        vec2 tickEnd = middle + front * (6.0 / max(uZoom, 0.0001));

        vec2 tickA = WorldToScreen(middle);
        vec2 tickB = WorldToScreen(tickEnd);
        if (distance(tickA, tickB) <= 0.0001) { Discard(); return; }

        vColor = vec4(uLinedefColor.rgb, uLinedefAlpha);
        screen = LineCorner(tickA, tickB, uLinedefThickness, 0.0, corner);
    }
    else {
        vec2 point = part == 2 ? a : b;
        vec2 topLeft = vec2(floor(point.x - uEndpointSize * 0.5 + 0.5), floor(point.y - uEndpointSize * 0.5 + 0.5));
        vColor = vec4(uEndpointColor.rgb, uLinedefAlpha);
        screen = topLeft + corner * uEndpointSize;
    }

    vScreen = screen;
    gl_Position = ScreenToClip(screen);
}
#endif

#ifdef PASS_ENTITY
struct Entity {
    vec4 positionHalf;
    vec4 data; // x = has sprite, y = enabled
};
layout(std430, binding = 10) readonly buffer EntityBuffer { Entity entities[]; };
layout(std430, binding = 11) readonly buffer EntityFlagBuffer { uint entityFlags[]; };

// Room around the square for the hover frame.
const float MARGIN = 4.0;

void main() {
    int index = gl_VertexID / 6;
    vec2 corner = CORNERS[gl_VertexID % 6];
    Entity entity = entities[index];

    vec2 center = WorldToScreen(entity.positionHalf.xy);
    vec2 halfSize = entity.positionHalf.zw * uZoom;

    vRect = vec4(center - halfSize, center + halfSize);
    vFlags = entityFlags[index];
    vData = entity.data;

    vec2 screen = mix(vRect.xy - MARGIN, vRect.zw + MARGIN, corner);
    vScreen = screen;
    vUV = (screen - vRect.xy) / max(vRect.zw - vRect.xy, vec2(0.0001));
    vColor = vec4(1.0);
    gl_Position = ScreenToClip(screen);
}
#endif

#ifdef PASS_COLLIDER
struct Collider {
    vec4 shape; // base xy, round: z = radius, box: zw = half size
    vec4 data;  // x round, y trigger, z active
};
layout(std430, binding = 10) readonly buffer ColliderBuffer { Collider colliders[]; };
layout(std430, binding = 11) readonly buffer ColliderFlagBuffer { uint colliderFlags[]; };

uniform float uColliderThickness;

void main() {
    int index = gl_VertexID / 6;
    vec2 corner = CORNERS[gl_VertexID % 6];
    Collider collider = colliders[index];

    vec2 center = WorldToScreen(collider.shape.xy);
    bool isRound = collider.data.x > 0.5;
    vec2 halfSize = (isRound ? collider.shape.zz : collider.shape.zw) * uZoom;
    float margin = uColliderThickness + 4.0;

    vRect = vec4(center, halfSize);
    vFlags = colliderFlags[index];
    vData = collider.data;

    vec2 screen = mix(center - halfSize - margin, center + halfSize + margin, corner);
    vScreen = screen;
    vColor = vec4(1.0);
    gl_Position = ScreenToClip(screen);
}
#endif

#ifdef PASS_OVERLAY
struct OverlayVertex {
    vec4 positionUV;
    vec4 color;
};
layout(std430, binding = 10) readonly buffer OverlayBuffer { OverlayVertex overlayVertices[]; };

void main() {
    OverlayVertex vertex = overlayVertices[gl_VertexID];
    vColor = vertex.color;
    vUV = vertex.positionUV.zw;
    vScreen = vertex.positionUV.xy;
    gl_Position = ScreenToClip(vertex.positionUV.xy);
}
#endif
