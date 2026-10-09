#version 430 core

// Full-screen triangle; vNdc is the fragment's normalized device position.
out vec2 vNdc;

void main() {
    const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
    );

    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    vNdc = positions[gl_VertexID];
}
