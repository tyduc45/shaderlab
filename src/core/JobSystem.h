#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace shaderlab::core {

template <typename T>
class ResultQueue final {
public:
    void push(T value) {
        const std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(value));
    }

    [[nodiscard]] std::optional<T> tryPop() {
        const std::scoped_lock lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    [[nodiscard]] std::size_t size() const {
        const std::scoped_lock lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::deque<T> queue_;
};

class JobSystem final {
public:
    using Job = std::function<void()>;

    explicit JobSystem(std::size_t workerCount = defaultWorkerCount());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(Job job);
    void waitIdle();
    [[nodiscard]] std::size_t workerCount() const noexcept { return workers_.size(); }
    [[nodiscard]] static std::size_t defaultWorkerCount() noexcept;

private:
    void workerLoop();

    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable idle_;
    std::deque<Job> jobs_;
    std::vector<std::thread> workers_;
    std::size_t activeJobs_ = 0;
    bool stopping_ = false;
};

} // namespace shaderlab::core

