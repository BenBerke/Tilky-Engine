#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_COLLIDER 4

// Collision Sphere debug view stuff
#define SIDECOUNT_SINGLE 0
#define SIDECOUNT_45 1
#define SIDECOUNT_90 2

#define COLLIDER_BOX_VERTEX_COUNT 24
#define COLLIDER_SPHERE_SEGMENTS 24
#define COLLIDER_SPHERE_VERTEX_COUNT (COLLIDER_SPHERE_SEGMENTS * 6)
#define COLLIDER_VERTICES_PER_COLLIDER COLLIDER_SPHERE_VERTEX_COUNT

#define SLOPE_PLUS_X 0
#define SLOPE_MINUS_X 1
#define SLOPE_PLUS_Z 2
#define SLOPE_MINUS_Z 3

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

    vec4 rotation;
// Quaternion layout: x, y, z, w

    ivec4 flags;
// flags.x = isStatic
};

struct Wall {
    vec4 startEnd;
    vec4 color;
    vec4 heights;
    vec4 data;
//data.x = texture region/index;
//data.y = unused;
//data.z = texture anchor height;
//data.w = texture direction;
    vec4 data2;
// data2.xy = texture offset in map units
// data2.zw = independent X/Y UV scale (1.0 = normal)

    vec4 data3;
// data3.x = flip texture X (0.0 or 1.0)
// data3.y = flip texture Y (0.0 or 1.0)
// data3.zw = unused
};

struct FlatTriangle {
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 color;
    vec4 data;
};

struct Sector {
    vec4 floorData;
// floorData.x = offset into sectorFloors
// floorData.y = floor count
};

struct SectorFloor {
    vec4 heights;
// heights.x = floor base height
// heights.y = ceiling base height

    vec4 slopeData;
// slopeData.x = floor slope direction
// slopeData.y = floor slope strength
// slopeData.z = ceiling slope direction
// slopeData.w = ceiling slope strength

    vec4 floorColor;
    vec4 ceilingColor;

    vec4 textureData;
// textureData.x = floor texture region index
// textureData.y = ceiling texture region index

    vec4 textureOffsets;
// textureOffsets.xy = floor texture offset in map units
// textureOffsets.zw = ceiling texture offset in map units

    vec4 textureFlip;
// textureFlip.xy = floor flip X/Y (0.0 or 1.0)
// textureFlip.zw = ceiling flip X/Y (0.0 or 1.0)

    vec4 textureScales;
// textureScales.xy = floor X/Y UV scale (1.0 = normal)
// textureScales.zw = ceiling X/Y UV scale (1.0 = normal)
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

layout(std430, binding = 4) readonly buffer SectorBuffer {
    Sector sectors[];
};

// 5 is used in the fragment shader
layout(std430, binding = 6) readonly buffer ColliderBuffer {
    Collider colliders[];
};

layout(std430, binding = 7) readonly buffer SectorFloorBuffer {
    SectorFloor sectorFloors[];
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

out vec3 vWorldPos;
out vec2 vSurfaceCoord;
flat out vec2 vSurfaceSize;

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

    vColor = isSphere
    ? vec4(0.2, 0.8, 1.0, 1.0)
    : vec4(1.0, 0.8, 0.2, 1.0);

    vWorldPos = worldPosition;
    vSurfaceCoord = vec2(0.0);
    vSurfaceSize = vec2(1.0);

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

vec3 RotateByQuaternion(vec3 value, vec4 quaternion) {
    float lengthSquared = dot(quaternion, quaternion);

    if (lengthSquared < 0.000001) {
        return value;
    }

    vec4 normalizedQuaternion = quaternion * inversesqrt(lengthSquared);
    vec3 quaternionVector = normalizedQuaternion.xyz;

    return value + 2.0 * cross(
    quaternionVector,
    cross(quaternionVector, value) +
    normalizedQuaternion.w * value
    );
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

    vec3 spriteRight;
    vec3 spriteUp;

    if (sprite.flags.x != 0) {
        spriteRight = RotateByQuaternion(
        vec3(1.0, 0.0, 0.0),
        sprite.rotation
        );

        spriteUp = RotateByQuaternion(
        vec3(0.0, 1.0, 0.0),
        sprite.rotation
        );
    }
    else {
        spriteRight = GetYawOnlySpriteRight(spriteWorldPos);
        spriteUp = vec3(0.0, 1.0, 0.0);
    }

    vec3 worldPos =
    bottomCenter +
    spriteRight * side * halfWidth +
    spriteUp * heightT * spriteHeight;

    vSpriteUV = uv;
    vSpriteTextureIndex = textureIndex;
    vColor = sprite.color;

    vWallUV = vec2(0.0);
    vFlatUV = vec2(0.0);

    vTextureIndex = -1;
    vFlatTextureIndex = -1;

    vWorldPos = worldPos;
    vSurfaceCoord = uv;
    vSurfaceSize = vec2(1.0);

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

vec4 GetSectorBounds(int sectorIndex, FlatTriangle initialTriangle) {
    vec2 minimum = min(
    min(initialTriangle.a.xy, initialTriangle.b.xy),
    initialTriangle.c.xy
    );

    vec2 maximum = max(
    max(initialTriangle.a.xy, initialTriangle.b.xy),
    initialTriangle.c.xy
    );

    for (int triangleIndex = 0;
    triangleIndex < flatTriangles.length();
    ++triangleIndex) {
        FlatTriangle candidate = flatTriangles[triangleIndex];

        if (int(candidate.data.x) != sectorIndex) continue;

        minimum = min(minimum, candidate.a.xy);
        minimum = min(minimum, candidate.b.xy);
        minimum = min(minimum, candidate.c.xy);

        maximum = max(maximum, candidate.a.xy);
        maximum = max(maximum, candidate.b.xy);
        maximum = max(maximum, candidate.c.xy);
    }

    return vec4(
    minimum.x,
    minimum.y,
    maximum.x,
    maximum.y
    );
}

float GetSlopeOffset(
vec2 worldXZ,
vec4 sectorBounds,
int slopeDirection,
float slopeStrength
) {
    if (slopeStrength == 0.0) return 0.0;

    switch (slopeDirection) {
        case SLOPE_PLUS_X:
        return (worldXZ.x - sectorBounds.x) * slopeStrength;

        case SLOPE_MINUS_X:
        return (sectorBounds.z - worldXZ.x) * slopeStrength;

        case SLOPE_PLUS_Z:
        return (worldXZ.y - sectorBounds.y) * slopeStrength;

        case SLOPE_MINUS_Z:
        return (sectorBounds.w - worldXZ.y) * slopeStrength;
    }

    return 0.0;
}

void renderFlat() {
    FlatTriangle triangle = flatTriangles[gl_InstanceID];

    vec4 point =
    gl_VertexID == 0
    ? triangle.a
    : gl_VertexID == 1
    ? triangle.b
    : triangle.c;

    int sectorIndex = int(triangle.data.x);
    int floorIndex = int(triangle.data.y);
    int surfaceType = int(triangle.data.z);

    Sector sector = sectors[sectorIndex];

    int packedFloorIndex =
    int(sector.floorData.x) + floorIndex;

    SectorFloor sectorFloor =
    sectorFloors[packedFloorIndex];

    bool isCeiling = surfaceType == 1;

    float baseHeight;
    int slopeDirection;
    float slopeStrength;

    if (isCeiling) {
        baseHeight = sectorFloor.heights.y;
        slopeDirection = int(sectorFloor.slopeData.z);
        slopeStrength = sectorFloor.slopeData.w;

        vColor = sectorFloor.ceilingColor;
        vFlatTextureIndex = int(sectorFloor.textureData.y);
    }
    else {
        baseHeight = sectorFloor.heights.x;
        slopeDirection = int(sectorFloor.slopeData.x);
        slopeStrength = sectorFloor.slopeData.y;

        vColor = sectorFloor.floorColor;
        vFlatTextureIndex = int(sectorFloor.textureData.x);
    }

    vec4 sectorBounds = GetSectorBounds(sectorIndex, triangle);

    point.z = baseHeight + GetSlopeOffset(
    point.xy,
    sectorBounds,
    slopeDirection,
    slopeStrength
    );

    vTextureIndex = -1;
    vSpriteTextureIndex = -1;

    vec2 textureOffset = isCeiling
    ? sectorFloor.textureOffsets.zw
    : sectorFloor.textureOffsets.xy;

    vec2 textureScale = isCeiling
    ? sectorFloor.textureScales.zw
    : sectorFloor.textureScales.xy;

    vec2 textureFlip = isCeiling
    ? sectorFloor.textureFlip.zw
    : sectorFloor.textureFlip.xy;

    // Match wall UV behaviour: scale, then flip, then offset.
    // Map X/Y correspond to world X/Z. A scale of 2.0 doubles repeats.
    vec2 uv = (point.xy / tileSize) * textureScale;

    if (textureFlip.x > 0.5) uv.x = -uv.x;
    if (textureFlip.y > 0.5) uv.y = -uv.y;

    // Keep UVs unwrapped; the fragment shader repeats the atlas texture.
    vFlatUV = uv + textureOffset / tileSize;

    vWallUV = vec2(0.0);
    vSpriteUV = vec2(0.0);

    vec3 worldPos = vec3(point.x, point.z, point.y);

    vWorldPos = worldPos;
    vSurfaceCoord = point.xy;
    vSurfaceSize = vec2(1.0);

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

float GetWallV(float height, float anchorHeight, float direction) {
    if (direction < 0.0) {
        return (anchorHeight - height) / tileSize;
    }

    return (height - anchorHeight) / tileSize;
}

void renderWall() {
    Wall wall = walls[gl_InstanceID];

    vec2 wallStart2D = wall.startEnd.xy;
    vec2 wallEnd2D = wall.startEnd.zw;

    // Heights are stored per end of the wall so a piece can follow a
    // sloped floor or ceiling instead of being a flat-topped quad.
    // heights.xy = bottom/top at the start point
    // heights.zw = bottom/top at the end point
    float bottomStartHeight = wall.heights.x;
    float topStartHeight = wall.heights.y;
    float bottomEndHeight = wall.heights.z;
    float topEndHeight = wall.heights.w;

    float wallLength = length(wallEnd2D - wallStart2D);

    float textureAnchorHeight = wall.data.z;
    float textureDirection = wall.data.w;

    // The anchor stays a single world height, so the texture keeps a
    // constant vertical alignment and the sloped edges just cut it.
    float bottomStartV = GetWallV(bottomStartHeight, textureAnchorHeight, textureDirection);
    float topStartV = GetWallV(topStartHeight, textureAnchorHeight, textureDirection);
    float bottomEndV = GetWallV(bottomEndHeight, textureAnchorHeight, textureDirection);
    float topEndV = GetWallV(topEndHeight, textureAnchorHeight, textureDirection);

    float rightU = wallLength / tileSize;

    vec2 uvOffset = wall.data2.xy / tileSize;

    vec3 bottomLeft = vec3(
    wallStart2D.x,
    bottomStartHeight,
    wallStart2D.y
    );

    vec3 topLeft = vec3(
    wallStart2D.x,
    topStartHeight,
    wallStart2D.y
    );

    vec3 bottomRight = vec3(
    wallEnd2D.x,
    bottomEndHeight,
    wallEnd2D.y
    );

    vec3 topRight = vec3(
    wallEnd2D.x,
    topEndHeight,
    wallEnd2D.y
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
    vec2(0.0, bottomStartV),
    vec2(0.0, topStartV),
    vec2(rightU, bottomEndV),

    vec2(rightU, bottomEndV),
    vec2(0.0, topStartV),
    vec2(rightU, topEndV)
    );

    float startHeight = max(topStartHeight - bottomStartHeight, 0.0);
    float endHeight = max(topEndHeight - bottomEndHeight, 0.0);

    float wallHeight = max(max(startHeight, endHeight), 0.0001);

    vec2 surfaceCoords[6] = vec2[6](
    vec2(0.0, 0.0),
    vec2(0.0, startHeight),
    vec2(wallLength, 0.0),

    vec2(wallLength, 0.0),
    vec2(0.0, startHeight),
    vec2(wallLength, endHeight)
    );

    vec3 worldPos = positions[gl_VertexID];

    // Scale the repeat rate independently on each axis.
    // A scale of 2.0 gives twice as many repeats on that axis.
    vec2 uv = uvs[gl_VertexID] * wall.data2.zw;

    if (wall.data3.x > 0.5) uv.x = -uv.x;
    if (wall.data3.y > 0.5) uv.y = -uv.y;

    // Apply the offset last so scaling/flipping does not change it.
    vWallUV = uv + uvOffset;
    vFlatUV = vec2(0.0);
    vSpriteUV = vec2(0.0);

    vTextureIndex = int(wall.data.x);
    vFlatTextureIndex = -1;
    vSpriteTextureIndex = -1;
    vColor = wall.color;

    vWorldPos = worldPos;
    vSurfaceCoord = surfaceCoords[gl_VertexID];
    vSurfaceSize = vec2(max(wallLength, 0.0001), wallHeight);

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

void main() {
    if (renderMode == RENDER_FLAT) renderFlat();
    else if (renderMode == RENDER_WALL) renderWall();
    else if (renderMode == RENDER_SPRITE) renderSprite();
    else if (renderMode == RENDER_COLLIDER) RenderColliderVertex();
    else gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
}