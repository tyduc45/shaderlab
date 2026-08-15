#version 460

layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform MaterialParams {
    // @param name="Coating Tint" type=color default=(1.0,1.0,1.0,1.0) group="Coating"
    vec4 coatingTint;
    // @param name="Iridescence" type=float range=[0.0,2.0] default=1.0 group="Coating"
    float iridescenceStrength;
    // @param name="Engraving Scale" type=float range=[4.0,30.0] default=13.0 group="Engraving"
    float engravingScale;
    // @param name="Engraving Strength" type=float range=[0.0,1.0] default=0.38 group="Engraving"
    float engravingStrength;
    // @param name="Rim Strength" type=float range=[0.0,2.0] default=1.0 group="Lighting"
    float rimStrength;
} material;

// @param name="Base Color Texture" type=texture default=white group="Textures"
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

layout(push_constant) uniform DrawPushConstants {
    mat4 modelViewProjection;
    vec4 baseColorFactor;
} drawData;

const float PI = 3.14159265359;

// A compact cosine palette gives the surface an oil-film iridescence without
// needing extra textures or material parameters.
vec3 spectralPalette(float t) {
    vec3 phase = vec3(0.00, 0.33, 0.67);
    return 0.52 + 0.48 * cos(2.0 * PI * (t + phase));
}

// Signed-ish distance to the edge of a repeating hexagonal cell. fwidth in
// the caller keeps the engraved lines stable as the camera moves.
float hexEdge(vec2 p) {
    p *= vec2(1.0, 1.1547005);
    vec2 cellA = fract(p) - 0.5;
    vec2 cellB = fract(p + 0.5) - 0.5;
    vec2 cell = dot(cellA, cellA) < dot(cellB, cellB) ? cellA : cellB;
    return max(abs(cell.y), dot(abs(cell), vec2(0.8660254, 0.5)));
}

void main() {
    vec4 texel = texture(baseColorTexture, uv) * drawData.baseColorFactor;
    vec3 albedo = max(texel.rgb * material.coatingTint.rgb, vec3(0.0));
    vec3 n = normalize(normal);

    // The fixed vertex shader currently exposes no camera-space position, so
    // these directions intentionally live in the same model-space convention
    // as the original demo shader.
    vec3 viewDir = normalize(vec3(0.0, 0.10, 1.0));
    vec3 keyDir = normalize(vec3(0.40, 0.98, 0.30));
    vec3 fillDir = normalize(vec3(-0.75, 0.20, 0.60));

    float nDotKey = max(dot(n, keyDir), 0.0);
    float nDotFill = max(dot(n, fillDir), 0.0);
    float facing = clamp(dot(n, viewDir), 0.0, 1.0);
    float rim = pow(1.0 - facing, 3.0);

    vec3 halfVector = normalize(keyDir + viewDir);
    float broadSpecular = pow(max(dot(n, halfVector), 0.0), 28.0);
    float sharpSpecular = pow(max(dot(n, halfVector), 0.0), 110.0);

    // Two scales of procedural engraving break up otherwise smooth surfaces.
    float coarseDistance = hexEdge(uv * material.engravingScale);
    float coarseAA = max(fwidth(coarseDistance), 0.0015);
    float coarseLines = smoothstep(0.405 - coarseAA, 0.405 + coarseAA,
                                   coarseDistance);

    vec2 detailUv = uv * (material.engravingScale * 3.6153846);
    float microWave = 0.5 + 0.5 * sin(detailUv.x + 1.8 * sin(detailUv.y * 0.73));
    float microAA = max(fwidth(microWave), 0.01);
    float microEtch = smoothstep(0.48 - microAA, 0.48 + microAA, microWave);
    float engraving = clamp(coarseLines * (0.55 + 0.45 * microEtch), 0.0, 1.0);

    // View angle, surface orientation and UV-space interference all contribute
    // to the rainbow, so it reads as a coating rather than a flat decal.
    float interference = 2.3 * (1.0 - facing)
                       + dot(n, vec3(0.57, -0.31, 0.76))
                       + 0.13 * sin((uv.x - uv.y) * 34.0);
    vec3 iridescence = spectralPalette(interference);

    vec3 coolAmbient = vec3(0.055, 0.075, 0.12);
    vec3 warmKey = vec3(1.00, 0.82, 0.62) * (0.20 + 0.87 * nDotKey);
    vec3 coolFill = vec3(0.22, 0.42, 0.70) * (0.18 * nDotFill);

    // Preserve the authored base color, then selectively reveal a dark metal
    // underlayer along the engraved pattern.
    vec3 metalUndercoat = mix(vec3(0.015, 0.025, 0.045),
                              iridescence * 0.24,
                              0.35 + 0.65 * rim);
    vec3 surface = mix(albedo, metalUndercoat, engraving * material.engravingStrength);
    vec3 color = surface * (coolAmbient + warmKey + coolFill);

    color += iridescence * material.iridescenceStrength *
             (0.08 * broadSpecular + 0.34 * rim * material.rimStrength);
    color += vec3(1.0, 0.93, 0.82) * sharpSpecular * (0.58 + 0.42 * engraving);
    color += iridescence * coarseLines * 0.055;

    // Soft highlight compression keeps the saturated coating from clipping.
    color = color / (vec3(1.0) + color * 0.32);
    outColor = vec4(color, texel.a * material.coatingTint.a);
}
