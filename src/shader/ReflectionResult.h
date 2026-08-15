#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace shaderlab::shader {

enum class MaterialValueType : std::uint8_t {
    Float,
    Int,
    UInt,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Mat3,
    Mat4,
    Unsupported,
};

enum class DescriptorKind : std::uint8_t {
    UniformBuffer,
    CombinedImageSampler,
    SampledImage,
    Sampler,
    Unsupported,
};

struct UniformMember {
    std::string name;
    MaterialValueType type = MaterialValueType::Unsupported;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct DescriptorBinding {
    std::string name;
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t count = 1;
    DescriptorKind kind = DescriptorKind::Unsupported;
    std::uint32_t blockSize = 0;
    std::vector<UniformMember> members;
};

struct ReflectionResult {
    std::vector<DescriptorBinding> bindings;
    std::uint64_t layoutHash = 0;

    [[nodiscard]] const DescriptorBinding* materialBuffer() const noexcept;
    [[nodiscard]] std::vector<const DescriptorBinding*> materialTextures() const;
};

[[nodiscard]] ReflectionResult reflectSpirv(std::span<const std::uint32_t> spirv);
[[nodiscard]] const char* materialValueTypeName(MaterialValueType type) noexcept;

} // namespace shaderlab::shader
