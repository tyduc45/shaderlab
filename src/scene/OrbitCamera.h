#pragma once

#include "scene/ModelAsset.h"

#include <volk.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

namespace shaderlab::scene {

class OrbitCamera final {
public:
    void frame(const Bounds& bounds);
    void update(GLFWwindow* window, float deltaSeconds);
    [[nodiscard]] glm::mat4 viewProjection(VkExtent2D extent) const;

private:
    glm::vec3 target_{};
    float distance_ = 5.0F;
    float yaw_ = 0.65F;
    float pitch_ = 0.25F;
    glm::dvec2 previousCursor_{};
    bool dragging_ = false;
};

} // namespace shaderlab::scene
