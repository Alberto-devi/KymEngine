#pragma once

#include "core/Timestep.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Kyumist {

class EditorCamera {
public:
    EditorCamera() = default;

    void OnUpdate(Timestep ts, GLFWwindow* window, bool viewportHovered, float scrollDelta);

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspectRatio) const;
    glm::mat4 GetViewProjection(float aspectRatio) const;

    bool Is2D() const { return m_Is2D; }
    void Set2D(bool is2D) { m_Is2D = is2D; }

    float GetFov() const { return m_Fov; }
    void SetFov(float fov) { m_Fov = fov; }

    float GetOrthoSize() const { return m_OrthoSize; }
    void SetOrthoSize(float orthoSize) { m_OrthoSize = orthoSize; }

    const glm::vec3& GetPosition() const { return m_Position; }
    void SetPosition(const glm::vec3& pos) { m_Position = pos; }

    glm::vec3 GetForward() const;
    glm::vec3 GetRight() const;
    glm::vec3 GetUp() const;

    void Reset();

private:
    bool m_Is2D = false;

    glm::vec3 m_Position{0.0f, 2.2f, 5.5f};
    float m_Pitch = -18.0f;
    float m_Yaw = -90.0f;
    float m_Fov = 45.0f;
    float m_Near = 0.1f;
    float m_Far = 100.0f;

    glm::vec2 m_OrthoCenter{0.0f, 1.0f};
    float m_OrthoSize = 4.5f;
    float m_OrthoNear = -10.0f;
    float m_OrthoFar = 10.0f;

    bool m_Dragging = false;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
};

} // namespace Kyumist

