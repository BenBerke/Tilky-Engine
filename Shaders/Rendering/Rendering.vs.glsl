#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3
#define RENDER_COLLIDER 4

// Collision Sphere debug view stuff
#define SIDECOUNT_SINGLE 0
#define SIDECOUNT_45 1
#define SIDECOUNT_90 2

#define DECAL_WALL 0
#define DECAL_FLOOR 1

#define COLLIDER_BOX_VERTEX_COUNT 24
#define COLLIDER_SPHERE_SEGMENTS 24
#define COLLIDER_SPHERE_VERTEX_COUNT (COLLIDER_SPHERE_SEGMENTS * 6)
#define COLLIDER_VERTICES_PER_COLLIDER COLLIDER_SPHERE_VERTEX_COUNT

struct Decal {
    vec4 startEnd;
    vec4 color;
    vec4 heights;
    vec4 data;
};

struct Sprite {
    vec4 positionSize;
    vec4 color;

    ivec4 textureIndices0; // N, NE, E, SE
    ivec4 textureIndices1; // S, SW, W, NW

    vec4 data;
// data.x = sprite width
// data.y = sideCount
// data.z = forward.x
// data.w = forward.y
};

struct Wall {
    vec4 startEnd;
    vec4 color;
    vec4 heights;
    vec4 data;
    vec4 textureOffset_padding;
};

struct FlatTriangle {
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 color;
    vec4 data;
};

struct Sector {
    vec4 heights;
    vec4 floorColor;
    vec4 ceilingColor;
    vec4 textureData;
};

struct Collider {
    vec4 positionType;
    vec4 scale;
};

layout(std430, binding = 0) readonly buffer WallBuffer {
    Wall walls[];
};

layout(std430, binding = 1) readonly buffer FlatTriangleBuffer {
    FlatTriangle flatTriangles[];
};

layout(std430, binding = 2) readonly buffer SpriteBuffer {
    Sprite sprites[];
};

layout(std430, binding = 3) readonly buffer DecalBuffer {
    Decal decals[];
};

layout(std430, binding = 4) readonly buffer SectorBuffer {
    Sector sectors[];
};

// 5 is used in the fragment shader
layout(std430, binding = 6) readonly buffer ColliderBuffer {
    Collider colliders[];
};

uniform mat4 uView;
uniform mat4 uProjection;
uniform int renderMode;
uniform vec3 uCameraWorldPos;

out vec2 vWallUV;
out vec2 vFlatUV;

flat out int vTextureIndex;
flat out int vFlatTextureIndex;
flat out vec4 vColor;

out vec2 vSpriteUV;
flat out int vSpriteTextureIndex;

out vec2 vDecalUV;

const float tileSize = 32.0;
const float PI = 3.14159265359;

vec3 ToRenderWorld(vec3 entityWorld) {
    return vec3(
    entityWorld.x,
    entityWorld.z,
    entityWorld.y
    );
}

vec2 SafeNormalize2(vec2 value, vec2 fallback) {
    float lenSq = dot(value, value);

    if (lenSq < 0.000001) {
        return fallback;
    }

    return value * inversesqrt(lenSq);
}

float SignedAngle(vec2 from, vec2 to) {
    float crossValue = from.x * to.y - from.y * to.x;
    float dotValue = dot(from, to);

    return atan(crossValue, dotValue);
}

float NormalizeAnglePositive(float angle) {
    angle = mod(angle, PI * 2.0);

    if (angle < 0.0) {
        angle += PI * 2.0;
    }

    return angle;
}

int GetSpriteTextureIndex(Sprite sprite, int slot) {
    if (slot < 4) {
        return sprite.textureIndices0[slot];
    }

    return sprite.textureIndices1[slot - 4];
}

int Pick8WaySpriteSlot(vec2 spriteForward, vec2 toCamera) {
    float angle = SignedAngle(spriteForward, toCamera);
    angle = NormalizeAnglePositive(angle);

    return int(floor((angle + PI / 8.0) / (PI / 4.0))) % 8;
}

int Pick4WaySpriteSlot(vec2 spriteForward, vec2 toCamera) {
    float angle = SignedAngle(spriteForward, toCamera);
    angle = NormalizeAnglePositive(angle);

    int quadrant = int(floor((angle + PI / 4.0) / (PI / 2.0))) % 4;

    if (quadrant == 0) return 0; // N
    if (quadrant == 1) return 2; // E
    if (quadrant == 2) return 4; // S

    return 6; // W
}

int SelectSpriteTextureIndex(Sprite sprite, vec3 spriteWorldPos) {
    int sideCount = int(sprite.data.y);

    if (sideCount == SIDECOUNT_SINGLE) {
        return sprite.textureIndices0.x;
    }

    vec2 spriteForward = SafeNormalize2(sprite.data.zw, vec2(1.0, 0.0));
    vec2 toCamera = SafeNormalize2(
    uCameraWorldPos.xz - spriteWorldPos.xz,
    spriteForward
    );

    int slot = 0;

    if (sideCount == SIDECOUNT_90) {
        slot = Pick4WaySpriteSlot(spriteForward, toCamera);
    }
    else if (sideCount == SIDECOUNT_45) {
        slot = Pick8WaySpriteSlot(spriteForward, toCamera);
    }

    return GetSpriteTextureIndex(sprite, slot);
}

vec3 GetBoxCorner(vec3 center, vec3 halfSize, int index) {
    return center + vec3(
    (index & 1) != 0 ? halfSize.x : -halfSize.x,
    (index & 2) != 0 ? halfSize.y : -halfSize.y,
    (index & 4) != 0 ? halfSize.z : -halfSize.z
    );
}

vec3 GetBoxVertex(Collider collider, int localVertex) {
    const int edgeIndices[24] = int[24](
    0, 1,
    1, 3,
    3, 2,
    2, 0,

    4, 5,
    5, 7,
    7, 6,
    6, 4,

    0, 4,
    1, 5,
    2, 6,
    3, 7
    );

    vec3 center = collider.positionType.xyz;
    vec3 halfSize = collider.scale.xyz * 0.5;

    if (localVertex >= COLLIDER_BOX_VERTEX_COUNT) {
        return center;
    }

    return GetBoxCorner(center, halfSize, edgeIndices[localVertex]);
}

vec3 GetSphereVertex(Collider collider, int localVertex) {
    vec3 center = collider.positionType.xyz;

    float radius = collider.scale.x;

    int ringVertexCount = COLLIDER_SPHERE_SEGMENTS * 2;

    int ring = localVertex / ringVertexCount;
    int ringLocal = localVertex % ringVertexCount;

    int segment = ringLocal / 2;
    bool secondPoint = (ringLocal & 1) != 0;

    float t = float(segment + (secondPoint ? 1 : 0)) / float(COLLIDER_SPHERE_SEGMENTS);
    float angle = t * PI * 2.0;

    float c = cos(angle);
    float s = sin(angle);

    if (ring == 0) return center + vec3(c * radius, s * radius, 0.0);
    if (ring == 1) return center + vec3(c * radius, 0.0, s * radius);

    return center + vec3(0.0, c * radius, s * radius);
}

void RenderColliderVertex() {
    int colliderIndex = gl_VertexID / COLLIDER_VERTICES_PER_COLLIDER;
    int localVertex = gl_VertexID % COLLIDER_VERTICES_PER_COLLIDER;

    Collider collider = colliders[colliderIndex];

    bool isSphere = collider.positionType.w < 0.5;

    vec3 worldPosition = isSphere
    ? GetSphereVertex(collider, localVertex)
    : GetBoxVertex(collider, localVertex);

    worldPosition = ToRenderWorld(worldPosition);

    vColor = isSphere
    ? vec4(0.2, 0.8, 1.0, 1.0)
    : vec4(1.0, 0.8, 0.2, 1.0);

    gl_Position = uProjection * uView * vec4(worldPosition, 1.0);
}

vec3 GetYawOnlySpriteRight(vec3 spriteWorldPos) {
    vec2 toCamera = uCameraWorldPos.xz - spriteWorldPos.xz;

    if (dot(toCamera, toCamera) < 0.000001) {
        return vec3(1.0, 0.0, 0.0);
    }

    toCamera = normalize(toCamera);

    return vec3(-toCamera.y, 0.0, toCamera.x);
}

void renderDecal() {
    Decal decal = decals[gl_InstanceID];

    int textureIndex = int(decal.data.x);
    int decalType = int(decal.data.y);

    vec3 worldPos;
    vec2 uv;

    if (decalType == DECAL_FLOOR) {
        vec2 decalMin = decal.startEnd.xy;
        vec2 decalMax = decal.startEnd.zw;

        float floorHeight = decal.heights.x;

        vec3 minMin = vec3(
        decalMin.x,
        floorHeight,
        decalMin.y
        );

        vec3 minMax = vec3(
        decalMin.x,
        floorHeight,
        decalMax.y
        );

        vec3 maxMin = vec3(
        decalMax.x,
        floorHeight,
        decalMin.y
        );

        vec3 maxMax = vec3(
        decalMax.x,
        floorHeight,
        decalMax.y
        );

        /*
         * Counter-clockwise when viewed from above.
         * This makes the decal face upward when face culling is enabled.
         */
        vec3 positions[6] = vec3[6](
        minMin,
        minMax,
        maxMin,

        maxMin,
        minMax,
        maxMax
        );

        vec2 uvs[6] = vec2[6](
        vec2(0.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),

        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 0.0)
        );

        worldPos = positions[gl_VertexID];
        uv = uvs[gl_VertexID];
    }
    else {
        /*
         * Existing wall decal behaviour.
         */
        vec2 decalStart2D = decal.startEnd.xy;
        vec2 decalEnd2D = decal.startEnd.zw;

        float bottomHeight = decal.heights.x;
        float topHeight = decal.heights.y;

        vec3 bottomLeft = vec3(
        decalStart2D.x,
        bottomHeight,
        decalStart2D.y
        );

        vec3 topLeft = vec3(
        decalStart2D.x,
        topHeight,
        decalStart2D.y
        );

        vec3 bottomRight = vec3(
        decalEnd2D.x,
        bottomHeight,
        decalEnd2D.y
        );

        vec3 topRight = vec3(
        decalEnd2D.x,
        topHeight,
        decalEnd2D.y
        );

        vec3 positions[6] = vec3[6](
        bottomLeft,
        topLeft,
        bottomRight,

        bottomRight,
        topLeft,
        topRight
        );

        vec2 uvs[6] = vec2[6](
        vec2(0.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),

        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 0.0)
        );

        worldPos = positions[gl_VertexID];
        uv = uvs[gl_VertexID];
    }

    vWallUV = vec2(0.0);
    vFlatUV = vec2(0.0);
    vSpriteUV = vec2(0.0);

    vTextureIndex = textureIndex;
    vFlatTextureIndex = -1;
    vSpriteTextureIndex = -1;

    vDecalUV = uv;
    vColor = decal.color / 255.0;

    gl_Position =
    uProjection *
    uView *
    vec4(worldPos, 1.0);
}

void renderSprite() {
    Sprite sprite = sprites[gl_InstanceID];

    vec3 spriteWorldPos = sprite.positionSize.xyz;

    float spriteHeight = sprite.positionSize.w;
    float spriteWidth = sprite.data.x;
    float halfWidth = spriteWidth * 0.5;

    int textureIndex = SelectSpriteTextureIndex(sprite, spriteWorldPos);

    float side;
    float heightT;
    vec2 uv;

    if (gl_VertexID == 0) {
        side = -1.0;
        heightT = 0.0;
        uv = vec2(0.0, 1.0);
    }
    else if (gl_VertexID == 1) {
        side = -1.0;
        heightT = 1.0;
        uv = vec2(0.0, 0.0);
    }
    else if (gl_VertexID == 2) {
        side = 1.0;
        heightT = 0.0;
        uv = vec2(1.0, 1.0);
    }
    else {
        side = 1.0;
        heightT = 1.0;
        uv = vec2(1.0, 0.0);
    }

    vec3 bottomCenter = spriteWorldPos;

    vec3 spriteRight = GetYawOnlySpriteRight(spriteWorldPos);
    vec3 spriteUp = vec3(0.0, 1.0, 0.0);

    vec3 worldPos =
    bottomCenter +
    spriteRight * side * halfWidth +
    spriteUp * heightT * spriteHeight;

    vSpriteUV = uv;
    vSpriteTextureIndex = textureIndex;
    vColor = sprite.color / 255.0;

    vWallUV = vec2(0.0);
    vFlatUV = vec2(0.0);
    vDecalUV = vec2(0.0);

    vTextureIndex = -1;
    vFlatTextureIndex = -1;

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

void renderFlat() {
    FlatTriangle triangle = flatTriangles[gl_InstanceID];

    vec4 point;

    if (gl_VertexID == 0) {
        point = triangle.a;
    }
    else if (gl_VertexID == 1) {
        point = triangle.b;
    }
    else {
        point = triangle.c;
    }

    int sectorIndex = int(triangle.data.x);
    int boundaryIndex = int(triangle.data.y);

    Sector sector = sectors[sectorIndex];

    float storeyHeight = sector.heights.y - sector.heights.x;

    point.z = sector.heights.x + storeyHeight * float(boundaryIndex);

    if (boundaryIndex == 0) {
        vColor = sector.floorColor / 255.0;
    }
    else {
        vColor = sector.ceilingColor / 255.0;
    }

    vFlatTextureIndex = boundaryIndex == 0
    ? int(sector.textureData.x)
    : int(sector.textureData.y);

    vTextureIndex = -1;

    vFlatUV = point.xy / tileSize;
    vWallUV = vec2(0.0);

    vec3 worldPos = vec3(point.x, point.z, point.y);

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

void renderWall() {
    Wall wall = walls[gl_InstanceID];

    vec2 wallStart2D = wall.startEnd.xy;
    vec2 wallEnd2D = wall.startEnd.zw;

    float bottomHeight = wall.heights.x;
    float topHeight = wall.heights.y;

    float wallLength = length(wallEnd2D - wallStart2D);

    float textureAnchorHeight = wall.data.z;
    float textureDirection = wall.data.w;

    float bottomV;
    float topV;

    if (textureDirection < 0.0) {
        bottomV = (textureAnchorHeight - bottomHeight) / tileSize;
        topV = (textureAnchorHeight - topHeight) / tileSize;
    }
    else {
        bottomV = (bottomHeight - textureAnchorHeight) / tileSize;
        topV = (topHeight - textureAnchorHeight) / tileSize;
    }

    float rightU = wallLength / tileSize;

    vec2 uvOffset = wall.textureOffset_padding.xy / tileSize;

    vec3 bottomLeft = vec3(wallStart2D.x, bottomHeight, wallStart2D.y);
    vec3 topLeft = vec3(wallStart2D.x, topHeight, wallStart2D.y);
    vec3 bottomRight = vec3(wallEnd2D.x, bottomHeight, wallEnd2D.y);
    vec3 topRight = vec3(wallEnd2D.x, topHeight, wallEnd2D.y);

    vec3 positions[6] = vec3[6](
    bottomLeft,
    topLeft,
    bottomRight,

    bottomRight,
    topLeft,
    topRight
    );

    vec2 uvs[6] = vec2[6](
    vec2(0.0, bottomV) + uvOffset,
    vec2(0.0, topV) + uvOffset,
    vec2(rightU, bottomV) + uvOffset,

    vec2(rightU, bottomV) + uvOffset,
    vec2(0.0, topV) + uvOffset,
    vec2(rightU, topV) + uvOffset
    );

    vec3 worldPos = positions[gl_VertexID];

    vWallUV = uvs[gl_VertexID];
    vFlatUV = vec2(0.0);
    vSpriteUV = vec2(0.0);
    vDecalUV = vec2(0.0);

    vTextureIndex = int(wall.data.x);
    vFlatTextureIndex = -1;
    vSpriteTextureIndex = -1;
    vColor = wall.color / 255.0;

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

void main() {
    if (renderMode == RENDER_FLAT) {
        renderFlat();
    }
    else if (renderMode == RENDER_WALL) {
        renderWall();
    }
    else if (renderMode == RENDER_SPRITE) {
        renderSprite();
    }
    else if (renderMode == RENDER_DECAL) {
        renderDecal();
    }
    else if (renderMode == RENDER_COLLIDER) {
        RenderColliderVertex();
    }
    else {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
    }
}