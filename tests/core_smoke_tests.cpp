#include "core/JobSystem.h"
#include "core/Log.h"
#include "rhi/DeletionQueue.h"

#include <atomic>
#include <cstdlib>

int main() {
    using shaderlab::core::Log;
    using shaderlab::core::LogLevel;

    Log::instance().write(LogLevel::Info, "smoke test");
    const auto messages = Log::instance().snapshot();
    if (messages.empty() || messages.front().text != "smoke test") {
        return EXIT_FAILURE;
    }

    shaderlab::rhi::DeletionQueue deletions(3);
    int destroyed = 0;
    deletions.push(5, [&destroyed] { ++destroyed; });
    deletions.flush(7);
    if (destroyed != 0 || deletions.pendingCount() != 1) {
        return EXIT_FAILURE;
    }
    deletions.flush(8);
    if (destroyed != 1 || deletions.pendingCount() != 0) {
        return EXIT_FAILURE;
    }

    shaderlab::core::JobSystem jobs(2);
    shaderlab::core::ResultQueue<int> results;
    std::atomic<int> executed = 0;
    for (int value = 1; value <= 64; ++value) {
        jobs.submit([value, &results, &executed] {
            results.push(value);
            executed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    jobs.waitIdle();
    int sum = 0;
    while (auto result = results.tryPop()) {
        sum += *result;
    }
    return executed.load(std::memory_order_relaxed) == 64 && sum == 2080 ? EXIT_SUCCESS : EXIT_FAILURE;
}
