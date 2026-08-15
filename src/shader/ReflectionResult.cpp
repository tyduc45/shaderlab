#include "shader/ReflectionResult.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace shaderlab::shader {
namespace {

MaterialValueType valueType(const SpvReflectBlockVariable& member) {
    const auto* description = member.type_description;
    if (description == nullptr) {
        return MaterialValueType::Unsupported;
    }
    const auto flags = description->type_flags;
    const std::uint32_t components = description->traits.numeric.vector.component_count;
    if ((flags & SPV_REFLECT_TYPE_FLAG_MATRIX) != 0) {
        const std::uint32_t columns = description->traits.numeric.matrix.column_count;
        const std::uint32_t rows = description->traits.numeric.matrix.row_count;
        if (columns == 3 && rows == 3) {
            return MaterialValueType::Mat3;
        }
        if (columns == 4 && rows == 4) {
            return MaterialValueType::Mat4;
        }
        return MaterialValueType::Unsupported;
    }
    if ((flags & SPV_REFLECT_TYPE_FLAG_VECTOR) != 0) {
        switch (components) {
        case 2:
            return MaterialValueType::Vec2;
        case 3:
            return MaterialValueType::Vec3;
        case 4:
            return MaterialValueType::Vec4;
        default:
            return MaterialValueType::Unsupported;
        }
    }
    if ((flags & SPV_REFLECT_TYPE_FLAG_BOOL) != 0) {
        return MaterialValueType::Bool;
    }
    if ((flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0) {
        return MaterialValueType::Float;
    }
    if ((flags & SPV_REFLECT_TYPE_FLAG_INT) != 0) {
        return description->traits.numeric.scalar.signedness != 0 ? MaterialValueType::Int
                                                                  : MaterialValueType::UInt;
    }
    return MaterialValueType::Unsupported;
}

DescriptorKind descriptorKind(const SpvReflectDescriptorType type) {
    switch (type) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return DescriptorKind::UniformBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return DescriptorKind::CombinedImageSampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return DescriptorKind::SampledImage;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        return DescriptorKind::Sampler;
    default:
        return DescriptorKind::Unsupported;
    }
}

void hashBytes(std::uint64_t& hash, const void* data, const std::size_t size) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= prime;
    }
}

template <typename T>
void hashValue(std::uint64_t& hash, const T& value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashString(std::uint64_t& hash, const std::string& value) {
    hashBytes(hash, value.data(), value.size());
    const unsigned char terminator = 0;
    hashBytes(hash, &terminator, 1);
}

} // namespace

const DescriptorBinding* ReflectionResult::materialBuffer() const noexcept {
    const auto found = std::ranges::find_if(bindings, [](const DescriptorBinding& binding) {
        return binding.set == 1 && binding.kind == DescriptorKind::UniformBuffer;
    });
    return found == bindings.end() ? nullptr : &*found;
}

std::vector<const DescriptorBinding*> ReflectionResult::materialTextures() const {
    std::vector<const DescriptorBinding*> textures;
    for (const auto& binding : bindings) {
        if (binding.set == 1 && binding.kind == DescriptorKind::CombinedImageSampler) {
            textures.push_back(&binding);
        }
    }
    return textures;
}

ReflectionResult reflectSpirv(const std::span<const std::uint32_t> spirv) {
    if (spirv.empty()) {
        throw std::invalid_argument("Cannot reflect empty SPIR-V");
    }
    SpvReflectShaderModule module{};
    const auto createResult = spvReflectCreateShaderModule(
        spirv.size_bytes(), spirv.data(), &module);
    if (createResult != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("SPIRV-Reflect could not create a shader module: " +
                                 std::to_string(createResult));
    }
    struct ModuleGuard final {
        SpvReflectShaderModule* module;
        ~ModuleGuard() { spvReflectDestroyShaderModule(module); }
    } guard{&module};

    std::uint32_t count = 0;
    auto enumerateResult = spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    if (enumerateResult != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("SPIRV-Reflect could not enumerate descriptor bindings");
    }
    std::vector<SpvReflectDescriptorBinding*> reflected(count);
    enumerateResult = spvReflectEnumerateDescriptorBindings(&module, &count, reflected.data());
    if (enumerateResult != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("SPIRV-Reflect could not read descriptor bindings");
    }

    ReflectionResult result;
    result.bindings.reserve(reflected.size());
    for (const auto* source : reflected) {
        if (source == nullptr) {
            continue;
        }
        DescriptorBinding binding;
        binding.name = source->name != nullptr ? source->name : "";
        binding.set = source->set;
        binding.binding = source->binding;
        binding.count = source->count;
        binding.kind = descriptorKind(source->descriptor_type);
        binding.blockSize = source->block.padded_size;
        binding.members.reserve(source->block.member_count);
        for (std::uint32_t memberIndex = 0; memberIndex < source->block.member_count; ++memberIndex) {
            const auto& sourceMember = source->block.members[memberIndex];
            UniformMember member;
            member.name = sourceMember.name != nullptr ? sourceMember.name : "";
            member.type = valueType(sourceMember);
            member.offset = sourceMember.offset;
            member.size = sourceMember.size;
            binding.members.push_back(std::move(member));
        }
        result.bindings.push_back(std::move(binding));
    }
    std::ranges::sort(result.bindings, [](const DescriptorBinding& left, const DescriptorBinding& right) {
        return left.set != right.set ? left.set < right.set : left.binding < right.binding;
    });

    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& binding : result.bindings) {
        hashString(hash, binding.name);
        hashValue(hash, binding.set);
        hashValue(hash, binding.binding);
        hashValue(hash, binding.count);
        hashValue(hash, binding.kind);
        hashValue(hash, binding.blockSize);
        for (const auto& member : binding.members) {
            hashString(hash, member.name);
            hashValue(hash, member.type);
            hashValue(hash, member.offset);
            hashValue(hash, member.size);
        }
    }
    result.layoutHash = hash;
    return result;
}

const char* materialValueTypeName(const MaterialValueType type) noexcept {
    switch (type) {
    case MaterialValueType::Float: return "float";
    case MaterialValueType::Int: return "int";
    case MaterialValueType::UInt: return "uint";
    case MaterialValueType::Bool: return "bool";
    case MaterialValueType::Vec2: return "vec2";
    case MaterialValueType::Vec3: return "vec3";
    case MaterialValueType::Vec4: return "vec4";
    case MaterialValueType::Mat3: return "mat3";
    case MaterialValueType::Mat4: return "mat4";
    case MaterialValueType::Unsupported: return "unsupported";
    }
    return "unsupported";
}

} // namespace shaderlab::shader
