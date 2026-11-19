#pragma once

#include "scene/Scene.hpp"

#include <filesystem>

namespace Kyumist {

class SceneSerializer {
public:
    static std::string SerializeToString(const Scene& scene);
    static bool DeserializeFromString(Scene& scene, const std::string& jsonStr);

    static bool SaveToFile(const Scene& scene, const std::filesystem::path& filepath);
    static bool LoadFromFile(Scene& scene, const std::filesystem::path& filepath);

    static bool SavePrefab(const Scene& scene, entt::entity entity, const std::filesystem::path& filepath);
    static entt::entity LoadPrefab(Scene& scene, const std::filesystem::path& filepath);
};

} // namespace Kyumist

