#include "core/JobSystem.h"

#include "core/Log.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace shaderlab::core {

JobSystem::JobSystem(const std::size_t workerCount) {
    if (workerCount == 0) {
        throw std::invalid_argument("JobSystem requires at least one worker");
    }
    workers_.reserve(workerCount);
    try {
        for (std::size_t index = 0; index < workerCount; ++index) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    } catch (...) {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        workAvailable_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

JobSystem::~JobSystem() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void JobSystem::submit(Job job) {
    if (!job) {
        throw std::invalid_argument("JobSystem job must be callable");
    }
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("Cannot submit to a stopping JobSystem");
        }
        jobs_.push_back(std::move(job));
    }
    workAvailable_.notify_one();
}

void JobSystem::waitIdle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return jobs_.empty() && activeJobs_ == 0; });
}

std::size_t JobSystem::defaultWorkerCount() noexcept {
    const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
    return std::max<std::size_t>(1, hardware > 1 ? hardware - 1 : 1);
}

void JobSystem::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            workAvailable_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++activeJobs_;
        }
        try {
            job();
        } catch (const std::exception& error) {
            Log::instance().write(LogLevel::Error, std::string("Worker job failed: ") + error.what());
        } catch (...) {
            Log::instance().write(LogLevel::Error, "Worker job failed with unknown exception");
        }
        {
            const std::scoped_lock lock(mutex_);
            --activeJobs_;
            if (jobs_.empty() && activeJobs_ == 0) {
                idle_.notify_all();
            }
        }
    }
}

} // namespace shaderlab::core
