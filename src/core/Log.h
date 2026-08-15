#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace shaderlab::core {

enum class LogLevel {
    Info,
    Warning,
    Error,
    Validation,
};

struct LogMessage {
    LogLevel level;
    std::string text;
};

class Log final {
public:
    using Sink = std::function<void(const LogMessage&)>;

    static Log& instance();

    void write(LogLevel level, std::string_view message);
    void setSink(Sink sink);
    [[nodiscard]] std::vector<LogMessage> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<LogMessage> messages_;
    Sink sink_;
};

} // namespace shaderlab::core

