#pragma once

#include "shader/ParamMetadata.h"
#include "shader/ReflectionResult.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace shaderlab::material {

using MaterialValue = std::variant<
    float,
    std::int32_t,
    std::uint32_t,
    std::array<float, 2>,
    std::array<float, 3>,
    std::array<float, 4>,
    std::array<float, 9>,
    std::array<float, 16>>;

struct MaterialParameter {
    shader::MaterialValueType type = shader::MaterialValueType::Unsupported;
    MaterialValue value = 0.0F;
};

class MaterialAsset final {
public:
    static constexpr int UseModelTexture = -2;
    static constexpr int UseFallbackTexture = -1;

    void reconcile(const shader::ReflectionResult& reflection,
                   const shader::ParamMetadataMap& metadata);

    [[nodiscard]] std::map<std::string, MaterialParameter>& parameters() noexcept { return parameters_; }
    [[nodiscard]] const std::map<std::string, MaterialParameter>& parameters() const noexcept { return parameters_; }
    [[nodiscard]] std::map<std::string, int>& textures() noexcept { return textures_; }
    [[nodiscard]] const std::map<std::string, int>& textures() const noexcept { return textures_; }

private:
    std::map<std::string, MaterialParameter> parameters_;
    std::map<std::string, int> textures_;
};

[[nodiscard]] MaterialParameter defaultParameter(
    shader::MaterialValueType type, const shader::ParamMetadata* metadata);

} // namespace shaderlab::material
