#pragma once

#include "core/Timestep.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Kyumist {

class Scene;

struct RaycastHit {
    bool hit = false;
    entt::entity entity = entt::null;
    float distance = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

class PhysicsSystem {
public:
    static void OnRuntimeStart(Scene* scene);
    static void Step(Scene* scene, Timestep ts);
    static RaycastHit Raycast(
        const Scene* scene,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance
    );
};

} // namespace Kyumist
