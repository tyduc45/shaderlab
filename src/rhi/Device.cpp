#define VMA_IMPLEMENTATION
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_STATIC_VULKAN_FUNCTIONS 0

#include "rhi/Device.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace shaderlab::rhi {
namespace {

using core::Log;
using core::LogLevel;

constexpr std::array requiredDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

void check(const VkResult result, const std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = [](const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                              const VkDebugUtilsMessageTypeFlagsEXT type,
                              const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                              void*) -> VkBool32 {
        const bool validationError = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT &&
                                     (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0U;
        const auto level = validationError ? LogLevel::Error : LogLevel::Validation;
        Log::instance().write(level, callbackData != nullptr && callbackData->pMessage != nullptr
                                        ? callbackData->pMessage
                                        : "Vulkan validation message without text");
        return VK_FALSE;
    };
    return info;
}

bool hasLayer(const char* requested) {
    std::uint32_t count = 0;
    check(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties(count)");
    std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties(data)");
    return std::ranges::any_of(layers, [requested](const auto& layer) {
        return std::strcmp(layer.layerName, requested) == 0;
    });
}

bool hasRequiredExtensions(const VkPhysicalDevice physicalDevice) {
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> available(count);
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data()) != VK_SUCCESS) {
        return false;
    }
    return std::ranges::all_of(requiredDeviceExtensions, [&available](const char* required) {
        return std::ranges::any_of(available, [required](const auto& extension) {
            return std::strcmp(extension.extensionName, required) == 0;
        });
    });
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    [[nodiscard]] bool complete() const noexcept { return graphics.has_value() && present.has_value(); }
};

QueueFamilies findQueueFamilies(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());

    QueueFamilies result;
    for (std::uint32_t index = 0; index < count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            result.graphics = index;
        }
        VkBool32 presentSupported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, surface, &presentSupported) == VK_SUCCESS &&
            presentSupported == VK_TRUE) {
            result.present = index;
        }
        if (result.complete()) {
            break;
        }
    }
    return result;
}

bool supportsRequiredFeatures(const VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceVulkan14Features features14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features14;
    features14.pNext = &features13;
    features13.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    return features14.pushDescriptor == VK_TRUE &&
           features13.dynamicRendering == VK_TRUE &&
           features13.synchronization2 == VK_TRUE &&
           features12.timelineSemaphore == VK_TRUE;
}

bool hasSwapchainSupport(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface) {
    std::uint32_t formatCount = 0;
    std::uint32_t presentModeCount = 0;
    const auto formatsResult = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    const auto modesResult = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    return formatsResult == VK_SUCCESS && modesResult == VK_SUCCESS && formatCount > 0 && presentModeCount > 0;
}

} // namespace

Device::Device(GLFWwindow* window) {
    try {
        check(volkInitialize(), "volkInitialize");
        createInstance();
        volkLoadInstance(instance_);
        createDebugMessenger();
        createSurface(window);
        selectPhysicalDevice();
        createLogicalDevice();
        volkLoadDevice(device_);
        createTimelineSemaphore();
        createAllocator();
    } catch (...) {
        destroy();
        throw;
    }
}

Device::~Device() {
    destroy();
}

void Device::createInstance() {
    std::uint32_t supportedVersion = VK_API_VERSION_1_0;
    check(vkEnumerateInstanceVersion(&supportedVersion), "vkEnumerateInstanceVersion");
    if (supportedVersion < VK_API_VERSION_1_4) {
        throw std::runtime_error("Vulkan 1.4 loader is required");
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "ShaderLab";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.pEngineName = "ShaderLab";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    std::vector<const char*> layers;
    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
#if SHADERLAB_ENABLE_VALIDATION
    constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    if (!hasLayer(validationLayer)) {
        throw std::runtime_error("Debug build requires VK_LAYER_KHRONOS_validation");
    }
    layers.push_back(validationLayer);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    debugInfo = debugMessengerCreateInfo();
#endif

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pNext = layers.empty() ? nullptr : &debugInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

void Device::createDebugMessenger() {
#if SHADERLAB_ENABLE_VALIDATION
    const auto createInfo = debugMessengerCreateInfo();
    check(vkCreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_),
          "vkCreateDebugUtilsMessengerEXT");
#endif
}

void Device::createSurface(GLFWwindow* window) {
    if (window == nullptr) {
        throw std::invalid_argument("Device requires a valid GLFW window");
    }
    check(glfwCreateWindowSurface(instance_, window, nullptr, &surface_), "glfwCreateWindowSurface");
}

void Device::selectPhysicalDevice() {
    std::uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (count == 0) {
        throw std::runtime_error("No Vulkan physical device found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices(data)");

    auto score = [this](const VkPhysicalDevice candidate) -> std::uint32_t {
        const auto families = findQueueFamilies(candidate, surface_);
        if (!families.complete() || !hasRequiredExtensions(candidate) ||
            !supportsRequiredFeatures(candidate) || !hasSwapchainSupport(candidate, surface_)) {
            return 0;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_4) {
            return 0;
        }
        return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2U : 1U;
    };

    const auto selected = std::ranges::max_element(devices, {}, score);
    if (selected == devices.end() || score(*selected) == 0) {
        throw std::runtime_error("No GPU satisfies ShaderLab's Vulkan 1.4 feature contract");
    }
    physicalDevice_ = *selected;
    const auto families = findQueueFamilies(physicalDevice_, surface_);
    graphicsQueueFamily_ = *families.graphics;
    presentQueueFamily_ = *families.present;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    Log::instance().write(LogLevel::Info, std::string("Selected GPU: ") + properties.deviceName);
}

void Device::createLogicalDevice() {
    std::array uniqueFamilies{graphicsQueueFamily_, presentQueueFamily_};
    std::ranges::sort(uniqueFamilies);
    const auto uniqueEnd = std::ranges::unique(uniqueFamilies).begin();
    constexpr float priority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (auto iterator = uniqueFamilies.begin(); iterator != uniqueEnd; ++iterator) {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = *iterator;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceVulkan14Features features14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
    features14.pushDescriptor = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.timelineSemaphore = VK_TRUE;
    features14.pNext = &features13;
    features13.pNext = &features12;

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features14;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(requiredDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredDeviceExtensions.data();
    check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
}

void Device::createTimelineSemaphore() {
    VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;
    VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    createInfo.pNext = &typeInfo;
    check(vkCreateSemaphore(device_, &createInfo, nullptr, &frameTimeline_), "vkCreateSemaphore(timeline)");
    setDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(frameTimeline_), "Frame timeline");
}

void Device::createAllocator() {
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo createInfo{};
    createInfo.vulkanApiVersion = VK_API_VERSION_1_4;
    createInfo.physicalDevice = physicalDevice_;
    createInfo.device = device_;
    createInfo.instance = instance_;
    createInfo.pVulkanFunctions = &functions;
    check(vmaCreateAllocator(&createInfo, &allocator_), "vmaCreateAllocator");
}

void Device::setDebugName(const VkObjectType type, const std::uint64_t handle, const std::string_view name) const {
#if SHADERLAB_ENABLE_VALIDATION
    if (device_ == VK_NULL_HANDLE || vkSetDebugUtilsObjectNameEXT == nullptr || handle == 0) {
        return;
    }
    const std::string ownedName(name);
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = ownedName.c_str();
    check(vkSetDebugUtilsObjectNameEXT(device_, &info), "vkSetDebugUtilsObjectNameEXT");
#else
    static_cast<void>(type);
    static_cast<void>(handle);
    static_cast<void>(name);
#endif
}

void Device::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

void Device::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        static_cast<void>(vkDeviceWaitIdle(device_));
    }
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
    if (frameTimeline_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, frameTimeline_, nullptr);
        frameTimeline_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace shaderlab::rhi
