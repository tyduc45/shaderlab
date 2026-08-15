#pragma once

#include <volk.h>

#include <vector>

struct GLFWwindow;

namespace shaderlab::rhi {

class Device;

class Swapchain final {
public:
    Swapchain(Device& device, GLFWwindow* window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate();
    [[nodiscard]] bool framebufferExtentChanged() const;

    [[nodiscard]] VkSwapchainKHR handle() const noexcept { return swapchain_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] VkImage image(const std::uint32_t index) const { return images_.at(index); }
    [[nodiscard]] VkImageView imageView(const std::uint32_t index) const { return imageViews_.at(index); }

private:
    void create(VkSwapchainKHR oldSwapchain);
    void destroyViews() noexcept;

    Device& device_;
    GLFWwindow* window_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
};

} // namespace shaderlab::rhi

