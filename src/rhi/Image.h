#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <string_view>

namespace shaderlab::rhi {

class Device;

class Image final {
public:
    Image() = default;
    Image(Device& device, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
          VkImageAspectFlags aspect, std::string_view debugName);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    [[nodiscard]] VkImage handle() const noexcept { return image_; }
    [[nodiscard]] VkImageView view() const noexcept { return view_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }

private:
    void destroy() noexcept;

    Device* device_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
};

} // namespace shaderlab::rhi

