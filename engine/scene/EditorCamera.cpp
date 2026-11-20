#include "scene/EditorCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Kyumist {

void EditorCamera::Reset() {
    m_Position = {0.0f, 2.2f, 5.5f};
    m_Pitch = -18.0f;
    m_Yaw = -90.0f;
    m_Fov = 45.0f;
    m_OrthoCenter = {0.0f, 1.0f};
    m_OrthoSize = 4.5f;
}

glm::vec3 EditorCamera::GetForward() const {
    const float yawRad = glm::radians(m_Yaw);
    const float pitchRad = glm::radians(m_Pitch);
    return glm::normalize(glm::vec3(
        std::cos(yawRad) * std::cos(pitchRad),
        std::sin(pitchRad),
        std::sin(yawRad) * std::cos(pitchRad)
    ));
}

glm::vec3 EditorCamera::GetRight() const {
    return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 EditorCamera::GetUp() const {
    return glm::normalize(glm::cross(GetRight(), GetForward()));
}

glm::mat4 EditorCamera::GetViewMatrix() const {
    if (m_Is2D) {
        return glm::translate(glm::mat4(1.0f), glm::vec3(-m_OrthoCenter.x, -m_OrthoCenter.y, 0.0f));
    }
    return glm::lookAt(m_Position, m_Position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 EditorCamera::GetProjectionMatrix(float aspectRatio) const {
    const float safeAspect = std::max(aspectRatio, 0.01f);
    if (m_Is2D) {
        const float halfH = m_OrthoSize * 0.5f;
        const float halfW = halfH * safeAspect;
        return glm::ortho(-halfW, halfW, -halfH, halfH, m_OrthoNear, m_OrthoFar);
    }
    return glm::perspective(glm::radians(m_Fov), safeAspect, m_Near, m_Far);
}

glm::mat4 EditorCamera::GetViewProjection(float aspectRatio) const {
    return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
}

void EditorCamera::OnUpdate(Timestep ts, GLFWwindow* window, bool viewportHovered, float scrollDelta) {
#ifdef __ANDROID__
    (void)ts;
    (void)window;
    (void)viewportHovered;
    (void)scrollDelta;
    return;
#else
    if (!window) {
        return;
    }

    if (viewportHovered && scrollDelta != 0.0f) {
        if (m_Is2D) {
            m_OrthoSize = std::clamp(m_OrthoSize - scrollDelta * 0.45f, 0.5f, 40.0f);
        } else {
            m_Position += GetForward() * (scrollDelta * 0.6f);
        }
    }

    const bool rightMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (!rightMouseDown) {
        m_Dragging = false;
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (!m_Dragging) {
        if (!viewportHovered) {
            return;
        }
        m_Dragging = true;
        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;
        return;
    }

    const float dx = static_cast<float>(mouseX - m_LastMouseX);
    const float dy = static_cast<float>(mouseY - m_LastMouseY);
    m_LastMouseX = mouseX;
    m_LastMouseY = mouseY;

    if (m_Is2D) {
        m_OrthoCenter.x -= dx * 0.0045f * m_OrthoSize;
        m_OrthoCenter.y += dy * 0.0045f * m_OrthoSize;
        return;
    }

    m_Yaw += dx * 0.12f;
    m_Pitch = std::clamp(m_Pitch - dy * 0.12f, -89.0f, 89.0f);

    float speed = 4.5f * ts.GetSeconds();
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        speed *= 2.5f;
    }

    const glm::vec3 forward = GetForward();
    const glm::vec3 right = GetRight();
    const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_Position += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) m_Position -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) m_Position += right * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) m_Position -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) m_Position += worldUp * speed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) m_Position -= worldUp * speed;
#endif
}

} // namespace Kyumist

