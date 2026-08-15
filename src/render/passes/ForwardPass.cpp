#include "render/passes/ForwardPass.h"

#include "material/GpuState.h"
#include "rhi/Device.h"
#include "rhi/Swapchain.h"
#include "shader/ReflectionResult.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace shaderlab::render {
namespace {

struct DrawPushConstants {
    glm::mat4 viewProjection{1.0F};
    glm::vec4 baseColorFactor{1.0F};
};

static_assert(sizeof(DrawPushConstants) == 80);

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

VkShaderModule createShaderModule(rhi::Device& device, const std::span<const std::uint32_t> words,
                                  const std::string_view name) {
    if (words.empty()) {
        throw std::invalid_argument("Cannot create a shader module from empty SPIR-V");
    }
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

VkDescriptorType vkDescriptorType(const shader::DescriptorKind kind) {
    switch (kind) {
    case shader::DescriptorKind::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case shader::DescriptorKind::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    default:
        throw std::invalid_argument("M3 supports only uniform buffers and combined image samplers in set 1");
    }
}

std::vector<std::byte> packMaterialBuffer(const shader::DescriptorBinding& buffer,
                                          const material::MaterialAsset& asset) {
    std::vector<std::byte> packed(buffer.blockSize);
    for (const auto& member : buffer.members) {
        if (member.type == shader::MaterialValueType::Unsupported ||
            member.type == shader::MaterialValueType::Mat3) {
            throw std::invalid_argument("Unsupported MaterialParams member type for " + member.name);
        }
        const auto found = asset.parameters().find(member.name);
        if (found == asset.parameters().end()) {
            continue;
        }
        if (found->second.type != member.type || member.offset > packed.size() ||
            member.size > packed.size() - member.offset) {
            throw std::invalid_argument("Material parameter layout mismatch for " + member.name);
        }
        std::visit([&](const auto& value) {
            const std::size_t bytes = std::min<std::size_t>(sizeof(value), member.size);
            std::memcpy(packed.data() + member.offset, &value, bytes);
        }, found->second.value);
    }
    return packed;
}

} // namespace

ForwardPass::ForwardPass(rhi::Device& device, const rhi::Swapchain& swapchain,
                         const std::filesystem::path& modelPath)
    : device_(device), model_(modelPath.empty() ? scene::ModelAsset::makeFallbackCube()
                                                : scene::ModelAsset::load(modelPath)),
      colorFormat_(swapchain.format()) {
    try {
        createGeometry();
        createDepth(swapchain.extent());
        createMaterials();
        createInitialGpuState();
    } catch (...) {
        pendingGpuState_.reset();
        liveGpuState_.reset();
        destroyMaterials();
        throw;
    }
}

ForwardPass::~ForwardPass() {
    pendingGpuState_.reset();
    liveGpuState_.reset();
    destroyMaterials();
}

void ForwardPass::resize(const VkExtent2D extent) {
    depthImage_ = {};
    createDepth(extent);
}

void ForwardPass::record(const VkCommandBuffer commandBuffer, const VkImage colorImage,
                         const VkImageView colorView, const VkExtent2D extent,
                         const glm::mat4& viewProjection) const {
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
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, liveGpuState_->pipeline());
    const VkDeviceSize offset = 0;
    const VkBuffer vertexBuffer = vertexBuffer_.handle();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    for (const auto& submesh : model_.submeshes()) {
        const std::size_t slot = materialSlot(submesh.materialIndex);
        const glm::vec4 factor = slot < model_.materials().size()
                                     ? model_.materials()[slot].baseColorFactor
                                     : glm::vec4(1.0F);
        const DrawPushConstants push{viewProjection, factor};
        vkCmdPushConstants(commandBuffer, liveGpuState_->pipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        const VkDescriptorSet materialSet = liveGpuState_->materialSet(slot);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                liveGpuState_->pipelineLayout(), 1, 1, &materialSet, 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    }
    vkCmdEndRendering(commandBuffer);
}

void ForwardPass::transitionToPresent(const VkCommandBuffer commandBuffer, const VkImage colorImage) const {
    VkImageMemoryBarrier2 toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image = colorImage;
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void ForwardPass::createGeometry() {
    constexpr auto allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
    const auto vertexBytes = model_.vertices().size() * sizeof(scene::Vertex);
    const auto indexBytes = model_.indices().size() * sizeof(std::uint32_t);
    vertexBuffer_ = rhi::Buffer(device_, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                VMA_MEMORY_USAGE_AUTO, allocationFlags, "Model vertices");
    vertexBuffer_.write(model_.vertices().data(), vertexBytes);
    indexBuffer_ = rhi::Buffer(device_, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VMA_MEMORY_USAGE_AUTO, allocationFlags, "Model indices");
    indexBuffer_.write(model_.indices().data(), indexBytes);
}

void ForwardPass::createMaterials() {
    scene::ImageData white;
    white.name = "White fallback";
    white.width = 1;
    white.height = 1;
    white.rgba = {255, 255, 255, 255};
    fallbackTexture_ = uploadTexture(white, "White fallback texture");

    textures_.reserve(model_.images().size());
    for (std::size_t index = 0; index < model_.images().size(); ++index) {
        const auto& image = model_.images()[index];
        const std::string name = image.name.empty() ? "glTF image " + std::to_string(index) : image.name;
        textures_.push_back(uploadTexture(image, name));
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = 0.0F;
    auto result = vkCreateSampler(device_.logicalDevice(), &samplerInfo, nullptr, &sampler_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler failed with VkResult " + std::to_string(result));
    }
    device_.setDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(sampler_), "M1 material sampler");

    VkDescriptorSetLayoutCreateInfo globalLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    result = vkCreateDescriptorSetLayout(device_.logicalDevice(), &globalLayoutInfo, nullptr, &globalLayout_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout(global) failed with VkResult " +
                                 std::to_string(result));
    }
    device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                         reinterpret_cast<std::uint64_t>(globalLayout_), "M2 fixed global layout");

}

rhi::Image ForwardPass::uploadTexture(const scene::ImageData& image, const std::string_view debugName) {
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        throw std::invalid_argument("Cannot upload an empty texture");
    }
    constexpr auto allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
    rhi::Buffer staging(device_, image.rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VMA_MEMORY_USAGE_AUTO, allocationFlags, std::string(debugName) + " staging");
    staging.write(image.rgba.data(), image.rgba.size());
    rhi::Image texture(device_, {image.width, image.height}, VK_FORMAT_R8G8B8A8_SRGB,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, debugName);
    device_.immediateSubmit([&](const VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.image = texture.handle();
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {image.width, image.height, 1};
        vkCmdCopyBufferToImage(commandBuffer, staging.handle(), texture.handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier2 toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toRead.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.image = texture.handle();
        toRead.subresourceRange = toTransfer.subresourceRange;
        dependency.pImageMemoryBarriers = &toRead;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    });
    return texture;
}

std::size_t ForwardPass::materialSlot(const int materialIndex) const noexcept {
    return materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < model_.materials().size()
               ? static_cast<std::size_t>(materialIndex)
               : model_.materials().size();
}

void ForwardPass::createInitialGpuState() {
    const auto shaderDirectory = std::filesystem::path(SHADERLAB_SHADER_DIR);
    const auto spirv = readSpirv(shaderDirectory / "fixed.frag.spv");
    const auto reflection = shader::reflectSpirv(spirv);
    materialAsset_.reconcile(reflection, {});
    liveGpuState_ = buildGpuState(spirv, reflection, materialAsset_, 0);
}

std::unique_ptr<material::GpuState> ForwardPass::buildGpuState(
    const std::span<const std::uint32_t> fragmentSpirv,
    const shader::ReflectionResult& reflection,
    const material::MaterialAsset& materialAsset,
    const std::uint64_t generation) const {
    const auto shaderDirectory = std::filesystem::path(SHADERLAB_SHADER_DIR);
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    rhi::Buffer materialBuffer;
    try {
        std::vector<VkDescriptorSetLayoutBinding> materialBindings;
        materialBindings.reserve(reflection.bindings.size());
        std::uint32_t uniformBufferCount = 0;
        std::uint32_t textureCount = 0;
        for (const auto& reflected : reflection.bindings) {
            if (reflected.set != 1) {
                throw std::invalid_argument("Only reflected material resources in descriptor set 1 are supported");
            }
            if (reflected.count != 1) {
                throw std::invalid_argument("Descriptor arrays are not supported in M3");
            }
            if (reflected.kind == shader::DescriptorKind::UniformBuffer) {
                ++uniformBufferCount;
                if (uniformBufferCount > 1 || reflected.binding != 0) {
                    throw std::invalid_argument(
                        "set 1 may contain one MaterialParams UBO at binding 0");
                }
            } else if (reflected.kind == shader::DescriptorKind::CombinedImageSampler) {
                ++textureCount;
            } else {
                static_cast<void>(vkDescriptorType(reflected.kind));
            }
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = reflected.binding;
            binding.descriptorType = vkDescriptorType(reflected.kind);
            binding.descriptorCount = reflected.count;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            materialBindings.push_back(binding);
        }
        VkDescriptorSetLayoutCreateInfo materialLayoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        materialLayoutInfo.bindingCount = static_cast<std::uint32_t>(materialBindings.size());
        materialLayoutInfo.pBindings = materialBindings.data();
        auto result = vkCreateDescriptorSetLayout(device_.logicalDevice(), &materialLayoutInfo,
                                                  nullptr, &materialLayout);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorSetLayout(material) failed with VkResult " +
                                     std::to_string(result));
        }

        vertex = createShaderModule(device_, shaderDirectory / "fixed.vert.spv", "M2 fixed VS");
        fragment = createShaderModule(device_, fragmentSpirv,
                                      "M2 candidate FS generation " + std::to_string(generation));
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = sizeof(DrawPushConstants);
        const std::array descriptorLayouts{globalLayout_, materialLayout};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = static_cast<std::uint32_t>(descriptorLayouts.size());
        layoutInfo.pSetLayouts = descriptorLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        result = vkCreatePipelineLayout(device_.logicalDevice(), &layoutInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreatePipelineLayout failed with VkResult " + std::to_string(result));
        }

        const std::array stages{
            VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                            VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
        };
        const VkVertexInputBindingDescription binding{0, sizeof(scene::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array attributes{
            VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(scene::Vertex, position)},
            VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(scene::Vertex, normal)},
            VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(scene::Vertex, uv)},
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
        rendering.pColorAttachmentFormats = &colorFormat_;
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
        pipelineInfo.layout = pipelineLayout;
        result = vkCreateGraphicsPipelines(device_.logicalDevice(), VK_NULL_HANDLE, 1,
                                           &pipelineInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateGraphicsPipelines failed with VkResult " + std::to_string(result));
        }
        const std::string suffix = " generation " + std::to_string(generation);
        device_.setDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<std::uint64_t>(pipelineLayout),
                             "M3 reflected pipeline layout" + suffix);
        device_.setDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline),
                             "M3 ForwardPass pipeline" + suffix);
        device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                             reinterpret_cast<std::uint64_t>(materialLayout),
                             "M3 reflected material layout" + suffix);

        const std::uint32_t setCount = static_cast<std::uint32_t>(model_.materials().size() + 1);
        std::vector<VkDescriptorPoolSize> poolSizes;
        if (uniformBufferCount != 0) {
            poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBufferCount * setCount});
        }
        if (textureCount != 0) {
            poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount * setCount});
        }
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = setCount;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        result = vkCreateDescriptorPool(device_.logicalDevice(), &poolInfo, nullptr, &descriptorPool);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorPool(material) failed with VkResult " +
                                     std::to_string(result));
        }
        device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                             reinterpret_cast<std::uint64_t>(descriptorPool),
                             "M3 reflected material pool" + suffix);

        std::vector<VkDescriptorSetLayout> layouts(setCount, materialLayout);
        std::vector<VkDescriptorSet> materialSets(setCount);
        VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = setCount;
        allocateInfo.pSetLayouts = layouts.data();
        result = vkAllocateDescriptorSets(device_.logicalDevice(), &allocateInfo, materialSets.data());
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateDescriptorSets(material) failed with VkResult " +
                                     std::to_string(result));
        }

        const auto* reflectedBuffer = reflection.materialBuffer();
        if (reflectedBuffer != nullptr) {
            if (reflectedBuffer->blockSize == 0) {
                throw std::invalid_argument("MaterialParams has a zero-sized reflected block");
            }
            constexpr auto allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
            materialBuffer = rhi::Buffer(device_, reflectedBuffer->blockSize,
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_AUTO, allocationFlags,
                                         "M3 MaterialParams" + suffix);
            const auto packed = packMaterialBuffer(*reflectedBuffer, materialAsset);
            materialBuffer.write(packed.data(), packed.size());
        }

        const std::size_t writeCapacity = static_cast<std::size_t>(setCount) * reflection.bindings.size();
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSet> writes;
        bufferInfos.reserve(writeCapacity);
        imageInfos.reserve(writeCapacity);
        writes.reserve(writeCapacity);
        for (std::size_t slot = 0; slot < materialSets.size(); ++slot) {
            device_.setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET,
                                 reinterpret_cast<std::uint64_t>(materialSets[slot]),
                                 "M3 material set " + std::to_string(slot) + suffix);
            for (const auto& reflected : reflection.bindings) {
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = materialSets[slot];
                write.dstBinding = reflected.binding;
                write.descriptorCount = 1;
                write.descriptorType = vkDescriptorType(reflected.kind);
                if (reflected.kind == shader::DescriptorKind::UniformBuffer) {
                    bufferInfos.push_back({materialBuffer.handle(), 0, reflected.blockSize});
                    write.pBufferInfo = &bufferInfos.back();
                } else {
                    int selection = material::MaterialAsset::UseFallbackTexture;
                    if (const auto selected = materialAsset.textures().find(reflected.name);
                        selected != materialAsset.textures().end()) {
                        selection = selected->second;
                    }
                    if (selection == material::MaterialAsset::UseModelTexture &&
                        slot < model_.materials().size()) {
                        selection = model_.materials()[slot].baseColorImage;
                    }
                    VkImageView view = fallbackTexture_.view();
                    if (selection >= 0 && static_cast<std::size_t>(selection) < textures_.size()) {
                        view = textures_[static_cast<std::size_t>(selection)].view();
                    }
                    imageInfos.push_back({sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                    write.pImageInfo = &imageInfos.back();
                }
                writes.push_back(write);
            }
        }
        if (!writes.empty()) {
            vkUpdateDescriptorSets(device_.logicalDevice(), static_cast<std::uint32_t>(writes.size()),
                                   writes.data(), 0, nullptr);
        }

        auto state = std::make_unique<material::GpuState>(
            device_.logicalDevice(), materialLayout, descriptorPool, std::move(materialSets),
            std::move(materialBuffer), pipelineLayout, pipeline, reflection, generation);
        materialLayout = VK_NULL_HANDLE;
        descriptorPool = VK_NULL_HANDLE;
        pipelineLayout = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_.logicalDevice(), fragment, nullptr);
        vkDestroyShaderModule(device_.logicalDevice(), vertex, nullptr);
        return state;
    } catch (...) {
        if (fragment != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_.logicalDevice(), fragment, nullptr);
        }
        if (vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_.logicalDevice(), vertex, nullptr);
        }
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_.logicalDevice(), pipeline, nullptr);
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_.logicalDevice(), pipelineLayout, nullptr);
        }
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_.logicalDevice(), descriptorPool, nullptr);
        }
        if (materialLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_.logicalDevice(), materialLayout, nullptr);
        }
        throw;
    }
}

void ForwardPass::stageGpuState(std::unique_ptr<material::GpuState> state,
                                material::MaterialAsset materialAsset,
                                shader::ParamMetadataMap metadata) {
    if (!state || !state->valid()) {
        throw std::invalid_argument("Cannot stage an invalid GPU state");
    }
    pendingGpuState_ = std::move(state);
    pendingMaterialAsset_ = std::move(materialAsset);
    pendingMetadata_ = std::move(metadata);
}

std::uint64_t ForwardPass::commitPendingGpuState(const std::uint64_t currentFrame) {
    if (!pendingGpuState_ || !pendingMaterialAsset_ || !pendingMetadata_) {
        return 0;
    }
    const std::uint64_t generation = pendingGpuState_->generation();
    std::shared_ptr<material::GpuState> retiring(std::move(liveGpuState_));
    device_.deletionQueue().push(currentFrame, [retiring = std::move(retiring)] {});
    liveGpuState_ = std::move(pendingGpuState_);
    materialAsset_ = std::move(*pendingMaterialAsset_);
    metadata_ = std::move(*pendingMetadata_);
    pendingMaterialAsset_.reset();
    pendingMetadata_.reset();
    return generation;
}

void ForwardPass::projectMaterialAsset() {
    if (!liveGpuState_) {
        return;
    }
    device_.waitIdle();
    const auto& reflection = liveGpuState_->reflection();
    if (const auto* reflectedBuffer = reflection.materialBuffer()) {
        const auto packed = packMaterialBuffer(*reflectedBuffer, materialAsset_);
        liveGpuState_->writeMaterialBuffer(packed);
    }

    const auto textures = reflection.materialTextures();
    const auto& sets = liveGpuState_->materialSets();
    const std::size_t writeCapacity = sets.size() * textures.size();
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;
    imageInfos.reserve(writeCapacity);
    writes.reserve(writeCapacity);
    for (std::size_t slot = 0; slot < sets.size(); ++slot) {
        for (const auto* reflected : textures) {
            int selection = material::MaterialAsset::UseFallbackTexture;
            if (const auto selected = materialAsset_.textures().find(reflected->name);
                selected != materialAsset_.textures().end()) {
                selection = selected->second;
            }
            if (selection == material::MaterialAsset::UseModelTexture &&
                slot < model_.materials().size()) {
                selection = model_.materials()[slot].baseColorImage;
            }
            VkImageView view = fallbackTexture_.view();
            if (selection >= 0 && static_cast<std::size_t>(selection) < textures_.size()) {
                view = textures_[static_cast<std::size_t>(selection)].view();
            }
            imageInfos.push_back({sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = sets[slot];
            write.dstBinding = reflected->binding;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        }
    }
    if (!writes.empty()) {
        vkUpdateDescriptorSets(device_.logicalDevice(), static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void ForwardPass::createDepth(const VkExtent2D extent) {
    depthImage_ = rhi::Image(device_, extent, VK_FORMAT_D32_SFLOAT,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT, "Forward depth");
}

void ForwardPass::destroyMaterials() noexcept {
    if (globalLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_.logicalDevice(), globalLayout_, nullptr);
        globalLayout_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_.logicalDevice(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

} // namespace shaderlab::render
