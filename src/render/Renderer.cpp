#include "render/Renderer.h"

#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
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
}

Renderer::~Renderer() {
    device_.waitIdle();
    destroyFrameContexts();
}

void Renderer::drawFrame() {
    if (swapchain_.framebufferExtentChanged()) {
        swapchain_.recreate();
    }

    auto& frame = frames_[frameIndex_];
    waitForFrame(frame);

    std::uint32_t imageIndex = 0;
    const auto acquireResult = vkAcquireNextImageKHR(device_.logicalDevice(), swapchain_.handle(),
                                                      UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.recreate();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        check(acquireResult, "vkAcquireNextImageKHR");
    }

    check(vkResetCommandPool(device_.logicalDevice(), frame.commandPool, 0), "vkResetCommandPool");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    recordClear(frame.commandBuffer, imageIndex);
    check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = frame.commandBuffer;

    VkSemaphoreSubmitInfo renderFinishedInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    renderFinishedInfo.semaphore = frame.renderFinished;
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
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const auto presentResult = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        swapchain_.recreate();
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
        check(vkCreateSemaphore(device_.logicalDevice(), &semaphoreInfo, nullptr, &frame.renderFinished),
              "vkCreateSemaphore(render finished)");

        device_.setDebugName(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<std::uint64_t>(frame.commandPool),
                             "Frame command pool " + std::to_string(index));
        device_.setDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(frame.commandBuffer),
                             "Frame command buffer " + std::to_string(index));
        device_.setDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(frame.imageAvailable),
                             "Image available " + std::to_string(index));
        device_.setDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(frame.renderFinished),
                             "Render finished " + std::to_string(index));
    }
}

void Renderer::destroyFrameContexts() noexcept {
    for (auto& frame : frames_) {
        if (frame.renderFinished != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_.logicalDevice(), frame.renderFinished, nullptr);
        }
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_.logicalDevice(), frame.imageAvailable, nullptr);
        }
        if (frame.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_.logicalDevice(), frame.commandPool, nullptr);
        }
        frame = {};
    }
}

void Renderer::recordClear(const VkCommandBuffer commandBuffer, const std::uint32_t imageIndex) const {
    VkImageMemoryBarrier2 toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toColor.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toColor.srcAccessMask = VK_ACCESS_2_NONE;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.image = swapchain_.image(imageIndex);
    toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColor.subresourceRange.levelCount = 1;
    toColor.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    const double time = glfwGetTime();
    const float pulse = static_cast<float>(0.5 + 0.5 * std::sin(time * 0.75));
    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = swapchain_.imageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.025F + pulse * 0.025F, 0.035F, 0.075F + pulse * 0.05F, 1.0F}};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea.extent = swapchain_.extent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = VK_ACCESS_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image = swapchain_.image(imageIndex);
    toPresent.subresourceRange = toColor.subresourceRange;
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
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

} // namespace shaderlab::render

