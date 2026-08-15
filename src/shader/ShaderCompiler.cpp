#include "shader/ShaderCompiler.h"

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <regex>
#include <sstream>
#include <string>

namespace shaderlab::shader {

CompileResult ShaderCompiler::compileFragment(const CompileRequest& request) const {
    CompileResult result;
    result.generation = request.generation;
    result.pass = request.pass;

    thread_local shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetWarningsAsErrors();
    if (request.optimize) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    } else {
        options.SetGenerateDebugInfo();
        options.SetOptimizationLevel(shaderc_optimization_level_zero);
    }

    const std::string sourceName = request.sourcePath.empty() ? "<memory>" : request.sourcePath.generic_string();
    const auto compilation = compiler.CompileGlslToSpv(request.source, shaderc_fragment_shader,
                                                       sourceName.c_str(), "main", options);
    result.diagnostics = compilation.GetErrorMessage();
    if (compilation.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.errors = parseDiagnostics(request.sourcePath, result.diagnostics);
        if (result.errors.empty()) {
            result.errors.push_back(CompileError{request.sourcePath, 0, 0,
                                                 result.diagnostics.empty() ? "Unknown shaderc failure"
                                                                            : result.diagnostics});
        }
        return result;
    }
    result.spirv.assign(compilation.cbegin(), compilation.cend());
    result.success = !result.spirv.empty();
    return result;
}

std::vector<CompileError> ShaderCompiler::parseDiagnostics(const std::filesystem::path& fallbackPath,
                                                           const std::string& diagnostics) {
    static const std::regex pattern(R"(^(.+?):(\d+)(?::(\d+))?:\s*(?:error|warning):\s*(.*)$)",
                                    std::regex::icase);
    std::vector<CompileError> errors;
    std::istringstream lines(diagnostics);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (!std::regex_match(line, match, pattern)) {
            continue;
        }
        CompileError error;
        error.path = match[1].str().empty() ? fallbackPath : std::filesystem::path(match[1].str());
        error.line = static_cast<std::uint32_t>(std::stoul(match[2].str()));
        error.column = match[3].matched ? static_cast<std::uint32_t>(std::stoul(match[3].str())) : 0;
        error.message = match[4].str();
        errors.push_back(std::move(error));
    }
    return errors;
}

} // namespace shaderlab::shader
