#pragma once

#include <volk.h>

#include <cstdint>

namespace shaderlab::rhi {
class DeletionQueue;
}

namespace shaderlab::material {

class GpuState final {
public:
    GpuState(VkDevice device, VkPipelineLayout pipelineLayout, VkPipeline pipeline,
             std::uint64_t generation) noexcept;
    ~GpuState();

    GpuState(const GpuState&) = delete;
    GpuState& operator=(const GpuState&) = delete;
    GpuState(GpuState&&) = delete;
    GpuState& operator=(GpuState&&) = delete;

    [[nodiscard]] VkPipelineLayout pipelineLayout() const noexcept { return pipelineLayout_; }
    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool valid() const noexcept;

    void enqueueRetirement(rhi::DeletionQueue& queue, std::uint64_t currentFrame) const;
    void disarm() noexcept;

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::uint64_t generation_ = 0;
};

} // namespace shaderlab::material
