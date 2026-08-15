#pragma once

#include "rhi/Buffer.h"
#include "rhi/Image.h"

#include <volk.h>

namespace shaderlab::rhi {
class Device;
class Swapchain;
}

namespace shaderlab::render {

class ForwardPass final {
public:
    ForwardPass(rhi::Device& device, const rhi::Swapchain& swapchain);
    ~ForwardPass();

    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    void resize(VkExtent2D extent);
    void record(VkCommandBuffer commandBuffer, VkImage colorImage, VkImageView colorView,
                VkExtent2D extent, double timeSeconds) const;

private:
    void createGeometry();
    void createPipeline(VkFormat colorFormat);
    void createDepth(VkExtent2D extent);
    void destroyPipeline() noexcept;

    rhi::Device& device_;
    rhi::Buffer vertexBuffer_;
    rhi::Buffer indexBuffer_;
    rhi::Image depthImage_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::uint32_t indexCount_ = 0;
};

} // namespace shaderlab::render

