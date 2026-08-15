#pragma once

#include "material/GpuState.h"
#include "material/MaterialAsset.h"
#include "rhi/Buffer.h"
#include "rhi/Image.h"
#include "scene/ModelAsset.h"
#include "shader/ParamMetadata.h"
#include "shader/ReflectionResult.h"

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace shaderlab::rhi {
class Device;
class Swapchain;
}

namespace shaderlab::render {

class ForwardPass final {
public:
    ForwardPass(rhi::Device& device, const rhi::Swapchain& swapchain, const std::filesystem::path& modelPath);
    ~ForwardPass();

    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    void resize(VkExtent2D extent);
    void record(VkCommandBuffer commandBuffer, VkImage colorImage, VkImageView colorView,
                VkExtent2D extent, const glm::mat4& viewProjection) const;
    void transitionToPresent(VkCommandBuffer commandBuffer, VkImage colorImage) const;
    [[nodiscard]] std::unique_ptr<material::GpuState> buildGpuState(
        std::span<const std::uint32_t> fragmentSpirv,
        const shader::ReflectionResult& reflection,
        const material::MaterialAsset& materialAsset,
        std::uint64_t generation) const;
    void stageGpuState(std::unique_ptr<material::GpuState> state,
                       material::MaterialAsset materialAsset,
                       shader::ParamMetadataMap metadata);
    [[nodiscard]] std::uint64_t commitPendingGpuState(std::uint64_t currentFrame);
    [[nodiscard]] const scene::Bounds& bounds() const noexcept { return model_.bounds(); }
    [[nodiscard]] const material::MaterialAsset& materialAsset() const noexcept { return materialAsset_; }
    [[nodiscard]] material::MaterialAsset& materialAsset() noexcept { return materialAsset_; }
    [[nodiscard]] const shader::ParamMetadataMap& materialMetadata() const noexcept { return metadata_; }
    [[nodiscard]] const shader::ReflectionResult& materialReflection() const noexcept {
        return liveGpuState_->reflection();
    }
    [[nodiscard]] const std::vector<scene::ImageData>& availableImages() const noexcept {
        return model_.images();
    }
    void projectMaterialAsset();

private:
    void createGeometry();
    void createMaterials();
    void createInitialGpuState();
    void createDepth(VkExtent2D extent);
    [[nodiscard]] rhi::Image uploadTexture(const scene::ImageData& image, std::string_view debugName);
    [[nodiscard]] std::size_t materialSlot(int materialIndex) const noexcept;
    void destroyMaterials() noexcept;

    rhi::Device& device_;
    scene::ModelAsset model_;
    rhi::Buffer vertexBuffer_;
    rhi::Buffer indexBuffer_;
    rhi::Image depthImage_;
    rhi::Image fallbackTexture_;
    std::vector<rhi::Image> textures_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout globalLayout_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    material::MaterialAsset materialAsset_;
    shader::ParamMetadataMap metadata_;
    std::optional<material::MaterialAsset> pendingMaterialAsset_;
    std::optional<shader::ParamMetadataMap> pendingMetadata_;
    std::unique_ptr<material::GpuState> liveGpuState_;
    std::unique_ptr<material::GpuState> pendingGpuState_;
};

} // namespace shaderlab::render
