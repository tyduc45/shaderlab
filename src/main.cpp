#include "core/Log.h"

#include <Windows.h>

#include <exception>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using shaderlab::core::Log;
    using shaderlab::core::LogLevel;

    try {
        Log::instance().write(LogLevel::Info, "ShaderLab starting");
        MessageBoxW(nullptr, L"ShaderLab M1 bootstrap is ready.", L"ShaderLab", MB_OK | MB_ICONINFORMATION);
        return 0;
    } catch (const std::exception& error) {
        Log::instance().write(LogLevel::Error, error.what());
        MessageBoxA(nullptr, error.what(), "ShaderLab fatal error", MB_OK | MB_ICONERROR);
        return 1;
    }
}

