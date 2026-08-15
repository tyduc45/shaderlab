#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace shaderlab::shader {

struct ParamMetadata {
    std::string displayName;
    std::string group = "Material";
    std::string tooltip;
    std::string uiType;
    std::string defaultTexture;
    std::optional<float> rangeMin;
    std::optional<float> rangeMax;
    std::vector<float> defaultValues;
};

using ParamMetadataMap = std::unordered_map<std::string, ParamMetadata>;

[[nodiscard]] ParamMetadataMap parseParamMetadata(std::string_view source);

} // namespace shaderlab::shader
