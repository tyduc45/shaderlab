#include "rhi/Buffer.h"

#include "rhi/Device.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace shaderlab::rhi {

Buffer::Buffer(Device& device, const VkDeviceSize size, const VkBufferUsageFlags usage,
               const VmaMemoryUsage memoryUsage, const VmaAllocationCreateFlags allocationFlags,
               const std::string_view debugName)
    : device_(&device), size_(size) {
    if (size == 0) {
        throw std::invalid_argument("Buffer size must be greater than zero");
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = allocationFlags;
    VmaAllocationInfo createdAllocation{};
    const auto result = vmaCreateBuffer(device.allocator(), &bufferInfo, &allocationInfo,
                                        &buffer_, &allocation_, &createdAllocation);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateBuffer failed with VkResult " + std::to_string(result));
    }
    mappedData_ = createdAllocation.pMappedData;
    device.setDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer_), debugName);
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer&& other) noexcept {
    *this = std::move(other);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, nullptr);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, 0);
        mappedData_ = std::exchange(other.mappedData_, nullptr);
    }
    return *this;
}

void Buffer::write(const void* data, const std::size_t bytes, const std::size_t offset) const {
    if (mappedData_ == nullptr) {
        throw std::logic_error("Cannot write an unmapped buffer");
    }
    if (data == nullptr || offset > size_ || bytes > size_ - offset) {
        throw std::out_of_range("Buffer write is outside the allocation");
    }
    std::memcpy(static_cast<std::byte*>(mappedData_) + offset, data, bytes);
    const auto result = vmaFlushAllocation(device_->allocator(), allocation_, offset, bytes);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vmaFlushAllocation failed with VkResult " + std::to_string(result));
    }
}

void Buffer::destroy() noexcept {
    if (device_ != nullptr && buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(device_->allocator(), buffer_, allocation_);
    }
    device_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    size_ = 0;
    mappedData_ = nullptr;
}

} // namespace shaderlab::rhi
