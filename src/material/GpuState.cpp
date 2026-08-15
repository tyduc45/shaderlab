#include "material/GpuState.h"

#include "rhi/DeletionQueue.h"

namespace shaderlab::material {

GpuState::GpuState(const VkDevice device, const VkPipelineLayout pipelineLayout,
                   const VkPipeline pipeline, const std::uint64_t generation) noexcept
    : device_(device), pipelineLayout_(pipelineLayout), pipeline_(pipeline), generation_(generation) {}

GpuState::~GpuState() {
    destroy();
}

bool GpuState::valid() const noexcept {
    return device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE;
}

void GpuState::enqueueRetirement(rhi::DeletionQueue& queue, const std::uint64_t currentFrame) const {
    const VkDevice device = device_;
    const VkPipeline pipeline = pipeline_;
    const VkPipelineLayout pipelineLayout = pipelineLayout_;
    queue.push(currentFrame, [device, pipeline, pipelineLayout] {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        }
    });
}

void GpuState::disarm() noexcept {
    device_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
}

void GpuState::destroy() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    disarm();
}

} // namespace shaderlab::material
