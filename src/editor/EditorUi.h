#pragma once

#include <volk.h>

#include <cstdint>

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
                VkExtent2D extent);

private:
    void destroy() noexcept;

    ImGuiContext* context_ = nullptr;
    bool vulkanInitialized_ = false;
    bool glfwInitialized_ = false;
};

} // namespace shaderlab::editor
