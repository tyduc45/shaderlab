#pragma once

#include "rhi/Buffer.h"
#include "shader/ReflectionResult.h"

#include <volk.h>

#include <cstdint>
#include <span>
#include <vector>

namespace shaderlab::material {

class GpuState final {
public:
    GpuState(VkDevice device, VkDescriptorSetLayout materialLayout,
             VkDescriptorPool descriptorPool, std::vector<VkDescriptorSet> materialSets,
             rhi::Buffer materialBuffer, VkPipelineLayout pipelineLayout,
             VkPipeline pipeline, shader::ReflectionResult reflection,
             std::uint64_t generation) noexcept;
    ~GpuState();

    GpuState(const GpuState&) = delete;
    GpuState& operator=(const GpuState&) = delete;
    GpuState(GpuState&&) = delete;
    GpuState& operator=(GpuState&&) = delete;

    [[nodiscard]] VkPipelineLayout pipelineLayout() const noexcept { return pipelineLayout_; }
    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] VkDescriptorSet materialSet(std::size_t slot) const;
    [[nodiscard]] const std::vector<VkDescriptorSet>& materialSets() const noexcept {
        return materialSets_;
    }
    void writeMaterialBuffer(std::span<const std::byte> data) const;
    [[nodiscard]] const shader::ReflectionResult& reflection() const noexcept { return reflection_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool valid() const noexcept;

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> materialSets_;
    rhi::Buffer materialBuffer_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    shader::ReflectionResult reflection_;
    std::uint64_t generation_ = 0;
};

} // namespace shaderlab::material
