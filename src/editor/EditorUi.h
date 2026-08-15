#pragma once

#include "material/MaterialAsset.h"
#include "scene/ModelAsset.h"
#include "shader/ParamMetadata.h"
#include "shader/ReflectionResult.h"

#include <volk.h>

#include <cstdint>
#include <span>

struct GLFWwindow;
struct ImGuiContext;

namespace shaderlab::rhi {
class Device;
class Swapchain;
}

namespace shaderlab::editor {

struct EditorFrameResult {
    bool compileRequested = false;
    bool materialChanged = false;
};

class EditorUi final {
public:
    EditorUi(rhi::Device& device, const rhi::Swapchain& swapchain, GLFWwindow* window);
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    [[nodiscard]] EditorFrameResult beginFrame(
        bool compileInFlight, std::uint64_t currentGeneration,
        std::uint64_t liveGeneration, double lastReloadMilliseconds,
        material::MaterialAsset& materialAsset,
        const shader::ReflectionResult& reflection,
        const shader::ParamMetadataMap& metadata,
        std::span<const scene::ImageData> images);
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
