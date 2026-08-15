#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace shaderlab::rhi {
class Device;
class Swapchain;
}

namespace shaderlab::render {

class ForwardPass;

class Renderer final {
public:
    static constexpr std::uint32_t FramesInFlight = 2;

    Renderer(rhi::Device& device, rhi::Swapchain& swapchain, GLFWwindow* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame();

private:
    struct FrameContext {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        std::uint64_t timelineValue = 0;
    };

    void createFrameContexts();
    void destroyFrameContexts() noexcept;
    void createPresentSemaphores();
    void destroyPresentSemaphores() noexcept;
    void recordFrame(VkCommandBuffer commandBuffer, std::uint32_t imageIndex) const;
    void waitForFrame(const FrameContext& frame) const;
    void recreateSwapchain();

    rhi::Device& device_;
    rhi::Swapchain& swapchain_;
    GLFWwindow* window_ = nullptr;
    std::unique_ptr<ForwardPass> forwardPass_;
    std::array<FrameContext, FramesInFlight> frames_{};
    std::vector<VkSemaphore> presentReady_;
    std::uint32_t frameIndex_ = 0;
    std::uint64_t nextTimelineValue_ = 1;
};

} // namespace shaderlab::render
