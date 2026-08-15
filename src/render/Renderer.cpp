#include "render/Renderer.h"

#include "render/passes/ForwardPass.h"

#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <GLFW/glfw3.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace shaderlab::render {
namespace {

void check(const VkResult result, const std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

} // namespace

Renderer::Renderer(rhi::Device& device, rhi::Swapchain& swapchain, GLFWwindow* window)
    : device_(device), swapchain_(swapchain), window_(window) {
    if (window_ == nullptr) {
        throw std::invalid_argument("Renderer requires a valid GLFW window");
    }
    createFrameContexts();
    createPresentSemaphores();
    forwardPass_ = std::make_unique<ForwardPass>(device_, swapchain_);
}

Renderer::~Renderer() {
    device_.waitIdle();
    forwardPass_.reset();
    destroyPresentSemaphores();
    destroyFrameContexts();
}

void Renderer::drawFrame() {
    if (swapchain_.framebufferExtentChanged()) {
        recreateSwapchain();
    }

    auto& frame = frames_[frameIndex_];
    waitForFrame(frame);

    std::uint32_t imageIndex = 0;
    const auto acquireResult = vkAcquireNextImageKHR(device_.logicalDevice(), swapchain_.handle(),
                                                      UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        check(acquireResult, "vkAcquireNextImageKHR");
    }

    check(vkResetCommandPool(device_.logicalDevice(), frame.commandPool, 0), "vkResetCommandPool");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    recordFrame(frame.commandBuffer, imageIndex);
    check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = frame.commandBuffer;

    VkSemaphoreSubmitInfo renderFinishedInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    renderFinishedInfo.semaphore = presentReady_.at(imageIndex);
    renderFinishedInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    frame.timelineValue = nextTimelineValue_++;
    VkSemaphoreSubmitInfo timelineInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    timelineInfo.semaphore = device_.frameTimeline();
    timelineInfo.value = frame.timelineValue;
    timelineInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    const std::array signalInfos{renderFinishedInfo, timelineInfo};
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();
    check(vkQueueSubmit2(device_.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit2");

    const VkSwapchainKHR swapchain = swapchain_.handle();
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &presentReady_.at(imageIndex);
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const auto presentResult = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        check(presentResult, "vkQueuePresentKHR");
    }

    frameIndex_ = (frameIndex_ + 1) % FramesInFlight;
}

void Renderer::createFrameContexts() {
    for (std::uint32_t index = 0; index < FramesInFlight; ++index) {
        auto& frame = frames_[index];
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device_.graphicsQueueFamily();
        check(vkCreateCommandPool(device_.logicalDevice(), &poolInfo, nullptr, &frame.commandPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = frame.commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_.logicalDevice(), &allocateInfo, &frame.commandBuffer),
              "vkAllocateCommandBuffers");

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_.logicalDevice(), &semaphoreInfo, nullptr, &frame.imageAvailable),
              "vkCreateSemaphore(image available)");

        device_.setDebugName(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<std::uint64_t>(frame.commandPool),
                             "Frame command pool " + std::to_string(index));
        device_.setDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(frame.commandBuffer),
                             "Frame command buffer " + std::to_string(index));
        device_.setDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(frame.imageAvailable),
                             "Image available " + std::to_string(index));
    }
}

void Renderer::destroyFrameContexts() noexcept {
    for (auto& frame : frames_) {
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_.logicalDevice(), frame.imageAvailable, nullptr);
        }
        if (frame.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_.logicalDevice(), frame.commandPool, nullptr);
        }
        frame = {};
    }
}

void Renderer::createPresentSemaphores() {
    presentReady_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::size_t index = 0; index < presentReady_.size(); ++index) {
        check(vkCreateSemaphore(device_.logicalDevice(), &semaphoreInfo, nullptr, &presentReady_[index]),
              "vkCreateSemaphore(present ready)");
        device_.setDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(presentReady_[index]),
                             "Present ready for swapchain image " + std::to_string(index));
    }
}

void Renderer::destroyPresentSemaphores() noexcept {
    for (const auto semaphore : presentReady_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_.logicalDevice(), semaphore, nullptr);
        }
    }
    presentReady_.clear();
}

void Renderer::recordFrame(const VkCommandBuffer commandBuffer, const std::uint32_t imageIndex) const {
    forwardPass_->record(commandBuffer, swapchain_.image(imageIndex), swapchain_.imageView(imageIndex),
                         swapchain_.extent(), glfwGetTime());
}

void Renderer::waitForFrame(const FrameContext& frame) const {
    if (frame.timelineValue == 0) {
        return;
    }
    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1;
    const VkSemaphore timeline = device_.frameTimeline();
    waitInfo.pSemaphores = &timeline;
    waitInfo.pValues = &frame.timelineValue;
    check(vkWaitSemaphores(device_.logicalDevice(), &waitInfo, UINT64_MAX), "vkWaitSemaphores(frame timeline)");
}

void Renderer::recreateSwapchain() {
    swapchain_.recreate();
    if (glfwWindowShouldClose(window_) == GLFW_FALSE) {
        destroyPresentSemaphores();
        createPresentSemaphores();
        forwardPass_->resize(swapchain_.extent());
    }
}

} // namespace shaderlab::render
