#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <string_view>

namespace shaderlab::rhi {

class Device;

class Buffer final {
public:
    Buffer() = default;
    Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
           VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags,
           std::string_view debugName);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
    [[nodiscard]] void* mappedData() const noexcept { return mappedData_; }

    void write(const void* data, std::size_t bytes, std::size_t offset = 0) const;

private:
    void destroy() noexcept;

    Device* device_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mappedData_ = nullptr;
};

} // namespace shaderlab::rhi

