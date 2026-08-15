#pragma once

#include <cstdint>

namespace shaderlab::shader {

class GenerationCounter final {
public:
    [[nodiscard]] std::uint64_t begin() noexcept { return ++current_; }
    [[nodiscard]] std::uint64_t current() const noexcept { return current_; }
    [[nodiscard]] bool isCurrent(const std::uint64_t generation) const noexcept {
        return generation == current_;
    }

private:
    std::uint64_t current_ = 0;
};

} // namespace shaderlab::shader

