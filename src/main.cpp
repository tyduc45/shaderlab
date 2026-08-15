#include "core/Log.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <Windows.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>

namespace {

bool smokeTestRequested() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"SHADERLAB_SMOKE_TEST", value, 2) > 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using shaderlab::core::Log;
    using shaderlab::core::LogLevel;

    try {
        Log::instance().write(LogLevel::Info, "ShaderLab starting");
        glfwSetErrorCallback([](int, const char* description) {
            Log::instance().write(LogLevel::Error, description != nullptr ? description : "Unknown GLFW error");
        });
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit failed");
        }

        struct GlfwGuard final {
            ~GlfwGuard() { glfwTerminate(); }
        } glfwGuard;

        const bool smokeTest = smokeTestRequested();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, smokeTest ? GLFW_FALSE : GLFW_TRUE);
        const auto windowDeleter = [](GLFWwindow* window) { glfwDestroyWindow(window); };
        std::unique_ptr<GLFWwindow, decltype(windowDeleter)> window(
            glfwCreateWindow(1280, 720, "ShaderLab", nullptr, nullptr), windowDeleter);
        if (!window) {
            throw std::runtime_error("glfwCreateWindow failed");
        }

        shaderlab::rhi::Device device(window.get());
        shaderlab::rhi::Swapchain swapchain(device, window.get());
        shaderlab::render::Renderer renderer(device, swapchain, window.get());
        if (smokeTest) {
            for (int frame = 0; frame < 4; ++frame) {
                renderer.drawFrame();
            }
            device.waitIdle();
            const auto messages = Log::instance().snapshot();
            const bool hasError = std::ranges::any_of(messages, [](const auto& message) {
                return message.level == LogLevel::Error;
            });
            if (hasError) {
                throw std::runtime_error("Vulkan smoke test received an error-level validation message");
            }
            return 0;
        }
        while (glfwWindowShouldClose(window.get()) == GLFW_FALSE) {
            glfwPollEvents();
            renderer.drawFrame();
        }
        device.waitIdle();
        return 0;
    } catch (const std::exception& error) {
        Log::instance().write(LogLevel::Error, error.what());
        MessageBoxA(nullptr, error.what(), "ShaderLab fatal error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
