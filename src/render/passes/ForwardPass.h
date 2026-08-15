#pragma once

#include "rhi/Buffer.h"
#include "rhi/Image.h"
#include "scene/ModelAsset.h"

#include <volk.h>

#include <filesystem>
#include <glm/mat4x4.hpp>

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
    void createPipeline(VkFormat colorFormat);
    void createDepth(VkExtent2D extent);
    void destroyPipeline() noexcept;

    rhi::Device& device_;
    scene::ModelAsset model_;
    rhi::Buffer vertexBuffer_;
    rhi::Buffer indexBuffer_;
    rhi::Image depthImage_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace shaderlab::render
