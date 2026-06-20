#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3
#define RENDER_COLLIDER 4

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
    vec4 data;
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

struct Collider{
    vec4 positionType; // World x, y, z; type. 0 = sphere, 1 = AABB
    vec4 scale; // if sphere, x = radius. if box use vec3 is x y z
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

out vec2 vWallUV;
out vec2 vFlatUV;

flat out int vTextureIndex;
flat out int vFlatTextureIndex;
flat out vec4 vColor;

out vec2 vSpriteUV;
flat out int vSpriteTextureIndex;
out vec2 vDecalUV;


//todo makes this a world/per-wall setting
const float tileSize = 32.0;

uniform vec3 uCameraWorldPos;

const float PI = 3.14159265359;

vec3 ToRenderWorld(vec3 entityWorld) {
    return vec3(
    entityWorld.x, // world X
    entityWorld.z, // vertical height
    entityWorld.y  // horizontal depth
    );
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

    vec2 decalStart2D = decal.startEnd.xy;
    vec2 decalEnd2D = decal.startEnd.zw;

    float bottomHeight = decal.heights.x;
    float topHeight = decal.heights.y;

    int textureIndex = int(decal.data.x);

    vec3 bottomLeft = vec3(decalStart2D.x, bottomHeight, decalStart2D.y);
    vec3 topLeft = vec3(decalStart2D.x, topHeight, decalStart2D.y);
    vec3 bottomRight = vec3(decalEnd2D.x, bottomHeight, decalEnd2D.y);
    vec3 topRight = vec3(decalEnd2D.x, topHeight, decalEnd2D.y);

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

    vec3 worldPos = positions[gl_VertexID];

    vWallUV = vec2(0.0);
    vFlatUV = vec2(0.0);
    vSpriteUV = vec2(0.0);

    vTextureIndex = textureIndex;
    vFlatTextureIndex = -1;
    vSpriteTextureIndex = -1;

    vDecalUV = uvs[gl_VertexID];
    vColor = decal.color / 255.0;

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}

void renderSprite() {
    Sprite sprite = sprites[gl_InstanceID];

    // positionSize.xyz is now the real 3D world position:
    // x = world x
    // y = vertical height
    // z = world z
    vec3 spriteWorldPos = sprite.positionSize.xyz;

    float spriteHeight = sprite.positionSize.w;

    float spriteWidth = sprite.data.x;
    int textureIndex = int(sprite.data.y);

    float halfWidth = spriteWidth * 0.5;

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

    // point.xy = map position
    // point.z = height
    point.z = sector.heights.x + storeyHeight * float(boundaryIndex);

    if (boundaryIndex == 0) {
        vColor = sector.floorColor / 255.0;
    }
    else {
        vColor = sector.ceilingColor / 255.0;
    }

    vFlatTextureIndex = boundaryIndex == 0 ? int(sector.textureData.x)   // floorTextureIndex
    : int(sector.textureData.y);  // ceilingTextureIndex
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

    // Positive offset moves the sampled texture.
    // Flip the sign if it moves visually opposite to what you want.
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
    if (renderMode == RENDER_FLAT) renderFlat();
    else if (renderMode == RENDER_WALL) renderWall();
    else if (renderMode == RENDER_SPRITE) renderSprite();
    else if (renderMode == RENDER_DECAL) renderDecal();
    else if (renderMode == RENDER_COLLIDER) RenderColliderVertex();
    else gl_Position = vec4(2.0, 2.0, 0.0, 1.0);

}