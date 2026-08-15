#include "shader/ShaderReloadController.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace shaderlab::shader {

ShaderReloadController::ShaderReloadController(const std::size_t workerCount) : jobs_(workerCount) {
    constexpr const char* warmupSource =
        "#version 460\nlayout(location=0) out vec4 color; void main(){ color=vec4(1.0); }";
    for (std::size_t worker = 0; worker < jobs_.workerCount(); ++worker) {
        jobs_.submit([warmupSource] {
            ShaderCompiler compiler;
            static_cast<void>(compiler.compileFragment(CompileRequest{"<shaderc-warmup>", warmupSource, 0}));
        });
    }
    jobs_.waitIdle();
}

std::uint64_t ShaderReloadController::requestFile(const std::filesystem::path& path, const bool optimize) {
    const std::uint64_t generation = generations_.begin();
    inFlight_ = true;
    jobs_.submit([path, optimize, generation, this] {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            CompileResult failure;
            failure.generation = generation;
            failure.errors.push_back(CompileError{path, 0, 0, "Cannot open shader source"});
            failure.diagnostics = "Cannot open shader source: " + path.string();
            results_.push(std::move(failure));
            return;
        }
        std::ostringstream source;
        source << stream.rdbuf();
        ShaderCompiler compiler;
        results_.push(compiler.compileFragment(CompileRequest{path, source.str(), generation,
                                                               PassTag::Forward, optimize}));
    });
    return generation;
}

std::uint64_t ShaderReloadController::requestSource(std::filesystem::path sourcePath, std::string source,
                                                    const bool optimize) {
    const std::uint64_t generation = generations_.begin();
    inFlight_ = true;
    jobs_.submit([sourcePath = std::move(sourcePath), source = std::move(source), optimize, generation, this] {
        ShaderCompiler compiler;
        results_.push(compiler.compileFragment(CompileRequest{sourcePath, source, generation,
                                                               PassTag::Forward, optimize}));
    });
    return generation;
}

std::optional<CompileResult> ShaderReloadController::pollCurrent() {
    while (auto result = results_.tryPop()) {
        if (!generations_.isCurrent(result->generation)) {
            continue;
        }
        inFlight_ = false;
        return result;
    }
    return std::nullopt;
}

void ShaderReloadController::waitIdle() {
    jobs_.waitIdle();
}

} // namespace shaderlab::shader
