#include "material/GpuState.h"

#include <stdexcept>
#include <utility>

namespace shaderlab::material {

MaterialResources::MaterialResources(const VkDevice device, const VkDescriptorSetLayout layout,
                                     const VkDescriptorPool pool, std::vector<VkDescriptorSet> sets,
                                     rhi::Buffer buffer, const std::uint64_t layoutHash) noexcept
    : device_(device), layout_(layout), pool_(pool), sets_(std::move(sets)),
      buffer_(std::move(buffer)), layoutHash_(layoutHash) {}

MaterialResources::~MaterialResources() {
    if (device_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    }
}

bool MaterialResources::valid() const noexcept {
    return device_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE &&
           !sets_.empty();
}

VkDescriptorSet MaterialResources::set(const std::size_t slot) const {
    if (slot >= sets_.size()) {
        throw std::out_of_range("Material descriptor slot is out of range");
    }
    return sets_[slot];
}

void MaterialResources::writeBuffer(const std::span<const std::byte> data) const {
    if (data.empty() && buffer_.handle() == VK_NULL_HANDLE) {
        return;
    }
    if (buffer_.handle() == VK_NULL_HANDLE || data.size() != buffer_.size()) {
        throw std::invalid_argument("Material UBO data does not match the live reflected buffer");
    }
    buffer_.write(data.data(), data.size());
}

GpuState::GpuState(const VkDevice device, std::shared_ptr<MaterialResources> materialResources,
                   const VkPipelineLayout pipelineLayout,
                   const VkPipeline pipeline, shader::ReflectionResult reflection,
                   const std::uint64_t generation) noexcept
    : device_(device), materialResources_(std::move(materialResources)),
      pipelineLayout_(pipelineLayout), pipeline_(pipeline), reflection_(std::move(reflection)),
      generation_(generation) {}

GpuState::~GpuState() {
    destroy();
}

bool GpuState::valid() const noexcept {
    return device_ != VK_NULL_HANDLE && materialResources_ && materialResources_->valid() &&
           pipelineLayout_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE;
}

VkDescriptorSet GpuState::materialSet(const std::size_t slot) const {
    return materialResources_->set(slot);
}

void GpuState::writeMaterialBuffer(const std::span<const std::byte> data) const {
    materialResources_->writeBuffer(data);
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
    materialResources_.reset();
    device_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
}

} // namespace shaderlab::material
