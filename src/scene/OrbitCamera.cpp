#include "scene/OrbitCamera.h"

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace shaderlab::scene {

void OrbitCamera::frame(const Bounds& bounds) {
    target_ = bounds.center();
    distance_ = std::max(bounds.radius() * 2.4F, 0.5F);
}

void OrbitCamera::update(GLFWwindow* window, const float deltaSeconds) {
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    const glm::dvec2 cursor(cursorX, cursorY);
    const bool pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (pressed && dragging_) {
        const glm::dvec2 delta = cursor - previousCursor_;
        yaw_ -= static_cast<float>(delta.x) * 0.006F;
        pitch_ = std::clamp(pitch_ - static_cast<float>(delta.y) * 0.006F, -1.45F, 1.45F);
    }
    dragging_ = pressed;
    previousCursor_ = cursor;

    const float zoomDirection = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ? -1.0F : 0.0F) +
                                (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? 1.0F : 0.0F);
    distance_ = std::max(0.05F, distance_ * std::exp(zoomDirection * deltaSeconds * 1.8F));
}

glm::mat4 OrbitCamera::viewProjection(const VkExtent2D extent) const {
    const float cosPitch = std::cos(pitch_);
    const glm::vec3 direction(cosPitch * std::sin(yaw_), std::sin(pitch_), cosPitch * std::cos(yaw_));
    const glm::vec3 eye = target_ + direction * distance_;
    const glm::mat4 view = glm::lookAt(eye, target_, glm::vec3(0.0F, 1.0F, 0.0F));
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 projection = glm::perspective(glm::radians(55.0F), aspect, 0.01F, std::max(distance_ * 20.0F, 100.0F));
    projection[1][1] *= -1.0F;
    return projection * view;
}

} // namespace shaderlab::scene
