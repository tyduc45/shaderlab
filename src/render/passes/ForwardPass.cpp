#include "render/passes/ForwardPass.h"

#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace shaderlab::render {
namespace {

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

constexpr std::array vertices{
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.9F, 0.2F, 0.2F}}, Vertex{{1.0F, -1.0F, -1.0F}, {0.2F, 0.9F, 0.2F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.2F, 0.2F, 0.9F}}, Vertex{{-1.0F, 1.0F, -1.0F}, {0.9F, 0.9F, 0.2F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.9F, 0.2F, 0.9F}}, Vertex{{1.0F, -1.0F, 1.0F}, {0.2F, 0.9F, 0.9F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.95F, 0.55F, 0.15F}}, Vertex{{-1.0F, 1.0F, 1.0F}, {0.8F, 0.8F, 0.9F}},
};

constexpr std::array<std::uint16_t, 36> indices{
    0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
    2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 3, 0, 4, 3, 4, 7,
};

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot open shader: " + path.string());
    }
    const auto end = stream.tellg();
    if (end <= 0 || end % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        throw std::runtime_error("Invalid SPIR-V size: " + path.string());
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(end) / sizeof(std::uint32_t));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(words.data()), end);
    if (!stream) {
        throw std::runtime_error("Cannot read shader: " + path.string());
    }
    return words;
}

VkShaderModule createShaderModule(rhi::Device& device, const std::filesystem::path& path, const char* name) {
    const auto words = readSpirv(path);
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = words.size() * sizeof(std::uint32_t);
    createInfo.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    const auto result = vkCreateShaderModule(device.logicalDevice(), &createInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateShaderModule failed with VkResult " + std::to_string(result));
    }
    device.setDebugName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<std::uint64_t>(module), name);
    return module;
}

} // namespace

ForwardPass::ForwardPass(rhi::Device& device, const rhi::Swapchain& swapchain) : device_(device) {
    createGeometry();
    createDepth(swapchain.extent());
    createPipeline(swapchain.format());
}

ForwardPass::~ForwardPass() {
    destroyPipeline();
}

void ForwardPass::resize(const VkExtent2D extent) {
    depthImage_ = {};
    createDepth(extent);
}

void ForwardPass::record(const VkCommandBuffer commandBuffer, const VkImage colorImage,
                         const VkImageView colorView, const VkExtent2D extent, const double timeSeconds) const {
    std::array<VkImageMemoryBarrier2, 2> barriers{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].image = colorImage;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[1].image = depthImage_.handle();
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.025F, 0.035F, 0.075F, 1.0F}};
    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depthImage_.view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    const VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkDeviceSize offset = 0;
    const VkBuffer vertexBuffer = vertexBuffer_.handle();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT16);

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const glm::mat4 projection = glm::perspective(glm::radians(55.0F), aspect, 0.1F, 100.0F);
    const glm::mat4 view = glm::lookAt(glm::vec3(3.3F, 2.4F, 4.0F), glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 model = glm::rotate(glm::mat4(1.0F), static_cast<float>(timeSeconds) * 0.55F,
                                       glm::normalize(glm::vec3(0.25F, 1.0F, 0.1F)));
    const glm::mat4 mvp = projection * view * model;
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image = colorImage;
    toPresent.subresourceRange = barriers[0].subresourceRange;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void ForwardPass::createGeometry() {
    constexpr auto allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
    vertexBuffer_ = rhi::Buffer(device_, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                VMA_MEMORY_USAGE_AUTO, allocationFlags, "M1 cube vertices");
    vertexBuffer_.write(vertices.data(), sizeof(vertices));
    indexBuffer_ = rhi::Buffer(device_, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VMA_MEMORY_USAGE_AUTO, allocationFlags, "M1 cube indices");
    indexBuffer_.write(indices.data(), sizeof(indices));
    indexCount_ = static_cast<std::uint32_t>(indices.size());
}

void ForwardPass::createPipeline(const VkFormat colorFormat) {
    const auto shaderDirectory = std::filesystem::path(SHADERLAB_SHADER_DIR);
    const VkShaderModule vertex = createShaderModule(device_, shaderDirectory / "fixed.vert.spv", "M1 fixed VS");
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        fragment = createShaderModule(device_, shaderDirectory / "fixed.frag.spv", "M1 fixed FS");
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.size = sizeof(glm::mat4);
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        auto result = vkCreatePipelineLayout(device_.logicalDevice(), &layoutInfo, nullptr, &pipelineLayout_);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreatePipelineLayout failed with VkResult " + std::to_string(result));
        }

        const std::array stages{
            VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                            VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
        };
        const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array attributes{
            VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &colorFormat;
        rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &rendering;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depth;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = pipelineLayout_;
        result = vkCreateGraphicsPipelines(device_.logicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateGraphicsPipelines failed with VkResult " + std::to_string(result));
        }
        device_.setDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<std::uint64_t>(pipelineLayout_),
                             "M1 fixed pipeline layout");
        device_.setDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline_),
                             "M1 fixed ForwardPass pipeline");
    } catch (...) {
        if (fragment != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_.logicalDevice(), fragment, nullptr);
        }
        vkDestroyShaderModule(device_.logicalDevice(), vertex, nullptr);
        destroyPipeline();
        throw;
    }
    vkDestroyShaderModule(device_.logicalDevice(), fragment, nullptr);
    vkDestroyShaderModule(device_.logicalDevice(), vertex, nullptr);
}

void ForwardPass::createDepth(const VkExtent2D extent) {
    depthImage_ = rhi::Image(device_, extent, VK_FORMAT_D32_SFLOAT,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT, "Forward depth");
}

void ForwardPass::destroyPipeline() noexcept {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_.logicalDevice(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_.logicalDevice(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
}

} // namespace shaderlab::render
