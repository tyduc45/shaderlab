#pragma once

#include "rhi/Buffer.h"
#include "rhi/Image.h"
#include "scene/ModelAsset.h"

#include <volk.h>

#include <filesystem>
#include <glm/mat4x4.hpp>
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
    [[nodiscard]] const scene::Bounds& bounds() const noexcept { return model_.bounds(); }

private:
    void createGeometry();
    void createMaterials();
    void createPipeline(VkFormat colorFormat);
    void createDepth(VkExtent2D extent);
    [[nodiscard]] rhi::Image uploadTexture(const scene::ImageData& image, std::string_view debugName);
    [[nodiscard]] std::size_t materialSlot(int materialIndex) const noexcept;
    void destroyMaterials() noexcept;
    void destroyPipeline() noexcept;

    rhi::Device& device_;
    scene::ModelAsset model_;
    rhi::Buffer vertexBuffer_;
    rhi::Buffer indexBuffer_;
    rhi::Image depthImage_;
    rhi::Image fallbackTexture_;
    std::vector<rhi::Image> textures_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> materialSets_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace shaderlab::render
