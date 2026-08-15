#include "core/Log.h"

#include <cstdlib>

int main() {
    using shaderlab::core::Log;
    using shaderlab::core::LogLevel;

    Log::instance().write(LogLevel::Info, "smoke test");
    const auto messages = Log::instance().snapshot();
    return messages.size() == 1 && messages.front().text == "smoke test" ? EXIT_SUCCESS : EXIT_FAILURE;
}

