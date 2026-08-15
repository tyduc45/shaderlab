#pragma once

#include "scene/OrbitCamera.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
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

    Renderer(rhi::Device& device, rhi::Swapchain& swapchain, GLFWwindow* window,
             const std::filesystem::path& modelPath);
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
    void recordFrame(VkCommandBuffer commandBuffer, std::uint32_t imageIndex);
    void waitForFrame(const FrameContext& frame) const;
    void recreateSwapchain();
    static void cursorPositionCallback(GLFWwindow* window, double cursorX, double cursorY);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int modifiers);

    rhi::Device& device_;
    rhi::Swapchain& swapchain_;
    GLFWwindow* window_ = nullptr;
    std::unique_ptr<ForwardPass> forwardPass_;
    scene::OrbitCamera camera_;
    std::array<FrameContext, FramesInFlight> frames_{};
    std::vector<VkSemaphore> presentReady_;
    std::uint32_t frameIndex_ = 0;
    std::uint64_t nextTimelineValue_ = 1;
    std::uint64_t frameNumber_ = 0;
    double lastFrameTime_ = 0.0;
};

} // namespace shaderlab::render
