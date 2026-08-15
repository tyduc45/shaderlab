#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform DrawPushConstants {
    mat4 modelViewProjection;
} drawData;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec2 uv;

void main() {
    gl_Position = drawData.modelViewProjection * vec4(inPosition, 1.0);
    normal = inNormal;
    uv = inUv;
}
