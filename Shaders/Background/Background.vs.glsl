#version 430 core

out vec2 vUV;

void main() {
    const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
    );

    const vec2 uvs[3] = vec2[3](
    vec2(0.0, 1.0),
    vec2(2.0, 1.0),
    vec2(0.0, -1.0)
    );

    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    vUV = uvs[gl_VertexID];
}
