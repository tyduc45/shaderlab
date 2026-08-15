#include "material/GpuState.h"

#include <stdexcept>
#include <utility>

namespace shaderlab::material {

GpuState::GpuState(const VkDevice device, const VkDescriptorSetLayout materialLayout,
                   const VkDescriptorPool descriptorPool, std::vector<VkDescriptorSet> materialSets,
                   rhi::Buffer materialBuffer, const VkPipelineLayout pipelineLayout,
                   const VkPipeline pipeline, shader::ReflectionResult reflection,
                   const std::uint64_t generation) noexcept
    : device_(device), materialLayout_(materialLayout), descriptorPool_(descriptorPool),
      materialSets_(std::move(materialSets)), materialBuffer_(std::move(materialBuffer)),
      pipelineLayout_(pipelineLayout), pipeline_(pipeline), reflection_(std::move(reflection)),
      generation_(generation) {}

GpuState::~GpuState() {
    destroy();
}

bool GpuState::valid() const noexcept {
    return device_ != VK_NULL_HANDLE && materialLayout_ != VK_NULL_HANDLE &&
           descriptorPool_ != VK_NULL_HANDLE && !materialSets_.empty() &&
           pipelineLayout_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE;
}

VkDescriptorSet GpuState::materialSet(const std::size_t slot) const {
    if (slot >= materialSets_.size()) {
        throw std::out_of_range("Material descriptor slot is out of range");
    }
    return materialSets_[slot];
}

void GpuState::writeMaterialBuffer(const std::span<const std::byte> data) const {
    if (data.empty() && materialBuffer_.handle() == VK_NULL_HANDLE) {
        return;
    }
    if (materialBuffer_.handle() == VK_NULL_HANDLE || data.size() != materialBuffer_.size()) {
        throw std::invalid_argument("Material UBO data does not match the live reflected buffer");
    }
    materialBuffer_.write(data.data(), data.size());
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
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    if (materialLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, materialLayout_, nullptr);
    }
    materialSets_.clear();
    device_ = VK_NULL_HANDLE;
    materialLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
}

} // namespace shaderlab::material
