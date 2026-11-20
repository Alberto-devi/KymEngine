#include "scene/Scene.hpp"
#include "scene/Components.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>

namespace Kyumist {

entt::entity Scene::CreateEntity(const std::string& name) {
    entt::entity entity = m_Registry.create();
    m_Registry.emplace<TagComponent>(entity, TagComponent{name.empty() ? "Entity" : name});
    m_Registry.emplace<TransformComponent>(entity);
    return entity;
}

void Scene::DestroyEntity(entt::entity entity) {
    if (IsValid(entity)) {
        m_Registry.destroy(entity);
    }
}

void Scene::Clear() {
    m_Registry.clear();
}

entt::entity Scene::FindEntityByName(std::string_view name) const {
    if (name.empty()) {
        return entt::null;
    }
    auto view = m_Registry.view<TagComponent>();
    for (auto entity : view) {
        if (view.get<TagComponent>(entity).tag == name) {
            return entity;
        }
    }
    return entt::null;
}

glm::mat4 Scene::GetWorldTransform(entt::entity entity) const {
    if (!IsValid(entity)) {
        return glm::mat4(1.0f);
    }

    std::vector<entt::entity> chain;
    chain.reserve(8);
    entt::entity current = entity;
    for (int depth = 0; depth < 16 && IsValid(current); ++depth) {
        if (std::find(chain.begin(), chain.end(), current) != chain.end()) {
            break; // Cycle protection
        }
        chain.push_back(current);
        const auto* hierarchy = m_Registry.try_get<HierarchyComponent>(current);
        if (!hierarchy || hierarchy->parentTag.empty()) {
            break;
        }
        current = FindEntityByName(hierarchy->parentTag);
    }

    glm::mat4 world(1.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (const auto* tr = m_Registry.try_get<TransformComponent>(*it)) {
            world *= tr->GetTransform();
        }
    }
    return world;
}

bool Scene::ComputePrimaryCameraViewProjection(float aspect, glm::mat4& outViewProj) const {
    auto view = m_Registry.view<TransformComponent, CameraComponent>();
    for (auto entity : view) {
        const auto& cam = view.get<CameraComponent>(entity);
        if (!cam.primary) {
            continue;
        }

        const glm::mat4 world = GetWorldTransform(entity);
        const glm::vec3 eye = glm::vec3(world[3]);
        const glm::vec3 rawForward = glm::vec3(-world[2]);
        const glm::vec3 rawUp = glm::vec3(world[1]);
        const glm::vec3 forward = glm::length(rawForward) > 1e-5f ? glm::normalize(rawForward) : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 up = glm::length(rawUp) > 1e-5f ? glm::normalize(rawUp) : glm::vec3(0.0f, 1.0f, 0.0f);

        const glm::mat4 viewMat = glm::lookAt(eye, eye + forward, up);
        const float safeAspect = std::max(aspect, 0.01f);
        const float nearClip = std::max(cam.nearClip, 0.01f);
        const float farClip = std::max(cam.farClip, nearClip + 1.0f);

        glm::mat4 proj(1.0f);
        if (cam.isOrthographic) {
            const float halfH = std::max(cam.orthoSize * 0.5f, 0.1f);
            const float halfW = halfH * safeAspect;
            proj = glm::ortho(-halfW, halfW, -halfH, halfH, -farClip, farClip);
        } else {
            proj = glm::perspective(glm::radians(std::clamp(cam.fov, 10.0f, 160.0f)), safeAspect, nearClip, farClip);
        }

        outViewProj = proj * viewMat;
        return true;
    }
    return false;
}

void Scene::UpdateAnimations(float deltaSeconds) {
    if (deltaSeconds <= 0.0f) {
        return;
    }
    auto view = m_Registry.view<AnimatorComponent>();
    for (auto entity : view) {
        auto& anim = view.get<AnimatorComponent>(entity);
        if (anim.playing) {
            anim.time += deltaSeconds * anim.speed;
        }
    }
}

void Scene::CreateDefaultScene() {
    Clear();

    {
        entt::entity light = CreateEntity("Sol Direccional");
        auto& tr = m_Registry.get<TransformComponent>(light);
        tr.translation = {0.0f, 4.0f, 0.0f};
        tr.rotationDegrees = {-45.0f, -35.0f, 0.0f};
        m_Registry.emplace<DirectionalLightComponent>(light);
        m_Registry.emplace<SkyboxComponent>(light);
    }

    {
        entt::entity camEnt = CreateEntity("Camara Principal");
        auto& tr = m_Registry.get<TransformComponent>(camEnt);
        tr.translation = {0.0f, 2.2f, 6.0f};
        tr.rotationDegrees = {-12.0f, 0.0f, 0.0f};
        auto& cam = m_Registry.emplace<CameraComponent>(camEnt);
        cam.primary = false;
    }

    {
        entt::entity cube = CreateEntity("Cubo PBR");
        auto& tr = m_Registry.get<TransformComponent>(cube);
        tr.translation = {-1.2f, 0.5f, 0.0f};
        tr.rotationDegrees = {0.0f, 25.0f, 0.0f};
        auto& mesh = m_Registry.emplace<MeshRendererComponent>(cube);
        mesh.type = MeshType::Cube;
        mesh.color = {0.90f, 0.32f, 0.25f, 1.0f};
        mesh.roughness = 0.45f;
        m_Registry.emplace<JavaScriptComponent>(cube, JavaScriptComponent{"RotatorScript", true, false});
        auto& rb = m_Registry.emplace<RigidBodyComponent>(cube);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {0.5f, 0.5f, 0.5f};
    }

    {
        entt::entity sphere = CreateEntity("Esfera PBR");
        auto& tr = m_Registry.get<TransformComponent>(sphere);
        tr.translation = {1.2f, 2.4f, 0.0f};
        auto& mesh = m_Registry.emplace<MeshRendererComponent>(sphere);
        mesh.type = MeshType::Sphere;
        mesh.color = {0.25f, 0.65f, 0.95f, 1.0f};
        mesh.roughness = 0.25f;
        auto& rb = m_Registry.emplace<RigidBodyComponent>(sphere);
        rb.bodyType = RigidBodyType::Dynamic;
        rb.shape = ColliderShape::Sphere3D;
        rb.radius = 0.5f;
        rb.restitution = 0.75f;
        auto& audio = m_Registry.emplace<AudioSourceComponent>(sphere);
        audio.soundPath = "audio/bounce.wav";
        audio.volume = 0.8f;
        audio.playOnStart = false;
        audio.playOnBounce = true;
    }

    {
        entt::entity floor = CreateEntity("Suelo 3D");
        auto& tr = m_Registry.get<TransformComponent>(floor);
        tr.translation = {0.0f, 0.0f, 0.0f};
        tr.scale = {8.0f, 1.0f, 8.0f};
        auto& mesh = m_Registry.emplace<MeshRendererComponent>(floor);
        mesh.type = MeshType::Plane;
        mesh.color = {0.48f, 0.52f, 0.58f, 1.0f};
        mesh.roughness = 0.85f;
        auto& rb = m_Registry.emplace<RigidBodyComponent>(floor);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {4.0f, 0.05f, 4.0f};
        rb.restitution = 0.8f;
    }

    {
        entt::entity sprite = CreateEntity("Sprite 2D");
        auto& tr = m_Registry.get<TransformComponent>(sprite);
        tr.translation = {0.0f, 1.8f, -1.0f};
        tr.scale = {1.2f, 1.2f, 1.0f};
        auto& spr = m_Registry.emplace<SpriteRendererComponent>(sprite);
        spr.color = {0.35f, 0.92f, 0.55f, 1.0f};
        spr.sortingOrder = 0;
        spr.checkerPattern = false;
        m_Registry.emplace<JavaScriptComponent>(sprite, JavaScriptComponent{"Oscillator2DScript", true, false});
    }
}

} // namespace Kyumist

