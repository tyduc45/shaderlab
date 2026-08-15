#include "rhi/Image.h"

#include "rhi/Device.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace shaderlab::rhi {

Image::Image(Device& device, const VkExtent2D extent, const VkFormat format, const VkImageUsageFlags usage,
             const VkImageAspectFlags aspect, const std::string_view debugName)
    : device_(&device), format_(format), extent_(extent) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("Image extent must be non-zero");
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    const auto result = vmaCreateImage(device.allocator(), &imageInfo, &allocationInfo,
                                       &image_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateImage failed with VkResult " + std::to_string(result));
    }

    try {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        const auto viewResult = vkCreateImageView(device.logicalDevice(), &viewInfo, nullptr, &view_);
        if (viewResult != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView failed with VkResult " + std::to_string(viewResult));
        }
        device.setDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(image_), debugName);
        device.setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view_),
                            std::string(debugName) + " view");
    } catch (...) {
        destroy();
        throw;
    }
}

Image::~Image() {
    destroy();
}

Image::Image(Image&& other) noexcept {
    *this = std::move(other);
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
    }
    return *this;
}

void Image::destroy() noexcept {
    if (device_ != nullptr && view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_->logicalDevice(), view_, nullptr);
    }
    if (device_ != nullptr && image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(device_->allocator(), image_, allocation_);
    }
    device_ = nullptr;
    image_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    format_ = VK_FORMAT_UNDEFINED;
    extent_ = {};
}

} // namespace shaderlab::rhi

