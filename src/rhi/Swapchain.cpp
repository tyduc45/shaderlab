#include "rhi/Swapchain.h"

#include "rhi/Device.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace shaderlab::rhi {
namespace {

void check(const VkResult result, const std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    const auto preferred = std::ranges::find_if(formats, [](const auto& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    return preferred != formats.end() ? *preferred : formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    return std::ranges::find(modes, VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()
               ? VK_PRESENT_MODE_MAILBOX_KHR
               : VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

Swapchain::Swapchain(Device& device, GLFWwindow* window) : device_(device), window_(window) {
    if (window_ == nullptr) {
        throw std::invalid_argument("Swapchain requires a valid GLFW window");
    }
    create(VK_NULL_HANDLE);
}

Swapchain::~Swapchain() {
    destroyViews();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_.logicalDevice(), swapchain_, nullptr);
    }
}

void Swapchain::recreate() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while ((width == 0 || height == 0) && glfwWindowShouldClose(window_) == GLFW_FALSE) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &width, &height);
    }
    if (glfwWindowShouldClose(window_) == GLFW_TRUE) {
        return;
    }

    device_.waitIdle();
    const VkSwapchainKHR oldSwapchain = swapchain_;
    destroyViews();
    swapchain_ = VK_NULL_HANDLE;
    try {
        create(oldSwapchain);
    } catch (...) {
        swapchain_ = oldSwapchain;
        throw;
    }
    vkDestroySwapchainKHR(device_.logicalDevice(), oldSwapchain, nullptr);
}

bool Swapchain::framebufferExtentChanged() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return width > 0 && height > 0 &&
           (extent_.width != static_cast<std::uint32_t>(width) ||
            extent_.height != static_cast<std::uint32_t>(height));
}

void Swapchain::create(const VkSwapchainKHR oldSwapchain) {
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_.physicalDevice(), device_.surface(), &capabilities),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    std::uint32_t formatCount = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(device_.physicalDevice(), device_.surface(), &formatCount, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(device_.physicalDevice(), device_.surface(), &formatCount, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR(data)");

    std::uint32_t modeCount = 0;
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(device_.physicalDevice(), device_.surface(), &modeCount, nullptr),
          "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> modes(modeCount);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(device_.physicalDevice(), device_.surface(), &modeCount, modes.data()),
          "vkGetPhysicalDeviceSurfacePresentModesKHR(data)");

    if (formats.empty() || modes.empty()) {
        throw std::runtime_error("Surface has no usable swapchain format or present mode");
    }

    const auto surfaceFormat = chooseFormat(formats);
    format_ = surfaceFormat.format;
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extent_ = capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        extent_.width = std::clamp(static_cast<std::uint32_t>(std::max(width, 1)),
                                  capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<std::uint32_t>(std::max(height, 1)),
                                   capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    const std::array queueFamilies{device_.graphicsQueueFamily(), device_.presentQueueFamily()};
    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = device_.surface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format_;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (queueFamilies[0] != queueFamilies[1]) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilies.size());
        createInfo.pQueueFamilyIndices = queueFamilies.data();
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = choosePresentMode(modes);
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;
    check(vkCreateSwapchainKHR(device_.logicalDevice(), &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    device_.setDebugName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<std::uint64_t>(swapchain_), "Main swapchain");

    check(vkGetSwapchainImagesKHR(device_.logicalDevice(), swapchain_, &imageCount, nullptr),
          "vkGetSwapchainImagesKHR(count)");
    images_.resize(imageCount);
    check(vkGetSwapchainImagesKHR(device_.logicalDevice(), swapchain_, &imageCount, images_.data()),
          "vkGetSwapchainImagesKHR(data)");

    imageViews_.reserve(images_.size());
    for (std::size_t index = 0; index < images_.size(); ++index) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images_[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_.logicalDevice(), &viewInfo, nullptr, &view), "vkCreateImageView(swapchain)");
        imageViews_.push_back(view);
        device_.setDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(images_[index]),
                             "Swapchain image " + std::to_string(index));
        device_.setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view),
                             "Swapchain view " + std::to_string(index));
    }
}

void Swapchain::destroyViews() noexcept {
    for (const auto view : imageViews_) {
        vkDestroyImageView(device_.logicalDevice(), view, nullptr);
    }
    imageViews_.clear();
    images_.clear();
}

} // namespace shaderlab::rhi
