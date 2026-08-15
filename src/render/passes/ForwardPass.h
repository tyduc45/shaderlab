#pragma once

#include "rhi/Buffer.h"
#include "rhi/Image.h"
#include "scene/ModelAsset.h"

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <memory>
#include <span>
#include <vector>

namespace shaderlab::rhi {
class Device;
class Swapchain;
}

namespace shaderlab::material {
class GpuState;
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
    [[nodiscard]] std::unique_ptr<material::GpuState> buildGpuState(
        std::span<const std::uint32_t> fragmentSpirv, std::uint64_t generation) const;
    void stageGpuState(std::unique_ptr<material::GpuState> state);
    [[nodiscard]] std::uint64_t commitPendingGpuState(std::uint64_t currentFrame);
    [[nodiscard]] const scene::Bounds& bounds() const noexcept { return model_.bounds(); }

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
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> materialSets_;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    std::unique_ptr<material::GpuState> liveGpuState_;
    std::unique_ptr<material::GpuState> pendingGpuState_;
};

} // namespace shaderlab::render
