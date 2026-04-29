#version 430 core

#define PI 3.14159265359
#define SCREEN_WIDTH 1680.0
#define SCREEN_HEIGHT 960.0

#define RENDER_WALL 0
#define RENDER_FLAT 1
#define RENDER_SPRITE 2
#define RENDER_DECAL 3

struct Wall {
    vec4 startEnd;
    vec4 color;
    vec4 heights;
    vec4 data;      // x = textureIndex
};

struct FlatTriangle {
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 color;
    vec4 data; // x = textureIndex
};

struct Sprite {
    vec4 positionSize;
// x = world x
// y = world y
// z = bottom height
// w = sprite height

    vec4 color;

    vec4 data;
// x = width
// y = textureIndex
};

struct Decal {
    vec4 startEnd;
    vec4 color;
    vec4 heights;
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

layout(std430, binding = 2) readonly buffer SpriteBuffer {
    Sprite sprites[];
};

layout(std430, binding = 3) readonly buffer DecalBuffer {
    Decal decals[];
};

layout(std430, binding = 4) readonly buffer SectorBuffer {
    Sector sectors[];
};

flat out vec4 vWallColor;

flat out float fScreenXStart;
flat out float fScreenXEnd;

flat out float fTopYStart;
flat out float fTopYEnd;
flat out float fBottomYStart;
flat out float fBottomYEnd;

flat out float fSStart;
flat out float fSEnd;
flat out float fZLeft;
flat out float fZRight;

noperspective out float vFlatInvZ;

flat out int vTextureIndex;
flat out float fWallWorldHeight;

noperspective out vec2 vFlatWorldOverZ;
flat out int vFlatTextureIndex;

noperspective out vec2 vSpriteUV;
flat out int vSpriteTextureIndex;
flat out float fSpriteViewDepth;
flat out vec4 vSpriteColor;

uniform vec2 playerPos;
uniform float playerAngle;
uniform float playerHeight;
uniform float playerCamZ;
uniform int renderMode;

const float FOV =  90.0;
const float halfFov = FOV * .5;
float horizonY = SCREEN_HEIGHT * playerCamZ;
const float wallHeight = 32.0;
const float nearPlane = .1;

float degToRad(float angle) {
    return angle * (PI / 180.0);
}

vec2 rotate(vec2 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);

    return vec2(
    p.x * c - p.y * s,
    p.x * s + p.y * c
    );
}

float getViewDepth(vec2 worldPos) {
    float playerAngleInRad = degToRad(playerAngle);

    vec2 relative = worldPos - playerPos;
    vec2 view = rotate(relative, playerAngleInRad);

    return view.y;
}

vec2 projectToNdc(vec2 worldPos, float height) {
    float playerAngleInRad = degToRad(playerAngle);
    float halfFovInRad = degToRad(halfFov);

    vec2 relative = worldPos - playerPos;
    vec2 view = rotate(relative, playerAngleInRad);

    float z = view.y;

    float focalLength = (SCREEN_WIDTH * 0.5) / tan(halfFovInRad);

    float screenX = SCREEN_WIDTH * 0.5 + (view.x / z) * focalLength;

    float verticalOffset = height - playerHeight;
    float screenY = horizonY - (verticalOffset / z) * focalLength;

    float ndcX = (screenX / SCREEN_WIDTH) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / SCREEN_HEIGHT) * 2.0;

    return vec2(ndcX, ndcY);
}

void outputDummyWallData() {
    fScreenXStart = 0.0;
    fScreenXEnd = 0.0;

    fTopYStart = 0.0;
    fTopYEnd = 0.0;
    fBottomYStart = 0.0;
    fBottomYEnd = 0.0;

    fSStart = 0.0;
    fSEnd = 0.0;
    fZLeft = 1.0;
    fZRight = 1.0;

    fWallWorldHeight = 1.0;
    vTextureIndex = -1;
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
    int flatType = int(triangle.data.y);

    Sector sector = sectors[sectorIndex];

    if(flatType == 2){
        point.z = sector.heights.y + sector.heights.y - sector.heights.x;
        vWallColor = sector.ceilingColor / 255.0;
        vFlatTextureIndex = int(sector.textureData.y);
    }
    else if (flatType == 1) {
        // Ceiling
        point.z = sector.heights.y;
        vWallColor = sector.ceilingColor / 255.0;
        vFlatTextureIndex = int(sector.textureData.y);
    }
    else {
        // Floor
        point.z = sector.heights.x;
        vWallColor = sector.floorColor / 255.0;
        vFlatTextureIndex = int(sector.textureData.x);
    }

    float viewDepth = getViewDepth(point.xy);
    vFlatInvZ = 1.0 / viewDepth;

    outputDummyWallData();

    vFlatWorldOverZ = point.xy * vFlatInvZ;

    vec2 ndc = projectToNdc(point.xy, point.z);

    gl_Position = vec4(ndc, 0.0, 1.0);
}

float projectScreenX(vec2 viewPoint) {
    float halfFovInRad = degToRad(halfFov);
    float focalLength = (SCREEN_WIDTH * 0.5) / tan(halfFovInRad);

    return SCREEN_WIDTH * 0.5 + (viewPoint.x / viewPoint.y) * focalLength;
}

float projectScreenY(float height, float viewDepth) {
    float halfFovInRad = degToRad(halfFov);
    float focalLength = (SCREEN_WIDTH * 0.5) / tan(halfFovInRad);

    float verticalOffset = height - playerHeight;

    return horizonY - (verticalOffset / viewDepth) * focalLength;
}

void renderSprite() {
    Sprite sprite = sprites[gl_InstanceID];

    vec2 spriteWorldPos = sprite.positionSize.xy;

    float bottomHeight = sprite.positionSize.z;
    float spriteHeight = sprite.positionSize.w;

    float spriteWidth = sprite.data.x;
    int textureIndex = int(sprite.data.y);

    float playerAngleInRad = degToRad(playerAngle);

    vec2 relative = spriteWorldPos - playerPos;
    vec2 centerView = rotate(relative, playerAngleInRad);

    if (centerView.y <= nearPlane) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    float halfWidth = spriteWidth * 0.5;

    float side;
    float heightT;
    vec2 uv;

    if (gl_VertexID == 0) {
        // bottom left
        side = -1.0;
        heightT = 0.0;
        uv = vec2(0.0, 1.0);
    }
    else if (gl_VertexID == 1) {
        // top left
        side = -1.0;
        heightT = 1.0;
        uv = vec2(0.0, 0.0);
    }
    else if (gl_VertexID == 2) {
        // bottom right
        side = 1.0;
        heightT = 0.0;
        uv = vec2(1.0, 1.0);
    }
    else {
        // top right
        side = 1.0;
        heightT = 1.0;
        uv = vec2(1.0, 0.0);
    }

    vec2 viewPoint = vec2(
    centerView.x + side * halfWidth,
    centerView.y
    );

    float screenX = projectScreenX(viewPoint);

    float worldHeight = bottomHeight + heightT * spriteHeight;
    float screenY = projectScreenY(worldHeight, centerView.y);

    float ndcX = (screenX / SCREEN_WIDTH) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / SCREEN_HEIGHT) * 2.0;

    vSpriteUV = uv;
    vSpriteTextureIndex = textureIndex;
    fSpriteViewDepth = centerView.y;
    vSpriteColor = sprite.color / 255.0;

    outputDummyWallData();

    vFlatInvZ = 1.0;
    vFlatWorldOverZ = vec2(0.0);
    vFlatTextureIndex = -1;

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}

void renderWall() {
    Wall wall = walls[gl_InstanceID];

    vFlatInvZ = 1.0;
    vFlatWorldOverZ = vec2(0.0);
    vFlatTextureIndex = -1;

    vec2 wallStart = wall.startEnd.xy;
    vec2 wallEnd = wall.startEnd.zw;

    float floorHeight = wall.heights.x;
    float ceilingHeight = wall.heights.y;

    vTextureIndex = int(wall.data.x);
    fWallWorldHeight = ceilingHeight - floorHeight;

    float playerAngleInRad = degToRad(playerAngle);

    vec2 relativeStart = wallStart - playerPos;
    vec2 relativeEnd = wallEnd - playerPos;

    vec2 viewStart = rotate(relativeStart, playerAngleInRad);
    vec2 viewEnd = rotate(relativeEnd, playerAngleInRad);

    float wallLength = length(wallEnd - wallStart);
    float sStart = 0.0;
    float sEnd = wallLength;

    if (viewStart.y <= nearPlane && viewEnd.y <= nearPlane) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    if (viewStart.y <= nearPlane) {
        float t = (nearPlane - viewStart.y) / (viewEnd.y - viewStart.y);
        viewStart = mix(viewStart, viewEnd, t);
        sStart = mix(sStart, sEnd, t);
    }

    if (viewEnd.y <= nearPlane) {
        float t = (nearPlane - viewEnd.y) / (viewStart.y - viewEnd.y);
        viewEnd = mix(viewEnd, viewStart, t);
        sEnd = mix(sEnd, sStart, t);
    }

    float screenXStart = projectScreenX(viewStart);
    float screenXEnd = projectScreenX(viewEnd);

    float topYStart = projectScreenY(ceilingHeight, viewStart.y);
    float topYEnd = projectScreenY(ceilingHeight, viewEnd.y);

    float bottomYStart = projectScreenY(floorHeight, viewStart.y);
    float bottomYEnd = projectScreenY(floorHeight, viewEnd.y);

    float leftX = (screenXStart / SCREEN_WIDTH) * 2.0 - 1.0;
    float rightX = (screenXEnd / SCREEN_WIDTH) * 2.0 - 1.0;

    float topLeftY = 1.0 - (topYStart / SCREEN_HEIGHT) * 2.0;
    float bottomLeftY = 1.0 - (bottomYStart / SCREEN_HEIGHT) * 2.0;
    float topRightY = 1.0 - (topYEnd / SCREEN_HEIGHT) * 2.0;
    float bottomRightY = 1.0 - (bottomYEnd / SCREEN_HEIGHT) * 2.0;

    vec2 verts[4] = vec2[4](
    vec2(leftX, bottomLeftY),
    vec2(leftX, topLeftY),
    vec2(rightX, bottomRightY),
    vec2(rightX, topRightY)
    );

    vWallColor = wall.color / 255.0;

    fScreenXStart = screenXStart;
    fScreenXEnd = screenXEnd;

    fTopYStart = topYStart;
    fTopYEnd = topYEnd;
    fBottomYStart = bottomYStart;
    fBottomYEnd = bottomYEnd;

    fSStart = sStart;
    fSEnd = sEnd;
    fZLeft = viewStart.y;
    fZRight = viewEnd.y;

    gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);
}

void renderDecal() {
    Decal decal = decals[gl_InstanceID];

    vFlatInvZ = 1.0;
    vFlatWorldOverZ = vec2(0.0);
    vFlatTextureIndex = -1;

    vec2 wallStart = decal.startEnd.xy;
    vec2 wallEnd = decal.startEnd.zw;

    float floorHeight = decal.heights.x;
    float ceilingHeight = decal.heights.y;

    vTextureIndex = int(decal.data.x);
    fWallWorldHeight = ceilingHeight - floorHeight;

    float playerAngleInRad = degToRad(playerAngle);

    vec2 relativeStart = wallStart - playerPos;
    vec2 relativeEnd = wallEnd - playerPos;

    vec2 viewStart = rotate(relativeStart, playerAngleInRad);
    vec2 viewEnd = rotate(relativeEnd, playerAngleInRad);

    float wallLength = length(wallEnd - wallStart);
    float sStart = 0.0;
    float sEnd = wallLength;

    if (viewStart.y <= nearPlane && viewEnd.y <= nearPlane) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    if (viewStart.y <= nearPlane) {
        float t = (nearPlane - viewStart.y) / (viewEnd.y - viewStart.y);
        viewStart = mix(viewStart, viewEnd, t);
        sStart = mix(sStart, sEnd, t);
    }

    if (viewEnd.y <= nearPlane) {
        float t = (nearPlane - viewEnd.y) / (viewStart.y - viewEnd.y);
        viewEnd = mix(viewEnd, viewStart, t);
        sEnd = mix(sEnd, sStart, t);
    }

    float screenXStart = projectScreenX(viewStart);
    float screenXEnd = projectScreenX(viewEnd);

    float topYStart = projectScreenY(ceilingHeight, viewStart.y);
    float topYEnd = projectScreenY(ceilingHeight, viewEnd.y);

    float bottomYStart = projectScreenY(floorHeight, viewStart.y);
    float bottomYEnd = projectScreenY(floorHeight, viewEnd.y);

    float leftX = (screenXStart / SCREEN_WIDTH) * 2.0 - 1.0;
    float rightX = (screenXEnd / SCREEN_WIDTH) * 2.0 - 1.0;

    float topLeftY = 1.0 - (topYStart / SCREEN_HEIGHT) * 2.0;
    float bottomLeftY = 1.0 - (bottomYStart / SCREEN_HEIGHT) * 2.0;
    float topRightY = 1.0 - (topYEnd / SCREEN_HEIGHT) * 2.0;
    float bottomRightY = 1.0 - (bottomYEnd / SCREEN_HEIGHT) * 2.0;

    vec2 verts[4] = vec2[4](
    vec2(leftX, bottomLeftY),
    vec2(leftX, topLeftY),
    vec2(rightX, bottomRightY),
    vec2(rightX, topRightY)
    );

    vWallColor = decal.color / 255.0;

    fScreenXStart = screenXStart;
    fScreenXEnd = screenXEnd;

    fTopYStart = topYStart;
    fTopYEnd = topYEnd;
    fBottomYStart = bottomYStart;
    fBottomYEnd = bottomYEnd;

    fSStart = sStart;
    fSEnd = sEnd;
    fZLeft = viewStart.y;
    fZRight = viewEnd.y;

    gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);
}

void main() {
    if (renderMode == RENDER_FLAT) {
        renderFlat();
    }
    else if (renderMode == RENDER_SPRITE) {
        renderSprite();
    }
    else if (renderMode == RENDER_DECAL) {
        renderDecal();
    }
    else {
        renderWall();
    }
}