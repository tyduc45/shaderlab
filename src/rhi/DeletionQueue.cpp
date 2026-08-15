#include "rhi/DeletionQueue.h"

#include "core/Log.h"

#include <stdexcept>
#include <utility>

namespace shaderlab::rhi {

DeletionQueue::DeletionQueue(const std::uint64_t delayFrames) : delayFrames_(delayFrames) {
    if (delayFrames_ == 0) {
        throw std::invalid_argument("DeletionQueue delay must be at least one frame");
    }
}

DeletionQueue::~DeletionQueue() {
    flushAll();
}

void DeletionQueue::push(const std::uint64_t currentFrame, std::function<void()> destroy) {
    if (!destroy) {
        throw std::invalid_argument("DeletionQueue callback must be callable");
    }
    const std::scoped_lock lock(mutex_);
    queue_.push_back(Entry{currentFrame, std::move(destroy)});
}

void DeletionQueue::flush(const std::uint64_t currentFrame) {
    std::deque<Entry> ready;
    {
        const std::scoped_lock lock(mutex_);
        while (!queue_.empty()) {
            const auto& entry = queue_.front();
            if (entry.frame > currentFrame || currentFrame - entry.frame < delayFrames_) {
                break;
            }
            ready.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
    }
    for (auto& entry : ready) {
        entry.destroy();
    }
}

void DeletionQueue::flushAll() noexcept {
    std::deque<Entry> pending;
    {
        const std::scoped_lock lock(mutex_);
        pending.swap(queue_);
    }
    for (auto& entry : pending) {
        try {
            entry.destroy();
        } catch (const std::exception& error) {
            core::Log::instance().write(core::LogLevel::Error,
                                        std::string("Deletion callback failed: ") + error.what());
        } catch (...) {
            core::Log::instance().write(core::LogLevel::Error, "Deletion callback failed with unknown exception");
        }
    }
}

std::size_t DeletionQueue::pendingCount() const {
    const std::scoped_lock lock(mutex_);
    return queue_.size();
}

} // namespace shaderlab::rhi

