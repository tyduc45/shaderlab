#include "material/MaterialAsset.h"

#include <algorithm>

namespace shaderlab::material {
namespace {

float component(const shader::ParamMetadata* metadata, const std::size_t index,
                const float fallback = 0.0F) {
    return metadata != nullptr && index < metadata->defaultValues.size()
               ? metadata->defaultValues[index]
               : fallback;
}

} // namespace

MaterialParameter defaultParameter(const shader::MaterialValueType type,
                                   const shader::ParamMetadata* metadata) {
    MaterialParameter parameter;
    parameter.type = type;
    switch (type) {
    case shader::MaterialValueType::Float:
        parameter.value = component(metadata, 0);
        break;
    case shader::MaterialValueType::Int:
        parameter.value = static_cast<std::int32_t>(component(metadata, 0));
        break;
    case shader::MaterialValueType::UInt:
    case shader::MaterialValueType::Bool:
        parameter.value = static_cast<std::uint32_t>(std::max(component(metadata, 0), 0.0F));
        break;
    case shader::MaterialValueType::Vec2:
        parameter.value = std::array{component(metadata, 0), component(metadata, 1)};
        break;
    case shader::MaterialValueType::Vec3:
        parameter.value = std::array{component(metadata, 0), component(metadata, 1), component(metadata, 2)};
        break;
    case shader::MaterialValueType::Vec4: {
        const bool color = metadata != nullptr && metadata->uiType == "color";
        parameter.value = std::array{component(metadata, 0, color ? 1.0F : 0.0F),
                                     component(metadata, 1, color ? 1.0F : 0.0F),
                                     component(metadata, 2, color ? 1.0F : 0.0F),
                                     component(metadata, 3, color ? 1.0F : 0.0F)};
        break;
    }
    case shader::MaterialValueType::Mat3: {
        std::array<float, 9> matrix{};
        matrix[0] = matrix[4] = matrix[8] = 1.0F;
        parameter.value = matrix;
        break;
    }
    case shader::MaterialValueType::Mat4: {
        std::array<float, 16> matrix{};
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0F;
        parameter.value = matrix;
        break;
    }
    case shader::MaterialValueType::Unsupported:
        parameter.value = 0.0F;
        break;
    }
    return parameter;
}

std::size_t MaterialAsset::reconcile(const shader::ReflectionResult& reflection,
                                     const shader::ParamMetadataMap& metadata) {
    std::size_t resetCount = 0;
    if (const auto* buffer = reflection.materialBuffer()) {
        for (const auto& member : buffer->members) {
            const auto foundMetadata = metadata.find(member.name);
            const shader::ParamMetadata* memberMetadata =
                foundMetadata == metadata.end() ? nullptr : &foundMetadata->second;
            const auto existing = parameters_.find(member.name);
            if (existing == parameters_.end() || existing->second.type != member.type) {
                if (existing != parameters_.end()) {
                    ++resetCount;
                }
                parameters_[member.name] = defaultParameter(member.type, memberMetadata);
            }
        }
    }
    for (const auto* texture : reflection.materialTextures()) {
        if (!textures_.contains(texture->name)) {
            const bool isBaseColor = texture->name == "baseColorTexture" ||
                                     texture->name == "uAlbedo" || texture->name == "albedoTexture";
            textures_[texture->name] = isBaseColor ? UseModelTexture : UseFallbackTexture;
        }
    }
    return resetCount;
}

} // namespace shaderlab::material
