#include "core/Log.h"

#include <Windows.h>

#include <utility>

namespace shaderlab::core {

Log& Log::instance() {
    static Log log;
    return log;
}

void Log::write(const LogLevel level, const std::string_view message) {
    LogMessage entry{level, std::string(message)};
    Sink sink;
    {
        const std::scoped_lock lock(mutex_);
        messages_.push_back(entry);
        sink = sink_;
    }

    std::string debuggerLine = entry.text;
    debuggerLine.push_back('\n');
    OutputDebugStringA(debuggerLine.c_str());
    if (sink) {
        sink(entry);
    }
}

void Log::setSink(Sink sink) {
    const std::scoped_lock lock(mutex_);
    sink_ = std::move(sink);
}

std::vector<LogMessage> Log::snapshot() const {
    const std::scoped_lock lock(mutex_);
    return messages_;
}

} // namespace shaderlab::core

