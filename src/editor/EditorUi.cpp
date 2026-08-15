#include "editor/EditorUi.h"

#include "core/Log.h"
#include "rhi/Buffer.h"
#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace shaderlab::editor {
namespace {

struct UiPushConstants {
    float scale[2];
    float translate[2];
};

void check(const VkResult result, const std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot open UI shader: " + path.string());
    }
    const auto end = stream.tellg();
    if (end <= 0 || end % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        throw std::runtime_error("Invalid UI SPIR-V size: " + path.string());
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(end) / sizeof(std::uint32_t));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(words.data()), end);
    if (!stream) {
        throw std::runtime_error("Cannot read UI shader: " + path.string());
    }
    return words;
}

VkShaderModule createShaderModule(rhi::Device& device, const std::filesystem::path& path,
                                  const std::string_view debugName) {
    const auto words = readSpirv(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device.logicalDevice(), &info, nullptr, &module),
          "vkCreateShaderModule(EditorUi)");
    device.setDebugName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<std::uint64_t>(module), debugName);
    return module;
}

std::size_t bufferCapacity(const std::size_t required) {
    std::size_t capacity = 64U * 1024U;
    while (capacity < required) {
        capacity *= 2;
    }
    return capacity;
}

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

} // namespace

struct EditorUi::FrameBuffers {
    rhi::Buffer vertices;
    rhi::Buffer indices;
    std::size_t vertexCapacity = 0;
    std::size_t indexCapacity = 0;
};

EditorUi::EditorUi(rhi::Device& device, const rhi::Swapchain& swapchain, GLFWwindow* window)
    : device_(device), colorFormat_(swapchain.format()) {
    if (window == nullptr) {
        throw std::invalid_argument("EditorUi requires a valid GLFW window");
    }

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    try {
        ImGui::SetCurrentContext(context_);
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr;
        io.BackendRendererName = "ShaderLab_Vulkan";
        io.BackendRendererUserData = this;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            throw std::runtime_error("ImGui GLFW backend initialization failed");
        }
        glfwInitialized_ = true;
        createFontTexture();
        createDescriptors();
        createPipeline();
        io.Fonts->SetTexID(1);
        core::Log::instance().write(core::LogLevel::Info, "Editor UI initialized");
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
                      const VkImageView colorView, const VkExtent2D extent,
                      const std::uint32_t frameSlot) {
    ImGui::SetCurrentContext(context_);
    const ImDrawData* drawData = ImGui::GetDrawData();
    const std::size_t vertexBytes = static_cast<std::size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const std::size_t indexBytes = static_cast<std::size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    ensureFrameBuffers(frameSlot, vertexBytes, indexBytes);
    auto& buffers = *frameBuffers_.at(frameSlot);

    std::size_t vertexOffset = 0;
    std::size_t indexOffset = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        const std::size_t listVertexBytes = static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const std::size_t listIndexBytes = static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        buffers.vertices.write(list->VtxBuffer.Data, listVertexBytes, vertexOffset);
        buffers.indices.write(list->IdxBuffer.Data, listIndexBytes, indexOffset);
        vertexOffset += listVertexBytes;
        indexOffset += listIndexBytes;
    }

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

    if (drawData->TotalVtxCount > 0 && drawData->TotalIdxCount > 0) {
        bindRenderState(commandBuffer, extent, drawData, buffers);
        const ImVec2 clipOffset = drawData->DisplayPos;
        const ImVec2 clipScale = drawData->FramebufferScale;
        std::uint32_t globalVertexOffset = 0;
        std::uint32_t globalIndexOffset = 0;
        const auto& platform = ImGui::GetPlatformIO();
        for (const ImDrawList* list : drawData->CmdLists) {
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback != nullptr) {
                    if (command.UserCallback == platform.DrawCallback_ResetRenderState ||
                        command.UserCallback == platform.DrawCallback_SetSamplerLinear ||
                        command.UserCallback == platform.DrawCallback_SetSamplerNearest) {
                        bindRenderState(commandBuffer, extent, drawData, buffers);
                    } else {
                        command.UserCallback(list, &command);
                    }
                    continue;
                }

                const float clipMinX = (command.ClipRect.x - clipOffset.x) * clipScale.x;
                const float clipMinY = (command.ClipRect.y - clipOffset.y) * clipScale.y;
                const float clipMaxX = (command.ClipRect.z - clipOffset.x) * clipScale.x;
                const float clipMaxY = (command.ClipRect.w - clipOffset.y) * clipScale.y;
                const float clampedMinX = std::clamp(clipMinX, 0.0F, static_cast<float>(extent.width));
                const float clampedMinY = std::clamp(clipMinY, 0.0F, static_cast<float>(extent.height));
                const float clampedMaxX = std::clamp(clipMaxX, 0.0F, static_cast<float>(extent.width));
                const float clampedMaxY = std::clamp(clipMaxY, 0.0F, static_cast<float>(extent.height));
                if (clampedMaxX <= clampedMinX || clampedMaxY <= clampedMinY) {
                    continue;
                }
                VkRect2D scissor{};
                scissor.offset = {static_cast<std::int32_t>(clampedMinX),
                                  static_cast<std::int32_t>(clampedMinY)};
                scissor.extent = {static_cast<std::uint32_t>(clampedMaxX - clampedMinX),
                                  static_cast<std::uint32_t>(clampedMaxY - clampedMinY)};
                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                vkCmdDrawIndexed(commandBuffer, command.ElemCount, 1,
                                 globalIndexOffset + command.IdxOffset,
                                 static_cast<std::int32_t>(globalVertexOffset + command.VtxOffset), 0);
            }
            globalIndexOffset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
            globalVertexOffset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
        }
    }
    vkCmdEndRendering(commandBuffer);
}

void EditorUi::createFontTexture() {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        throw std::runtime_error("ImGui returned an empty font atlas");
    }
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    constexpr auto allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
    rhi::Buffer staging(device_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VMA_MEMORY_USAGE_AUTO, allocationFlags, "Editor font staging");
    staging.write(pixels, bytes);
    fontTexture_ = rhi::Image(device_, {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)},
                              VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, "Editor font atlas");
    device_.immediateSubmit([&](const VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.image = fontTexture_.handle();
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        vkCmdCopyBufferToImage(commandBuffer, staging.handle(), fontTexture_.handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier2 toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toRead.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.image = fontTexture_.handle();
        toRead.subresourceRange = toTransfer.subresourceRange;
        dependency.pImageMemoryBarriers = &toRead;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    });
}

void EditorUi::createDescriptors() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_.logicalDevice(), &samplerInfo, nullptr, &fontSampler_),
          "vkCreateSampler(EditorUi)");
    device_.setDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(fontSampler_),
                         "Editor font sampler");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device_.logicalDevice(), &layoutInfo, nullptr, &descriptorLayout_),
          "vkCreateDescriptorSetLayout(EditorUi)");
    device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                         reinterpret_cast<std::uint64_t>(descriptorLayout_), "Editor descriptor layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(device_.logicalDevice(), &poolInfo, nullptr, &descriptorPool_),
          "vkCreateDescriptorPool(EditorUi)");
    device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                         reinterpret_cast<std::uint64_t>(descriptorPool_), "Editor descriptor pool");

    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorLayout_;
    check(vkAllocateDescriptorSets(device_.logicalDevice(), &allocateInfo, &descriptorSet_),
          "vkAllocateDescriptorSets(EditorUi)");
    device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET,
                         reinterpret_cast<std::uint64_t>(descriptorSet_), "Editor font descriptor");
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = fontSampler_;
    imageInfo.imageView = fontTexture_.view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_.logicalDevice(), 1, &write, 0, nullptr);
}

void EditorUi::createPipeline() {
    const auto shaderDirectory = std::filesystem::path(SHADERLAB_SHADER_DIR);
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        vertex = createShaderModule(device_, shaderDirectory / "imgui.vert.spv", "Editor UI VS");
        fragment = createShaderModule(device_, shaderDirectory / "imgui.frag.spv", "Editor UI FS");

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.size = sizeof(UiPushConstants);
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        check(vkCreatePipelineLayout(device_.logicalDevice(), &layoutInfo, nullptr, &pipelineLayout_),
              "vkCreatePipelineLayout(EditorUi)");
        device_.setDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                             reinterpret_cast<std::uint64_t>(pipelineLayout_), "Editor pipeline layout");

        const std::array stages{
            VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                            VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
        };
        const VkVertexInputBindingDescription vertexBinding{0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array attributes{
            VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
            VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
            VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)},
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &colorFormat_;
        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &rendering;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = pipelineLayout_;
        check(vkCreateGraphicsPipelines(device_.logicalDevice(), VK_NULL_HANDLE, 1,
                                        &pipelineInfo, nullptr, &pipeline_),
              "vkCreateGraphicsPipelines(EditorUi)");
        device_.setDebugName(VK_OBJECT_TYPE_PIPELINE,
                             reinterpret_cast<std::uint64_t>(pipeline_), "Editor UI pipeline");
        vkDestroyShaderModule(device_.logicalDevice(), fragment, nullptr);
        vkDestroyShaderModule(device_.logicalDevice(), vertex, nullptr);
    } catch (...) {
        if (fragment != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_.logicalDevice(), fragment, nullptr);
        }
        if (vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_.logicalDevice(), vertex, nullptr);
        }
        throw;
    }
}

void EditorUi::ensureFrameBuffers(const std::uint32_t frameSlot, const std::size_t vertexBytes,
                                  const std::size_t indexBytes) {
    auto& buffers = frameBuffers_.at(frameSlot);
    if (!buffers) {
        buffers = std::make_unique<FrameBuffers>();
    }
    constexpr auto allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vertexBytes > buffers->vertexCapacity) {
        buffers->vertexCapacity = bufferCapacity(vertexBytes);
        buffers->vertices = rhi::Buffer(device_, buffers->vertexCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        VMA_MEMORY_USAGE_AUTO, allocationFlags, "Editor UI vertices");
    }
    if (indexBytes > buffers->indexCapacity) {
        buffers->indexCapacity = bufferCapacity(indexBytes);
        buffers->indices = rhi::Buffer(device_, buffers->indexCapacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                       VMA_MEMORY_USAGE_AUTO, allocationFlags, "Editor UI indices");
    }
}

void EditorUi::bindRenderState(const VkCommandBuffer commandBuffer, const VkExtent2D extent,
                               const void* opaqueDrawData, const FrameBuffers& buffers) const {
    const auto& drawData = *static_cast<const ImDrawData*>(opaqueDrawData);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);
    const VkDeviceSize offset = 0;
    const VkBuffer vertexBuffer = buffers.vertices.handle();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, buffers.indices.handle(), 0,
                         sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    const VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0F, 1.0F};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    const UiPushConstants push{
        {2.0F / drawData.DisplaySize.x, 2.0F / drawData.DisplaySize.y},
        {-1.0F - drawData.DisplayPos.x * (2.0F / drawData.DisplaySize.x),
         -1.0F - drawData.DisplayPos.y * (2.0F / drawData.DisplaySize.y)},
    };
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(push), &push);
}

void EditorUi::destroy() noexcept {
    if (context_ != nullptr) {
        ImGui::SetCurrentContext(context_);
        auto& io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
        io.BackendRendererUserData = nullptr;
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        if (glfwInitialized_) {
            ImGui_ImplGlfw_Shutdown();
            glfwInitialized_ = false;
        }
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
    for (auto& buffers : frameBuffers_) {
        buffers.reset();
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_.logicalDevice(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_.logicalDevice(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.logicalDevice(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (descriptorLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_.logicalDevice(), descriptorLayout_, nullptr);
        descriptorLayout_ = VK_NULL_HANDLE;
    }
    if (fontSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_.logicalDevice(), fontSampler_, nullptr);
        fontSampler_ = VK_NULL_HANDLE;
    }
    fontTexture_ = {};
}

} // namespace shaderlab::editor
