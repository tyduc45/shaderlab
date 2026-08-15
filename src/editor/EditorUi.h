#pragma once

#include "rhi/Image.h"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

struct GLFWwindow;
struct ImGuiContext;

namespace shaderlab::rhi {
class Device;
class Swapchain;
}

namespace shaderlab::editor {

class EditorUi final {
public:
    EditorUi(rhi::Device& device, const rhi::Swapchain& swapchain, GLFWwindow* window);
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    [[nodiscard]] bool beginFrame(bool compileInFlight, std::uint64_t currentGeneration,
                                  std::uint64_t liveGeneration, double lastReloadMilliseconds);
    [[nodiscard]] bool wantsMouseCapture() const noexcept;
    void record(VkCommandBuffer commandBuffer, VkImage colorImage, VkImageView colorView,
                VkExtent2D extent, std::uint32_t frameSlot);

private:
    struct FrameBuffers;

    void createFontTexture();
    void createDescriptors();
    void createPipeline();
    void ensureFrameBuffers(std::uint32_t frameSlot, std::size_t vertexBytes, std::size_t indexBytes);
    void bindRenderState(VkCommandBuffer commandBuffer, VkExtent2D extent,
                         const void* drawData, const FrameBuffers& buffers) const;
    void destroy() noexcept;

    rhi::Device& device_;
    ImGuiContext* context_ = nullptr;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    rhi::Image fontTexture_;
    VkSampler fontSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::array<std::unique_ptr<FrameBuffers>, 2> frameBuffers_;
    bool glfwInitialized_ = false;
};

} // namespace shaderlab::editor
