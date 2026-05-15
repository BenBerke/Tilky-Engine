#version 430 core

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3

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

layout(std430, binding = 0) readonly buffer WallBuffer {
    Wall walls[];
};

layout(std430, binding = 1) readonly buffer FlatTriangleBuffer {
    FlatTriangle flatTriangles[];
};

layout(std430, binding = 4) readonly buffer SectorBuffer {
    Sector sectors[];
};

layout(std430, binding = 2) readonly buffer SpriteBuffer {
    Sprite sprites[];
};

layout(std430, binding = 3) readonly buffer DecalBuffer {
    Decal decals[];
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

const float tileSize = 32.0;

vec3 GetYawOnlySpriteRight() {
    vec3 cameraForward = -vec3(
    uView[0][2],
    uView[1][2],
    uView[2][2]
    );

    // Remove pitch / up-down camera rotation.
    cameraForward.y = 0.0;

    if (length(cameraForward) < 0.0001) {
        return vec3(1.0, 0.0, 0.0);
    }

    cameraForward = normalize(cameraForward);

    // Build right vector from flattened forward.
    return normalize(cross(vec3(0.0, 1.0, 0.0), cameraForward));
}

void renderDecal() {
    Decal decal = decals[gl_InstanceID];

    vec2 decalStart2D = decal.startEnd.xy;
    vec2 decalEnd2D = decal.startEnd.zw;

    float bottomHeight = decal.heights.x;
    float topHeight = decal.heights.y;

    int textureIndex = int(decal.data.x);

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

    vec2 spriteWorldPos = sprite.positionSize.xy;

    float bottomHeight = sprite.positionSize.z;
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

    vec3 bottomCenter = vec3(
    spriteWorldPos.x,
    bottomHeight,
    spriteWorldPos.y
    );

    vec3 spriteRight = GetYawOnlySpriteRight();
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

    vFlatTextureIndex = int(triangle.data.z);
    vTextureIndex = -1;

    vFlatUV = point.xy / tileSize;
    vWallUV = vec2(0.0);

    vec3 worldPos = vec3(
    point.x,
    point.z,
    point.y
    );

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
    vec2(0.0, bottomV),
    vec2(0.0, topV),
    vec2(rightU, bottomV),

    vec2(rightU, bottomV),
    vec2(0.0, topV),
    vec2(rightU, topV)
    );

    vec3 worldPos = positions[gl_VertexID];

    vWallUV = uvs[gl_VertexID];
    vFlatUV = vec2(0.0);

    vTextureIndex = int(wall.data.x);
    vFlatTextureIndex = -1;
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
    else {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
    }
}