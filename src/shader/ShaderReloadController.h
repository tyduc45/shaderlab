#pragma once

#include "core/JobSystem.h"
#include "shader/GenerationCounter.h"
#include "shader/ShaderCompiler.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace shaderlab::shader {

class ShaderReloadController final {
public:
    explicit ShaderReloadController(std::size_t workerCount = core::JobSystem::defaultWorkerCount());

    ShaderReloadController(const ShaderReloadController&) = delete;
    ShaderReloadController& operator=(const ShaderReloadController&) = delete;

    [[nodiscard]] std::uint64_t requestFile(const std::filesystem::path& path, bool optimize = false);
    [[nodiscard]] std::uint64_t requestSource(std::filesystem::path sourcePath, std::string source,
                                              bool optimize = false);
    [[nodiscard]] std::optional<CompileResult> pollCurrent();
    [[nodiscard]] bool inFlight() const noexcept { return inFlight_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generations_.current(); }

    void waitIdle();

private:
    core::JobSystem jobs_;
    core::ResultQueue<CompileResult> results_;
    GenerationCounter generations_;
    bool inFlight_ = false;
};

} // namespace shaderlab::shader

