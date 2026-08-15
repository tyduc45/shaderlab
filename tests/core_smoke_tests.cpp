#include "core/JobSystem.h"
#include "core/Log.h"
#include "rhi/DeletionQueue.h"
#include "shader/ShaderCompiler.h"
#include "shader/GenerationCounter.h"

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
    return EXIT_SUCCESS;
}
