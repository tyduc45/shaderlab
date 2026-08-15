#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shaderlab::shader {

enum class PassTag : std::uint8_t {
    Forward,
    Shadow,
    Id,
    Outline,
};

struct CompileError {
    std::filesystem::path path;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string message;
};

struct CompileRequest {
    std::filesystem::path sourcePath;
    std::string source;
    std::uint64_t generation = 0;
    PassTag pass = PassTag::Forward;
    bool optimize = false;
};

struct CompileResult {
    std::uint64_t generation = 0;
    PassTag pass = PassTag::Forward;
    bool success = false;
    std::vector<std::uint32_t> spirv;
    std::vector<CompileError> errors;
    std::string diagnostics;
};

class ShaderCompiler final {
public:
    [[nodiscard]] CompileResult compileFragment(const CompileRequest& request) const;

private:
    [[nodiscard]] static std::vector<CompileError> parseDiagnostics(
        const std::filesystem::path& fallbackPath, const std::string& diagnostics);
};

} // namespace shaderlab::shader

