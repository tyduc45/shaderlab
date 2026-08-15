#include "core/JobSystem.h"
#include "core/Log.h"
#include "material/MaterialAsset.h"
#include "rhi/DeletionQueue.h"
#include "shader/ShaderCompiler.h"
#include "shader/GenerationCounter.h"
#include "shader/ShaderReloadController.h"

#include <atomic>
#include <cstdlib>

int main() {
    using shaderlab::core::Log;
    using shaderlab::core::LogLevel;

    Log::instance().write(LogLevel::Info, "smoke test");
    const auto messages = Log::instance().snapshot();
    if (messages.empty() || messages.front().text != "smoke test") {
        return EXIT_FAILURE;
    }

    shaderlab::rhi::DeletionQueue deletions(3);
    int destroyed = 0;
    deletions.push(5, [&destroyed] { ++destroyed; });
    deletions.flush(7);
    if (destroyed != 0 || deletions.pendingCount() != 1) {
        return EXIT_FAILURE;
    }
    deletions.flush(8);
    if (destroyed != 1 || deletions.pendingCount() != 0) {
        return EXIT_FAILURE;
    }

    shaderlab::core::JobSystem jobs(2);
    shaderlab::core::ResultQueue<int> results;
    std::atomic<int> executed = 0;
    for (int value = 1; value <= 64; ++value) {
        jobs.submit([value, &results, &executed] {
            results.push(value);
            executed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    jobs.waitIdle();
    int sum = 0;
    while (auto result = results.tryPop()) {
        sum += *result;
    }
    if (executed.load(std::memory_order_relaxed) != 64 || sum != 2080) {
        return EXIT_FAILURE;
    }

    shaderlab::shader::ShaderCompiler compiler;
    const shaderlab::shader::CompileRequest validRequest{
        "valid.frag",
        "#version 460\nlayout(location=0) out vec4 color; void main(){ color=vec4(1.0); }",
        41,
    };
    const auto valid = compiler.compileFragment(validRequest);
    if (!valid.success || valid.generation != 41 || valid.spirv.empty()) {
        return EXIT_FAILURE;
    }
    const shaderlab::shader::CompileRequest reflectedRequest{
        "reflected.frag",
        R"(#version 460
layout(location=0) out vec4 color;
// @param name="Tint" type=color default=(0.2,0.4,0.8,1.0) group="Surface"
layout(set=1,binding=0) uniform MaterialParams {
    vec4 tint;
    float roughness;
} material;
// @param name="Albedo" type=texture default=white group="Textures"
layout(set=1,binding=1) uniform sampler2D albedoTexture;
void main(){ color=texture(albedoTexture, vec2(0.5))*material.tint; })",
        43,
    };
    const auto reflected = compiler.compileFragment(reflectedRequest);
    const auto* materialBuffer = reflected.reflection.materialBuffer();
    if (!reflected.success || materialBuffer == nullptr || materialBuffer->binding != 0 ||
        materialBuffer->members.size() != 2 || materialBuffer->members[0].name != "tint" ||
        materialBuffer->members[0].offset != 0 || materialBuffer->members[1].name != "roughness" ||
        reflected.reflection.materialTextures().size() != 1 ||
        reflected.metadata.at("albedoTexture").displayName != "Albedo") {
        return EXIT_FAILURE;
    }
    shaderlab::material::MaterialAsset material;
    material.reconcile(reflected.reflection, reflected.metadata);
    if (!material.parameters().contains("tint") ||
        material.textures().at("albedoTexture") != shaderlab::material::MaterialAsset::UseModelTexture) {
        return EXIT_FAILURE;
    }
    auto* tint = std::get_if<std::array<float, 4>>(&material.parameters().at("tint").value);
    if (tint == nullptr) {
        return EXIT_FAILURE;
    }
    (*tint)[0] = 0.73F;
    material.textures().at("albedoTexture") = 3;
    material.reconcile({}, {});
    material.reconcile(reflected.reflection, reflected.metadata);
    tint = std::get_if<std::array<float, 4>>(&material.parameters().at("tint").value);
    if (tint == nullptr || (*tint)[0] != 0.73F || material.textures().at("albedoTexture") != 3) {
        return EXIT_FAILURE;
    }
    auto typeChangedReflection = reflected.reflection;
    for (auto& binding : typeChangedReflection.bindings) {
        if (binding.set == 1 && binding.kind == shaderlab::shader::DescriptorKind::UniformBuffer) {
            binding.members.front().type = shaderlab::shader::MaterialValueType::Float;
        }
    }
    auto typeChangedMetadata = reflected.metadata;
    typeChangedMetadata["tint"].defaultValues = {0.25F};
    const auto resetCount = material.reconcile(typeChangedReflection, typeChangedMetadata);
    const auto* resetTint = std::get_if<float>(&material.parameters().at("tint").value);
    if (resetCount != 1 || resetTint == nullptr || *resetTint != 0.25F) {
        return EXIT_FAILURE;
    }
    const shaderlab::shader::CompileRequest invalidRequest{
        "broken.frag",
        "#version 460\nlayout(location=0) out vec4 color; void main(){ color=vec4(; }",
        42,
    };
    const auto invalid = compiler.compileFragment(invalidRequest);
    if (invalid.success || invalid.generation != 42 || invalid.errors.empty() || invalid.errors.front().line != 2) {
        return EXIT_FAILURE;
    }
    shaderlab::shader::GenerationCounter generations;
    std::uint64_t lastGeneration = 0;
    for (int request = 0; request < 10; ++request) {
        lastGeneration = generations.begin();
    }
    if (generations.isCurrent(lastGeneration - 1) || !generations.isCurrent(lastGeneration)) {
        return EXIT_FAILURE;
    }

    shaderlab::shader::ShaderReloadController reloads(2);
    constexpr const char* goodSource =
        "#version 460\nlayout(location=0) out vec4 color; void main(){ color=vec4(1.0); }";
    constexpr const char* badSource =
        "#version 460\nlayout(location=0) out vec4 color; void main(){ color=vec4(; }";
    for (int request = 0; request < 9; ++request) {
        static_cast<void>(reloads.requestSource("stale.frag", badSource));
    }
    const auto expectedGeneration = reloads.requestSource("current.frag", goodSource);
    reloads.waitIdle();
    auto current = reloads.pollCurrent();
    if (!current || !current->success || current->generation != expectedGeneration || reloads.inFlight()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
