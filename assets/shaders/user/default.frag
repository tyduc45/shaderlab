#version 460

layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;

layout(push_constant) uniform DrawPushConstants {
    mat4 modelViewProjection;
    vec4 baseColorFactor;
} drawData;

void main() {
    vec3 n = normalize(normal);
    float lighting = 0.18 + 0.82 * max(dot(n, normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    vec4 base = texture(baseColorTexture, uv) * drawData.baseColorFactor;
    outColor = vec4(base.rgb * lighting, base.a);
}
