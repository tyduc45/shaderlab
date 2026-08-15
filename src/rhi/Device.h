#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <string_view>

struct GLFWwindow;

namespace shaderlab::rhi {

class Device final {
public:
    explicit Device(GLFWwindow* window);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkDevice logicalDevice() const noexcept { return device_; }
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }
    [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept { return graphicsQueueFamily_; }
    [[nodiscard]] std::uint32_t presentQueueFamily() const noexcept { return presentQueueFamily_; }
    [[nodiscard]] VmaAllocator allocator() const noexcept { return allocator_; }
    [[nodiscard]] VkSemaphore frameTimeline() const noexcept { return frameTimeline_; }

    void setDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const;
    void waitIdle() const;

private:
    void createInstance();
    void createDebugMessenger();
    void createSurface(GLFWwindow* window);
    void selectPhysicalDevice();
    void createLogicalDevice();
    void createTimelineSemaphore();
    void createAllocator();
    void destroy() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = 0;
    std::uint32_t presentQueueFamily_ = 0;
    VkSemaphore frameTimeline_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

} // namespace shaderlab::rhi

