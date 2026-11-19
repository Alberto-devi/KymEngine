#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <string_view>

namespace Kyumist {

class Scene {
public:
    Scene() = default;

    entt::entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(entt::entity entity);
    void Clear();

    void CreateDefaultScene();

    entt::entity FindEntityByName(std::string_view name) const;
    glm::mat4 GetWorldTransform(entt::entity entity) const;
    bool ComputePrimaryCameraViewProjection(float aspect, glm::mat4& outViewProj) const;

    bool IsValid(entt::entity entity) const {
        return entity != entt::null && m_Registry.valid(entity);
    }

    entt::registry& GetRegistry() { return m_Registry; }
    const entt::registry& GetRegistry() const { return m_Registry; }

private:
    entt::registry m_Registry;
};

} // namespace Kyumist

