#include "editor/EditorUi.h"

#include "core/Log.h"
#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace shaderlab::editor {
namespace {

constexpr std::uint32_t imguiDescriptorCapacity = 64;

ImVec4 logColor(const core::LogLevel level) {
    switch (level) {
    case core::LogLevel::Warning:
        return {1.0F, 0.78F, 0.30F, 1.0F};
    case core::LogLevel::Error:
        return {1.0F, 0.38F, 0.38F, 1.0F};
    case core::LogLevel::Validation:
        return {0.55F, 0.75F, 1.0F, 1.0F};
    case core::LogLevel::Info:
    default:
        return {0.82F, 0.84F, 0.88F, 1.0F};
    }
}

void reportVulkanResult(const VkResult result) {
    if (result == VK_SUCCESS) {
        return;
    }
    core::Log::instance().write(core::LogLevel::Error,
                                "ImGui Vulkan backend reported VkResult " + std::to_string(result));
}

} // namespace

EditorUi::EditorUi(rhi::Device& device, const rhi::Swapchain& swapchain, GLFWwindow* window) {
    if (window == nullptr) {
        throw std::invalid_argument("EditorUi requires a valid GLFW window");
    }
    if (swapchain.imageCount() < 2 ||
        swapchain.imageCount() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("ImGui Vulkan backend requires 2..UINT32_MAX swapchain images");
    }

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    try {
        ImGui::SetCurrentContext(context_);
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            throw std::runtime_error("ImGui GLFW backend initialization failed");
        }
        glfwInitialized_ = true;

        const auto imageCount = static_cast<std::uint32_t>(swapchain.imageCount());
        const VkFormat colorFormat = swapchain.format();
        VkPipelineRenderingCreateInfo pipelineRendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        pipelineRendering.colorAttachmentCount = 1;
        pipelineRendering.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_4;
        initInfo.Instance = device.instance();
        initInfo.PhysicalDevice = device.physicalDevice();
        initInfo.Device = device.logicalDevice();
        initInfo.QueueFamily = device.graphicsQueueFamily();
        initInfo.Queue = device.graphicsQueue();
        initInfo.DescriptorPoolSize = imguiDescriptorCapacity;
        initInfo.MinImageCount = imageCount;
        initInfo.ImageCount = imageCount;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRendering;
        initInfo.UseDynamicRendering = true;
        initInfo.CheckVkResultFn = reportVulkanResult;
        initInfo.MinAllocationSize = 1024U * 1024U;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            throw std::runtime_error("ImGui Vulkan backend initialization failed");
        }
        vulkanInitialized_ = true;
        core::Log::instance().write(core::LogLevel::Info,
                                    "Editor UI initialized with official ImGui Vulkan backend");
    } catch (...) {
        destroy();
        throw;
    }
}

EditorUi::~EditorUi() {
    destroy();
}

bool EditorUi::beginFrame(const bool compileInFlight, const std::uint64_t currentGeneration,
                          const std::uint64_t liveGeneration, const double lastReloadMilliseconds) {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 16.0F, viewport->WorkPos.y + 16.0F}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({360.0F, 145.0F}, ImGuiCond_Once);
    ImGui::Begin("Shader");
    const bool compileRequested = ImGui::Button("Compile (F5)");
    ImGui::SameLine();
    if (compileInFlight) {
        ImGui::TextUnformatted("Compiling...");
    } else {
        ImGui::Text("Ready (live generation %llu)", static_cast<unsigned long long>(liveGeneration));
    }
    ImGui::Text("Latest request: %llu", static_cast<unsigned long long>(currentGeneration));
    if (lastReloadMilliseconds > 0.0) {
        ImGui::Text("Last reload: %.1f ms", lastReloadMilliseconds);
    }
    ImGui::TextWrapped("Edit assets/shaders/user/default.frag, then compile.");
    ImGui::End();

    const auto messages = core::Log::instance().snapshot();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 16.0F,
                             viewport->WorkPos.y + viewport->WorkSize.y - 250.0F}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({viewport->WorkSize.x - 32.0F, 234.0F}, ImGuiCond_Once);
    ImGui::Begin("Console");
    const bool followTail = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0F;
    for (const auto& message : messages) {
        ImGui::PushStyleColor(ImGuiCol_Text, logColor(message.level));
        ImGui::TextUnformatted(message.text.c_str());
        ImGui::PopStyleColor();
    }
    if (followTail && !messages.empty()) {
        ImGui::SetScrollHereY(1.0F);
    }
    ImGui::End();

    ImGui::Render();
    return compileRequested;
}

bool EditorUi::wantsMouseCapture() const noexcept {
    if (context_ == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(context_);
    return ImGui::GetIO().WantCaptureMouse;
}

void EditorUi::record(const VkCommandBuffer commandBuffer, const VkImage colorImage,
                      const VkImageView colorView, const VkExtent2D extent) {
    ImGui::SetCurrentContext(context_);

    VkImageMemoryBarrier2 sceneToUi{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    sceneToUi.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    sceneToUi.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    sceneToUi.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    sceneToUi.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    sceneToUi.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sceneToUi.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sceneToUi.image = colorImage;
    sceneToUi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sceneToUi.subresourceRange.levelCount = 1;
    sceneToUi.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &sceneToUi;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRendering(commandBuffer);
}

void EditorUi::destroy() noexcept {
    if (context_ == nullptr) {
        return;
    }

    ImGui::SetCurrentContext(context_);
    if (vulkanInitialized_) {
        ImGui_ImplVulkan_Shutdown();
        vulkanInitialized_ = false;
    }
    if (glfwInitialized_) {
        ImGui_ImplGlfw_Shutdown();
        glfwInitialized_ = false;
    }
    ImGui::DestroyContext(context_);
    context_ = nullptr;
}

} // namespace shaderlab::editor
