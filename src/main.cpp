#include "core/Log.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "rhi/Swapchain.h"

#include <Windows.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {

bool environmentFlagRequested(const wchar_t* name) {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(name, value, 2) > 0;
}

bool smokeTestRequested() {
    return environmentFlagRequested(L"SHADERLAB_SMOKE_TEST");
}

bool shaderReloadSmokeTestRequested() {
    return environmentFlagRequested(L"SHADERLAB_RELOAD_SMOKE_TEST");
}

bool shaderReloadStressTestRequested() {
    return environmentFlagRequested(L"SHADERLAB_RELOAD_STRESS_TEST");
}

bool materialSmokeTestRequested() {
    return environmentFlagRequested(L"SHADERLAB_MATERIAL_SMOKE_TEST");
}

bool headlessTestRequested() {
    return smokeTestRequested() || shaderReloadSmokeTestRequested() ||
           shaderReloadStressTestRequested() || materialSmokeTestRequested();
}

std::filesystem::path requestedModelPath() {
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        throw std::runtime_error("CommandLineToArgvW failed");
    }
    const auto releaseArguments = [](wchar_t** values) { static_cast<void>(LocalFree(values)); };
    const std::unique_ptr<wchar_t*, decltype(releaseArguments)> guard(arguments, releaseArguments);
    if (argumentCount > 1) {
        return std::filesystem::path(arguments[1]);
    }
    const std::filesystem::path defaultModel = "assets/models/DamagedHelmet/DamagedHelmet.gltf";
    return std::filesystem::exists(defaultModel) ? defaultModel : std::filesystem::path{};
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

        const bool shaderReloadSmokeTest = shaderReloadSmokeTestRequested();
        const bool shaderReloadStressTest = shaderReloadStressTestRequested();
        const bool materialSmokeTest = materialSmokeTestRequested();
        const bool smokeTest = headlessTestRequested();
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
        shaderlab::render::Renderer renderer(device, swapchain, window.get(), requestedModelPath());
        if (smokeTest) {
            std::uint64_t expectedShaderGeneration = 0;
            if (shaderReloadStressTest) {
                for (int reload = 0; reload < 100; ++reload) {
                    expectedShaderGeneration = renderer.requestShaderReload();
                    renderer.waitForShaderReload();
                    renderer.drawFrame();
                    if (renderer.lastAppliedShaderGeneration() != expectedShaderGeneration) {
                        throw std::runtime_error("Sequential shader generation was not applied");
                    }
                }
            } else if (shaderReloadSmokeTest) {
                for (int reload = 0; reload < 10; ++reload) {
                    expectedShaderGeneration = renderer.requestShaderReload();
                }
                renderer.waitForShaderReload();
            } else if (materialSmokeTest) {
                expectedShaderGeneration = renderer.requestShaderReload();
                renderer.waitForShaderReload();
            }
            for (int frame = 0; frame < 4; ++frame) {
                renderer.drawFrame();
            }
            device.waitIdle();
            if ((shaderReloadSmokeTest || shaderReloadStressTest || materialSmokeTest) &&
                renderer.lastAppliedShaderGeneration() != expectedShaderGeneration) {
                throw std::runtime_error("Latest shader generation was not applied");
            }
            if (shaderReloadSmokeTest) {
                constexpr const char* invalidSource =
                    "#version 460\nlayout(location=0) out vec4 color; void main(){ color=vec4(; }";
                static_cast<void>(renderer.requestShaderSource("reload_failure.frag", invalidSource));
                renderer.waitForShaderReload();
                for (int frame = 0; frame < 4; ++frame) {
                    renderer.drawFrame();
                }
                device.waitIdle();
                if (renderer.lastAppliedShaderGeneration() != expectedShaderGeneration) {
                    throw std::runtime_error("Failed shader compilation replaced the live GPU state");
                }
            }
            if (materialSmokeTest) {
                constexpr float editedStrength = 0.125F;
                if (!renderer.setMaterialFloat("engravingStrength", editedStrength)) {
                    throw std::runtime_error("Reflected material float was not available");
                }
                renderer.drawFrame();
                expectedShaderGeneration = renderer.requestShaderReload();
                renderer.waitForShaderReload();
                for (int frame = 0; frame < 4; ++frame) {
                    renderer.drawFrame();
                }
                device.waitIdle();
                const auto retained = renderer.materialFloat("engravingStrength");
                if (renderer.lastAppliedShaderGeneration() != expectedShaderGeneration ||
                    !retained || *retained != editedStrength) {
                    throw std::runtime_error("Material value did not survive a layout-unchanged reload");
                }
            }
            if (shaderReloadStressTest && device.deletionQueue().pendingCount() != 0) {
                throw std::runtime_error("DeletionQueue did not drain after shader reload stress test");
            }
            const auto messages = Log::instance().snapshot();
            const bool hasExpectedCompileError = std::ranges::any_of(messages, [](const auto& message) {
                return message.level == LogLevel::Error &&
                       message.text.find("reload_failure.frag:2") != std::string::npos;
            });
            const bool hasUnexpectedError = std::ranges::any_of(messages, [shaderReloadSmokeTest](const auto& message) {
                if (message.level != LogLevel::Error) {
                    return false;
                }
                return !shaderReloadSmokeTest ||
                       message.text.find("reload_failure.frag:2") == std::string::npos;
            });
            if (hasUnexpectedError || (shaderReloadSmokeTest && !hasExpectedCompileError)) {
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
        if (!headlessTestRequested()) {
            MessageBoxA(nullptr, error.what(), "ShaderLab fatal error", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
