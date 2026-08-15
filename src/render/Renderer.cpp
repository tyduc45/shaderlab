#include "render/Renderer.h"

#include "core/Log.h"
#include "editor/EditorUi.h"
#include "render/passes/ForwardPass.h"

#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <GLFW/glfw3.h>

#include <array>
#include <memory>
#include <sstream>
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

Renderer::Renderer(rhi::Device& device, rhi::Swapchain& swapchain, GLFWwindow* window,
                   const std::filesystem::path& modelPath)
    : device_(device), swapchain_(swapchain), window_(window) {
    if (window_ == nullptr) {
        throw std::invalid_argument("Renderer requires a valid GLFW window");
    }
    createFrameContexts();
    createPresentSemaphores();
    forwardPass_ = std::make_unique<ForwardPass>(device_, swapchain_, modelPath);
    userFragmentPath_ = std::filesystem::path(SHADERLAB_SOURCE_DIR) /
                        "assets/shaders/user/default.frag";
    camera_.frame(forwardPass_->bounds());
    glfwSetWindowUserPointer(window_, this);
    glfwSetCursorPosCallback(window_, cursorPositionCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetKeyCallback(window_, keyCallback);
    editorUi_ = std::make_unique<editor::EditorUi>(device_, swapchain_, window_);
    lastFrameTime_ = glfwGetTime();
}

Renderer::~Renderer() {
    shaderReloads_.waitIdle();
    gpuBuildJobs_.waitIdle();
    device_.waitIdle();
    camera_.releaseInput(window_);
    editorUi_.reset();
    glfwSetCursorPosCallback(window_, nullptr);
    glfwSetMouseButtonCallback(window_, nullptr);
    glfwSetKeyCallback(window_, nullptr);
    glfwSetWindowUserPointer(window_, nullptr);
    forwardPass_.reset();
    destroyPresentSemaphores();
    destroyFrameContexts();
}

void Renderer::drawFrame() {
    const double now = glfwGetTime();
    camera_.update(window_, static_cast<float>(now - lastFrameTime_));
    lastFrameTime_ = now;

    device_.deletionQueue().flush(frameNumber_);
    processShaderReloads();
    if (const std::uint64_t generation = forwardPass_->commitPendingGpuState(frameNumber_); generation != 0) {
        lastAppliedShaderGeneration_ = generation;
        core::Log::instance().write(core::LogLevel::Info,
                                    "Applied shader generation " + std::to_string(generation));
    }
    if (swapchain_.framebufferExtentChanged()) {
        recreateSwapchain();
    }
    if (editorUi_->beginFrame(shaderReloadInFlight(), shaderReloads_.generation(),
                              lastAppliedShaderGeneration_)) {
        static_cast<void>(requestShaderReload());
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
    ++frameNumber_;
}

std::uint64_t Renderer::requestShaderReload() {
    const std::uint64_t generation = shaderReloads_.requestFile(userFragmentPath_);
    core::Log::instance().write(core::LogLevel::Info,
                                "Queued shader generation " + std::to_string(generation));
    return generation;
}

std::uint64_t Renderer::requestShaderSource(std::filesystem::path sourcePath, std::string source) {
    const std::uint64_t generation = shaderReloads_.requestSource(std::move(sourcePath), std::move(source));
    core::Log::instance().write(core::LogLevel::Info,
                                "Queued shader generation " + std::to_string(generation));
    return generation;
}

void Renderer::waitForShaderReload() {
    shaderReloads_.waitIdle();
    processShaderReloads();
    gpuBuildJobs_.waitIdle();
    processShaderReloads();
}

bool Renderer::shaderReloadInFlight() const noexcept {
    return shaderReloads_.inFlight() || gpuBuildsInFlight_ != 0;
}

void Renderer::processShaderReloads() {
    if (auto compile = shaderReloads_.pollCurrent()) {
        if (!compile->success) {
            logCompileFailure(*compile);
        } else {
            const std::uint64_t generation = compile->generation;
            auto spirv = std::move(compile->spirv);
            ForwardPass* const pass = forwardPass_.get();
            ++gpuBuildsInFlight_;
            try {
                gpuBuildJobs_.submit([this, pass, generation, spirv = std::move(spirv)] {
                    GpuBuildResult result;
                    result.generation = generation;
                    try {
                        result.state = pass->buildGpuState(spirv, generation);
                    } catch (const std::exception& error) {
                        result.error = error.what();
                    } catch (...) {
                        result.error = "Unknown pipeline creation failure";
                    }
                    gpuBuildResults_.push(std::move(result));
                });
            } catch (...) {
                --gpuBuildsInFlight_;
                throw;
            }
        }
    }

    while (auto result = gpuBuildResults_.tryPop()) {
        if (gpuBuildsInFlight_ != 0) {
            --gpuBuildsInFlight_;
        }
        if (result->generation != shaderReloads_.generation()) {
            continue;
        }
        if (!result->state) {
            core::Log::instance().write(core::LogLevel::Error,
                                        "Shader generation " + std::to_string(result->generation) +
                                            " pipeline creation failed: " + result->error);
            continue;
        }
        forwardPass_->stageGpuState(std::move(result->state));
    }
}

void Renderer::logCompileFailure(const shader::CompileResult& result) {
    if (result.errors.empty()) {
        core::Log::instance().write(core::LogLevel::Error,
                                    "Shader generation " + std::to_string(result.generation) +
                                        " compilation failed: " + result.diagnostics);
        return;
    }
    for (const auto& error : result.errors) {
        std::ostringstream message;
        message << error.path.string();
        if (error.line != 0) {
            message << ':' << error.line;
            if (error.column != 0) {
                message << ':' << error.column;
            }
        }
        message << ": " << error.message;
        core::Log::instance().write(core::LogLevel::Error, message.str());
    }
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

void Renderer::recordFrame(const VkCommandBuffer commandBuffer, const std::uint32_t imageIndex) {
    const VkImage colorImage = swapchain_.image(imageIndex);
    const VkImageView colorView = swapchain_.imageView(imageIndex);
    forwardPass_->record(commandBuffer, colorImage, colorView,
                         swapchain_.extent(), camera_.viewProjection(swapchain_.extent()));
    editorUi_->record(commandBuffer, colorImage, colorView, swapchain_.extent(), frameIndex_);
    forwardPass_->transitionToPresent(commandBuffer, colorImage);
}

void Renderer::cursorPositionCallback(GLFWwindow* window, const double cursorX, const double cursorY) {
    if (auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window)); renderer != nullptr) {
        renderer->camera_.onCursorPosition(cursorX, cursorY);
    }
}

void Renderer::mouseButtonCallback(GLFWwindow* window, const int button, const int action, const int modifiers) {
    static_cast<void>(modifiers);
    if (auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window)); renderer != nullptr) {
        if (action == GLFW_PRESS && renderer->editorUi_ && renderer->editorUi_->wantsMouseCapture()) {
            return;
        }
        renderer->camera_.onMouseButton(window, button, action);
    }
}

void Renderer::keyCallback(GLFWwindow* window, const int key, const int scanCode,
                           const int action, const int modifiers) {
    static_cast<void>(scanCode);
    static_cast<void>(modifiers);
    if (key != GLFW_KEY_F5 || action != GLFW_PRESS) {
        return;
    }
    if (auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window)); renderer != nullptr) {
        static_cast<void>(renderer->requestShaderReload());
    }
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
        editorUi_.reset();
        destroyPresentSemaphores();
        createPresentSemaphores();
        forwardPass_->resize(swapchain_.extent());
        editorUi_ = std::make_unique<editor::EditorUi>(device_, swapchain_, window_);
    }
}

} // namespace shaderlab::render
