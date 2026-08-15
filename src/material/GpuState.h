#pragma once

#include "rhi/Buffer.h"
#include "shader/ReflectionResult.h"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace shaderlab::material {

class MaterialResources final {
public:
    MaterialResources(VkDevice device, VkDescriptorSetLayout layout,
                      VkDescriptorPool pool, std::vector<VkDescriptorSet> sets,
                      rhi::Buffer buffer, std::uint64_t layoutHash) noexcept;
    ~MaterialResources();

    MaterialResources(const MaterialResources&) = delete;
    MaterialResources& operator=(const MaterialResources&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_; }
    [[nodiscard]] VkDescriptorSet set(std::size_t slot) const;
    [[nodiscard]] const std::vector<VkDescriptorSet>& sets() const noexcept { return sets_; }
    [[nodiscard]] std::uint64_t layoutHash() const noexcept { return layoutHash_; }
    void writeBuffer(std::span<const std::byte> data) const;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    rhi::Buffer buffer_;
    std::uint64_t layoutHash_ = 0;
};

class GpuState final {
public:
    GpuState(VkDevice device, std::shared_ptr<MaterialResources> materialResources,
             VkPipelineLayout pipelineLayout,
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
        return materialResources_->sets();
    }
    void writeMaterialBuffer(std::span<const std::byte> data) const;
    [[nodiscard]] const std::shared_ptr<MaterialResources>& materialResources() const noexcept {
        return materialResources_;
    }
    [[nodiscard]] const shader::ReflectionResult& reflection() const noexcept { return reflection_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool valid() const noexcept;

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::shared_ptr<MaterialResources> materialResources_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    shader::ReflectionResult reflection_;
    std::uint64_t generation_ = 0;
};

} // namespace shaderlab::material
