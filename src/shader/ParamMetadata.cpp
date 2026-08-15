#include "shader/ParamMetadata.h"

#include <regex>
#include <sstream>
#include <string>

namespace shaderlab::shader {
namespace {

std::optional<std::string> quotedValue(const std::string& text, const char* key) {
    const std::regex pattern(std::string("(?:^|\\s)") + key + R"regex(\s*=\s*"([^"]*)")regex");
    std::smatch match;
    return std::regex_search(text, match, pattern) ? std::optional<std::string>(match[1].str())
                                                   : std::nullopt;
}

std::optional<std::string> bareValue(const std::string& text, const char* key) {
    const std::regex pattern(std::string("(?:^|\\s)") + key + R"(\s*=\s*([^\s]+))");
    std::smatch match;
    return std::regex_search(text, match, pattern) ? std::optional<std::string>(match[1].str())
                                                   : std::nullopt;
}

std::vector<float> parseNumbers(const std::string& value) {
    static const std::regex number(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)");
    std::vector<float> values;
    for (std::sregex_iterator it(value.begin(), value.end(), number), end; it != end; ++it) {
        values.push_back(std::stof((*it)[0].str()));
    }
    return values;
}

std::optional<std::string> declarationName(const std::string& line) {
    static const std::regex sampler(R"(\buniform\s+sampler(?:1D|2D|3D|Cube|2DArray)\s+(\w+)\s*;)");
    static const std::regex member(R"(^\s*(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])\s+(\w+)\s*;)");
    std::smatch match;
    if (std::regex_search(line, match, sampler) || std::regex_search(line, match, member)) {
        return match[1].str();
    }
    return std::nullopt;
}

} // namespace

ParamMetadataMap parseParamMetadata(const std::string_view source) {
    ParamMetadataMap result;
    std::istringstream lines{std::string(source)};
    std::optional<ParamMetadata> pending;
    std::string line;
    while (std::getline(lines, line)) {
        const auto marker = line.find("@param");
        if (marker != std::string::npos) {
            const std::string text = line.substr(marker + 6);
            ParamMetadata metadata;
            if (const auto name = quotedValue(text, "name")) metadata.displayName = *name;
            if (const auto group = quotedValue(text, "group")) metadata.group = *group;
            if (const auto tooltip = quotedValue(text, "tooltip")) metadata.tooltip = *tooltip;
            if (const auto type = bareValue(text, "type")) metadata.uiType = *type;
            if (const auto defaultValue = bareValue(text, "default")) {
                metadata.defaultValues = parseNumbers(*defaultValue);
                if (metadata.defaultValues.empty()) metadata.defaultTexture = *defaultValue;
            }
            static const std::regex rangePattern(R"((?:^|\s)range\s*=\s*\[([^,]+),([^\]]+)\])");
            std::smatch range;
            if (std::regex_search(text, range, rangePattern)) {
                metadata.rangeMin = std::stof(range[1].str());
                metadata.rangeMax = std::stof(range[2].str());
            }
            pending = std::move(metadata);
            continue;
        }
        if (pending) {
            if (const auto name = declarationName(line)) {
                if (pending->displayName.empty()) pending->displayName = *name;
                result[*name] = std::move(*pending);
                pending.reset();
            }
        }
    }
    return result;
}

} // namespace shaderlab::shader
