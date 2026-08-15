#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "scene/ModelAsset.h"

#include "core/Log.h"

#include <tiny_gltf.h>

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

namespace shaderlab::scene {
namespace {

const unsigned char* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                                  const tinygltf::BufferView*& view) {
    if (accessor.bufferView < 0 || static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        throw std::runtime_error("Sparse or missing-bufferView glTF accessors are not supported in v1");
    }
    view = &model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view->buffer < 0 || static_cast<std::size_t>(view->buffer) >= model.buffers.size()) {
        throw std::runtime_error("glTF accessor references an invalid buffer");
    }
    const auto& buffer = model.buffers[static_cast<std::size_t>(view->buffer)];
    const std::size_t offset = view->byteOffset + accessor.byteOffset;
    if (offset >= buffer.data.size()) {
        throw std::runtime_error("glTF accessor starts outside its buffer");
    }
    return buffer.data.data() + offset;
}

template <std::size_t Components>
std::array<float, Components> readFloatElement(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                                                const std::size_t index) {
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        throw std::runtime_error("ShaderLab v1 requires float vertex attributes");
    }
    const tinygltf::BufferView* view = nullptr;
    const auto* base = accessorData(model, accessor, view);
    const int reportedStride = accessor.ByteStride(*view);
    const std::size_t stride = reportedStride > 0 ? static_cast<std::size_t>(reportedStride)
                                                   : Components * sizeof(float);
    std::array<float, Components> result{};
    std::memcpy(result.data(), base + index * stride, sizeof(result));
    return result;
}

std::uint32_t readIndex(const tinygltf::Model& model, const tinygltf::Accessor& accessor, const std::size_t index) {
    const tinygltf::BufferView* view = nullptr;
    const auto* base = accessorData(model, accessor, view);
    const int reportedStride = accessor.ByteStride(*view);
    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const std::size_t stride = reportedStride > 0 ? static_cast<std::size_t>(reportedStride) : sizeof(std::uint8_t);
        return *(base + index * stride);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const std::size_t stride = reportedStride > 0 ? static_cast<std::size_t>(reportedStride) : sizeof(std::uint16_t);
        std::uint16_t value = 0;
        std::memcpy(&value, base + index * stride, sizeof(value));
        return value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        const std::size_t stride = reportedStride > 0 ? static_cast<std::size_t>(reportedStride) : sizeof(std::uint32_t);
        std::uint32_t value = 0;
        std::memcpy(&value, base + index * stride, sizeof(value));
        return value;
    }
    default:
        throw std::runtime_error("glTF indices must be unsigned byte, short, or int");
    }
}

glm::mat4 nodeTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 result(1.0F);
        for (glm::length_t column = 0; column < 4; ++column) {
            for (glm::length_t row = 0; row < 4; ++row) {
                const auto sourceIndex = static_cast<std::size_t>(column * 4 + row);
                result[column][row] = static_cast<float>(node.matrix[sourceIndex]);
            }
        }
        return result;
    }

    glm::mat4 result(1.0F);
    if (node.translation.size() == 3) {
        result = glm::translate(result, glm::vec3(static_cast<float>(node.translation[0]),
                                                 static_cast<float>(node.translation[1]),
                                                 static_cast<float>(node.translation[2])));
    }
    if (node.rotation.size() == 4) {
        const glm::quat rotation(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                 static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        result *= glm::mat4_cast(rotation);
    }
    if (node.scale.size() == 3) {
        result = glm::scale(result, glm::vec3(static_cast<float>(node.scale[0]),
                                             static_cast<float>(node.scale[1]),
                                             static_cast<float>(node.scale[2])));
    }
    return result;
}

const tinygltf::Accessor* findAttribute(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
                                        const char* semantic) {
    const auto found = primitive.attributes.find(semantic);
    if (found == primitive.attributes.end() || found->second < 0 ||
        static_cast<std::size_t>(found->second) >= model.accessors.size()) {
        return nullptr;
    }
    return &model.accessors[static_cast<std::size_t>(found->second)];
}

} // namespace

float Bounds::radius() const noexcept {
    return glm::length(maximum - minimum) * 0.5F;
}

ModelAsset ModelAsset::load(const std::filesystem::path& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warnings;
    std::string errors;
    const bool loaded = path.extension() == ".glb"
                            ? loader.LoadBinaryFromFile(&model, &errors, &warnings, path.string())
                            : loader.LoadASCIIFromFile(&model, &errors, &warnings, path.string());
    if (!warnings.empty()) {
        core::Log::instance().write(core::LogLevel::Warning, warnings);
    }
    if (!loaded) {
        throw std::runtime_error("Failed to load glTF '" + path.string() + "': " + errors);
    }

    ModelAsset asset;
    asset.sourcePath_ = std::filesystem::weakly_canonical(path);
    std::function<void(int, const glm::mat4&)> visitNode;
    visitNode = [&](const int nodeIndex, const glm::mat4& parentTransform) {
        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= model.nodes.size()) {
            throw std::runtime_error("glTF scene references an invalid node");
        }
        const auto& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
        const glm::mat4 world = parentTransform * nodeTransform(node);
        if (node.mesh >= 0) {
            if (static_cast<std::size_t>(node.mesh) >= model.meshes.size()) {
                throw std::runtime_error("glTF node references an invalid mesh");
            }
            const auto& mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
            for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                const auto& primitive = mesh.primitives[primitiveIndex];
                if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
                    core::Log::instance().write(core::LogLevel::Warning, "Skipping non-triangle glTF primitive");
                    continue;
                }
                const auto* positions = findAttribute(model, primitive, "POSITION");
                if (positions == nullptr || positions->type != TINYGLTF_TYPE_VEC3) {
                    throw std::runtime_error("glTF triangle primitive has no vec3 POSITION accessor");
                }
                const auto* normals = findAttribute(model, primitive, "NORMAL");
                const auto* texcoords = findAttribute(model, primitive, "TEXCOORD_0");
                const auto baseVertex = static_cast<std::uint32_t>(asset.vertices_.size());
                const glm::mat3 normalTransform = glm::transpose(glm::inverse(glm::mat3(world)));
                asset.vertices_.reserve(asset.vertices_.size() + positions->count);
                for (std::size_t vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex) {
                    const auto p = readFloatElement<3>(model, *positions, vertexIndex);
                    Vertex vertex;
                    vertex.position = glm::vec3(world * glm::vec4(p[0], p[1], p[2], 1.0F));
                    if (normals != nullptr) {
                        const auto n = readFloatElement<3>(model, *normals, vertexIndex);
                        vertex.normal = glm::normalize(normalTransform * glm::vec3(n[0], n[1], n[2]));
                    }
                    if (texcoords != nullptr) {
                        const auto uv = readFloatElement<2>(model, *texcoords, vertexIndex);
                        vertex.uv = {uv[0], uv[1]};
                    }
                    asset.includeInBounds(vertex.position);
                    asset.vertices_.push_back(vertex);
                }

                Submesh submesh;
                submesh.name = mesh.name.empty() ? "Mesh " + std::to_string(node.mesh) + " primitive " +
                                                       std::to_string(primitiveIndex)
                                                 : mesh.name + " primitive " + std::to_string(primitiveIndex);
                submesh.firstIndex = static_cast<std::uint32_t>(asset.indices_.size());
                submesh.materialIndex = primitive.material;
                if (primitive.indices >= 0) {
                    if (static_cast<std::size_t>(primitive.indices) >= model.accessors.size()) {
                        throw std::runtime_error("glTF primitive references an invalid index accessor");
                    }
                    const auto& indexAccessor = model.accessors[static_cast<std::size_t>(primitive.indices)];
                    asset.indices_.reserve(asset.indices_.size() + indexAccessor.count);
                    for (std::size_t index = 0; index < indexAccessor.count; ++index) {
                        asset.indices_.push_back(baseVertex + readIndex(model, indexAccessor, index));
                    }
                    submesh.indexCount = static_cast<std::uint32_t>(indexAccessor.count);
                } else {
                    asset.indices_.reserve(asset.indices_.size() + positions->count);
                    for (std::size_t index = 0; index < positions->count; ++index) {
                        asset.indices_.push_back(baseVertex + static_cast<std::uint32_t>(index));
                    }
                    submesh.indexCount = static_cast<std::uint32_t>(positions->count);
                }
                asset.submeshes_.push_back(std::move(submesh));
            }
        }
        for (const int child : node.children) {
            visitNode(child, world);
        }
    };

    if (model.scenes.empty()) {
        throw std::runtime_error("glTF contains no scene");
    }
    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (static_cast<std::size_t>(sceneIndex) >= model.scenes.size()) {
        throw std::runtime_error("glTF default scene index is invalid");
    }
    for (const int node : model.scenes[static_cast<std::size_t>(sceneIndex)].nodes) {
        visitNode(node, glm::mat4(1.0F));
    }
    if (asset.vertices_.empty() || asset.indices_.empty()) {
        throw std::runtime_error("glTF scene contains no triangle geometry");
    }
    core::Log::instance().write(core::LogLevel::Info,
                                "Loaded glTF: " + std::to_string(asset.vertices_.size()) + " vertices, " +
                                    std::to_string(asset.indices_.size()) + " indices, " +
                                    std::to_string(asset.submeshes_.size()) + " submeshes");
    return asset;
}

ModelAsset ModelAsset::makeFallbackCube() {
    ModelAsset asset;
    constexpr std::array positions{
        glm::vec3{-1.0F, -1.0F, -1.0F}, glm::vec3{1.0F, -1.0F, -1.0F},
        glm::vec3{1.0F, 1.0F, -1.0F}, glm::vec3{-1.0F, 1.0F, -1.0F},
        glm::vec3{-1.0F, -1.0F, 1.0F}, glm::vec3{1.0F, -1.0F, 1.0F},
        glm::vec3{1.0F, 1.0F, 1.0F}, glm::vec3{-1.0F, 1.0F, 1.0F},
    };
    for (const auto position : positions) {
        asset.vertices_.push_back(Vertex{position, glm::normalize(position), {0.0F, 0.0F}});
        asset.includeInBounds(position);
    }
    constexpr std::array<std::uint32_t, 36> cubeIndices{
        0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
        2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 3, 0, 4, 3, 4, 7,
    };
    asset.indices_.assign(cubeIndices.begin(), cubeIndices.end());
    asset.submeshes_.push_back(Submesh{"Fallback cube", 0, static_cast<std::uint32_t>(cubeIndices.size()), -1});
    return asset;
}

void ModelAsset::includeInBounds(const glm::vec3& position) {
    bounds_.minimum = glm::min(bounds_.minimum, position);
    bounds_.maximum = glm::max(bounds_.maximum, position);
}

} // namespace shaderlab::scene
