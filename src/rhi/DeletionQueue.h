#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace shaderlab::rhi {

class DeletionQueue final {
public:
    static constexpr std::uint64_t DefaultDelayFrames = 3;

    explicit DeletionQueue(std::uint64_t delayFrames = DefaultDelayFrames);
    ~DeletionQueue();

    DeletionQueue(const DeletionQueue&) = delete;
    DeletionQueue& operator=(const DeletionQueue&) = delete;

    void push(std::uint64_t currentFrame, std::function<void()> destroy);
    void flush(std::uint64_t currentFrame);
    void flushAll() noexcept;
    [[nodiscard]] std::size_t pendingCount() const;

private:
    struct Entry {
        std::uint64_t frame = 0;
        std::function<void()> destroy;
    };

    std::uint64_t delayFrames_;
    mutable std::mutex mutex_;
    std::deque<Entry> queue_;
};

} // namespace shaderlab::rhi

