#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace shaderlab::scene {

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    glm::vec2 uv{};
};

struct Submesh {
    std::string name;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    int materialIndex = -1;
};

struct Bounds {
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};

    [[nodiscard]] glm::vec3 center() const noexcept { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] float radius() const noexcept;
};

class ModelAsset final {
public:
    static ModelAsset load(const std::filesystem::path& path);
    static ModelAsset makeFallbackCube();

    [[nodiscard]] const std::vector<Vertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept { return indices_; }
    [[nodiscard]] const std::vector<Submesh>& submeshes() const noexcept { return submeshes_; }
    [[nodiscard]] const Bounds& bounds() const noexcept { return bounds_; }
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }

private:
    void includeInBounds(const glm::vec3& position);

    std::filesystem::path sourcePath_;
    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<Submesh> submeshes_;
    Bounds bounds_;
};

} // namespace shaderlab::scene

