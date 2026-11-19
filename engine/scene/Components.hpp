#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <string>

namespace Kyumist {

struct TagComponent {
    std::string tag = "Entity";
};

struct TransformComponent {
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDegrees{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 GetTransform() const {
        const glm::mat4 rX = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 rY = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 rZ = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::translate(glm::mat4(1.0f), translation) * (rZ * rY * rX) * glm::scale(glm::mat4(1.0f), scale);
    }
};

enum class MeshType {
    Cube = 0,
    Sphere = 1,
    Plane = 2,
    CustomOBJ = 3
};

struct MeshRendererComponent {
    MeshType type = MeshType::Cube;
    std::string objPath;
    std::string materialPath;
    std::string texturePath;
    glm::vec4 color{0.85f, 0.85f, 0.85f, 1.0f};
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};
    float metallic = 0.1f;
    float roughness = 0.5f;
    bool castShadows = true;
    bool visible = true;
};

struct SpriteRendererComponent {
    std::string texturePath;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int32_t sortingOrder = 0;
    bool checkerPattern = false;
    int32_t hFrames = 1;
    int32_t vFrames = 1;
    int32_t frameIndex = 0;
    float animFps = 0.0f;
    float animTimer = 0.0f;
    bool visible = true;
};

struct DirectionalLightComponent {
    glm::vec3 color{1.0f, 0.98f, 0.92f};
    float intensity = 1.35f;
};

struct CameraComponent {
    bool primary = true;
    bool isOrthographic = false;
    float fov = 45.0f;
    float orthoSize = 5.0f;
    float nearClip = 0.1f;
    float farClip = 100.0f;
};

struct JavaScriptComponent {
    std::string className = "RotatorScript";
    bool enabled = true;
    bool instanceCreated = false;
};

enum class RigidBodyType {
    Static = 0,
    Dynamic = 1,
    Kinematic = 2
};

enum class ColliderShape {
    Box3D = 0,
    Sphere3D = 1,
    Box2D = 2
};

struct RigidBodyComponent {
    RigidBodyType bodyType = RigidBodyType::Dynamic;
    ColliderShape shape = ColliderShape::Box3D;
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity{0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float mass = 1.0f;
    float restitution = 0.6f;
    float friction = 0.3f;
    bool useGravity = true;
    bool isTrigger = false;
};

struct AudioSourceComponent {
    std::string soundPath = "audio/bounce.wav";
    float volume = 0.8f;
    bool playOnStart = false;
    bool playOnBounce = true;
};

struct HierarchyComponent {
    std::string parentTag;
};

struct PointLightComponent {
    glm::vec3 color{1.0f, 0.82f, 0.52f};
    float intensity = 2.0f;
    float range = 8.0f;
};

struct SkyboxComponent {
    glm::vec3 zenithColor{0.24f, 0.50f, 0.86f};
    glm::vec3 horizonColor{0.64f, 0.80f, 0.96f};
    glm::vec3 groundColor{0.34f, 0.38f, 0.44f};
    bool enabled = true;
};

struct UITextComponent {
    std::string text = "Kyumist Engine";
    glm::vec2 screenPos{24.0f, 48.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float fontScale = 1.2f;
    bool visible = true;
};

} // namespace Kyumist

