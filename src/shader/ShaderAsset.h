#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace shaderlab::shader {

using ShaderId = std::uint64_t;

struct ShaderAsset {
    ShaderId id = 0;
    std::string name;
    std::filesystem::path fragmentPath;
    bool dirty = true;
};

} // namespace shaderlab::shader

