#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform DrawPushConstants {
    mat4 modelViewProjection;
} drawData;

layout(location = 0) out vec3 color;

void main() {
    gl_Position = drawData.modelViewProjection * vec4(inPosition, 1.0);
    color = inColor;
}
