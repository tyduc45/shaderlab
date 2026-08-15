#version 460

layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(normal);
    float lighting = 0.18 + 0.82 * max(dot(n, normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    vec3 base = mix(vec3(0.12, 0.28, 0.65), vec3(0.85, 0.38, 0.12), uv.x);
    outColor = vec4(base * lighting, 1.0);
}
