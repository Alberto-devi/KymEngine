#include "scene/SceneSerializer.hpp"

#include "core/Log.hpp"
#include "scene/Components.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace Kyumist {

namespace {

nlohmann::json Vec2ToJson(const glm::vec2& v) {
    return nlohmann::json::array({v.x, v.y});
}

nlohmann::json Vec3ToJson(const glm::vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

nlohmann::json Vec4ToJson(const glm::vec4& v) {
    return nlohmann::json::array({v.x, v.y, v.z, v.w});
}

glm::vec2 JsonToVec2(const nlohmann::json& j, const glm::vec2& fallback) {
    if (!j.is_array() || j.size() != 2) {
        return fallback;
    }
    return {j[0].get<float>(), j[1].get<float>()};
}

glm::vec3 JsonToVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) {
        return fallback;
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

glm::vec4 JsonToVec4(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() != 4) {
        return fallback;
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

std::string MeshTypeToString(MeshType type) {
    switch (type) {
        case MeshType::Cube:      return "Cube";
        case MeshType::Sphere:    return "Sphere";
        case MeshType::Plane:     return "Plane";
        case MeshType::CustomOBJ: return "CustomOBJ";
        default:                  return "Cube";
    }
}

MeshType StringToMeshType(const std::string& str) {
    if (str == "Sphere")      return MeshType::Sphere;
    if (str == "Plane")       return MeshType::Plane;
    if (str == "CustomOBJ")   return MeshType::CustomOBJ;
    if (str == "CustomModel") return MeshType::CustomOBJ;
    return MeshType::Cube;
}

std::string BodyTypeToString(RigidBodyType type) {
    switch (type) {
        case RigidBodyType::Static:    return "Static";
        case RigidBodyType::Dynamic:   return "Dynamic";
        case RigidBodyType::Kinematic: return "Kinematic";
        default:                       return "Dynamic";
    }
}

RigidBodyType StringToBodyType(const std::string& str) {
    if (str == "Static")    return RigidBodyType::Static;
    if (str == "Kinematic") return RigidBodyType::Kinematic;
    return RigidBodyType::Dynamic;
}

std::string ShapeToString(ColliderShape shape) {
    switch (shape) {
        case ColliderShape::Box3D:    return "Box3D";
        case ColliderShape::Sphere3D: return "Sphere3D";
        case ColliderShape::Box2D:    return "Box2D";
        default:                      return "Box3D";
    }
}

ColliderShape StringToShape(const std::string& str) {
    if (str == "Sphere3D") return ColliderShape::Sphere3D;
    if (str == "Box2D")    return ColliderShape::Box2D;
    return ColliderShape::Box3D;
}

nlohmann::json SerializeEntityJson(const entt::registry& registry, entt::entity entity) {
    nlohmann::json entityJson;

    if (const auto* tag = registry.try_get<TagComponent>(entity)) {
        entityJson["tag"] = tag->tag;
    }
    if (const auto* tr = registry.try_get<TransformComponent>(entity)) {
        entityJson["transform"] = {
            {"translation", Vec3ToJson(tr->translation)},
            {"rotationDegrees", Vec3ToJson(tr->rotationDegrees)},
            {"scale", Vec3ToJson(tr->scale)}
        };
    }
    if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
        entityJson["hierarchy"] = {
            {"parentTag", hierarchy->parentTag}
        };
    }
    if (const auto* mesh = registry.try_get<MeshRendererComponent>(entity)) {
        entityJson["meshRenderer"] = {
            {"type", MeshTypeToString(mesh->type)},
            {"objPath", mesh->objPath},
            {"materialPath", mesh->materialPath},
            {"texturePath", mesh->texturePath},
            {"color", Vec4ToJson(mesh->color)},
            {"emissive", Vec3ToJson(mesh->emissive)},
            {"metallic", mesh->metallic},
            {"roughness", mesh->roughness},
            {"castShadows", mesh->castShadows},
            {"visible", mesh->visible}
        };
    }
    if (const auto* anim = registry.try_get<AnimatorComponent>(entity)) {
        entityJson["animator"] = {
            {"currentAnimation", anim->currentAnimation},
            {"time", anim->time},
            {"speed", anim->speed},
            {"loop", anim->loop},
            {"playing", anim->playing},
            {"autoPlayFirst", anim->autoPlayFirst}
        };
    }
    if (const auto* sprite = registry.try_get<SpriteRendererComponent>(entity)) {
        entityJson["spriteRenderer"] = {
            {"texturePath", sprite->texturePath},
            {"color", Vec4ToJson(sprite->color)},
            {"sortingOrder", sprite->sortingOrder},
            {"checkerPattern", sprite->checkerPattern},
            {"hFrames", sprite->hFrames},
            {"vFrames", sprite->vFrames},
            {"frameIndex", sprite->frameIndex},
            {"animFps", sprite->animFps},
            {"visible", sprite->visible}
        };
    }
    if (const auto* light = registry.try_get<DirectionalLightComponent>(entity)) {
        entityJson["directionalLight"] = {
            {"color", Vec3ToJson(light->color)},
            {"intensity", light->intensity}
        };
    }
    if (const auto* pl = registry.try_get<PointLightComponent>(entity)) {
        entityJson["pointLight"] = {
            {"color", Vec3ToJson(pl->color)},
            {"intensity", pl->intensity},
            {"range", pl->range}
        };
    }
    if (const auto* sky = registry.try_get<SkyboxComponent>(entity)) {
        entityJson["skybox"] = {
            {"zenithColor", Vec3ToJson(sky->zenithColor)},
            {"horizonColor", Vec3ToJson(sky->horizonColor)},
            {"groundColor", Vec3ToJson(sky->groundColor)},
            {"enabled", sky->enabled}
        };
    }
    if (const auto* uiText = registry.try_get<UITextComponent>(entity)) {
        entityJson["uiText"] = {
            {"text", uiText->text},
            {"screenPos", Vec2ToJson(uiText->screenPos)},
            {"color", Vec4ToJson(uiText->color)},
            {"fontScale", uiText->fontScale},
            {"visible", uiText->visible}
        };
    }
    if (const auto* cam = registry.try_get<CameraComponent>(entity)) {
        entityJson["camera"] = {
            {"primary", cam->primary},
            {"isOrthographic", cam->isOrthographic},
            {"fov", cam->fov},
            {"orthoSize", cam->orthoSize},
            {"nearClip", cam->nearClip},
            {"farClip", cam->farClip}
        };
    }
    if (const auto* js = registry.try_get<JavaScriptComponent>(entity)) {
        entityJson["javaScript"] = {
            {"className", js->className},
            {"enabled", js->enabled}
        };
    }
    if (const auto* rb = registry.try_get<RigidBodyComponent>(entity)) {
        entityJson["rigidBody"] = {
            {"bodyType", BodyTypeToString(rb->bodyType)},
            {"shape", ShapeToString(rb->shape)},
            {"velocity", Vec3ToJson(rb->velocity)},
            {"angularVelocity", Vec3ToJson(rb->angularVelocity)},
            {"halfExtents", Vec3ToJson(rb->halfExtents)},
            {"radius", rb->radius},
            {"mass", rb->mass},
            {"restitution", rb->restitution},
            {"friction", rb->friction},
            {"useGravity", rb->useGravity},
            {"isTrigger", rb->isTrigger}
        };
    }
    if (const auto* audio = registry.try_get<AudioSourceComponent>(entity)) {
        entityJson["audioSource"] = {
            {"soundPath", audio->soundPath},
            {"volume", audio->volume},
            {"playOnStart", audio->playOnStart},
            {"playOnBounce", audio->playOnBounce}
        };
    }
    return entityJson;
}

entt::entity DeserializeEntityJson(Scene& scene, const nlohmann::json& entityJson) {
    auto& registry = scene.GetRegistry();
    const std::string tag = entityJson.value("tag", "Entity");
    entt::entity entity = scene.CreateEntity(tag);

    if (entityJson.contains("transform")) {
        const auto& trJson = entityJson["transform"];
        auto& tr = registry.get<TransformComponent>(entity);
        tr.translation = JsonToVec3(trJson.value("translation", nlohmann::json{}), tr.translation);
        tr.rotationDegrees = JsonToVec3(trJson.value("rotationDegrees", nlohmann::json{}), tr.rotationDegrees);
        tr.scale = JsonToVec3(trJson.value("scale", nlohmann::json{}), tr.scale);
    }

    if (entityJson.contains("hierarchy")) {
        const auto& hJson = entityJson["hierarchy"];
        auto& hierarchy = registry.emplace<HierarchyComponent>(entity);
        hierarchy.parentTag = hJson.value("parentTag", "");
    }

    if (entityJson.contains("meshRenderer")) {
        const auto& mJson = entityJson["meshRenderer"];
        auto& mesh = registry.emplace<MeshRendererComponent>(entity);
        mesh.type = StringToMeshType(mJson.value("type", "Cube"));
        mesh.objPath = mJson.value("objPath", "");
        mesh.materialPath = mJson.value("materialPath", "");
        mesh.texturePath = mJson.value("texturePath", "");
        mesh.color = JsonToVec4(mJson.value("color", nlohmann::json{}), mesh.color);
        mesh.emissive = JsonToVec3(mJson.value("emissive", nlohmann::json{}), mesh.emissive);
        mesh.metallic = mJson.value("metallic", mesh.metallic);
        mesh.roughness = mJson.value("roughness", mesh.roughness);
        mesh.castShadows = mJson.value("castShadows", mesh.castShadows);
        mesh.visible = mJson.value("visible", mesh.visible);
    }

    if (entityJson.contains("animator")) {
        const auto& aJson = entityJson["animator"];
        auto& anim = registry.emplace<AnimatorComponent>(entity);
        anim.currentAnimation = aJson.value("currentAnimation", anim.currentAnimation);
        anim.time = aJson.value("time", anim.time);
        anim.speed = aJson.value("speed", anim.speed);
        anim.loop = aJson.value("loop", anim.loop);
        anim.playing = aJson.value("playing", anim.playing);
        anim.autoPlayFirst = aJson.value("autoPlayFirst", anim.autoPlayFirst);
    }

    if (entityJson.contains("spriteRenderer")) {
        const auto& sJson = entityJson["spriteRenderer"];
        auto& sprite = registry.emplace<SpriteRendererComponent>(entity);
        sprite.texturePath = sJson.value("texturePath", "");
        sprite.color = JsonToVec4(sJson.value("color", nlohmann::json{}), sprite.color);
        sprite.sortingOrder = sJson.value("sortingOrder", sprite.sortingOrder);
        sprite.checkerPattern = sJson.value("checkerPattern", sprite.checkerPattern);
        sprite.hFrames = std::max(1, sJson.value("hFrames", sprite.hFrames));
        sprite.vFrames = std::max(1, sJson.value("vFrames", sprite.vFrames));
        sprite.frameIndex = std::max(0, sJson.value("frameIndex", sprite.frameIndex));
        sprite.animFps = std::max(0.0f, sJson.value("animFps", sprite.animFps));
        sprite.visible = sJson.value("visible", sprite.visible);
    }

    if (entityJson.contains("directionalLight")) {
        const auto& lJson = entityJson["directionalLight"];
        auto& light = registry.emplace<DirectionalLightComponent>(entity);
        light.color = JsonToVec3(lJson.value("color", nlohmann::json{}), light.color);
        light.intensity = lJson.value("intensity", light.intensity);
    }

    if (entityJson.contains("pointLight")) {
        const auto& plJson = entityJson["pointLight"];
        auto& pl = registry.emplace<PointLightComponent>(entity);
        pl.color = JsonToVec3(plJson.value("color", nlohmann::json{}), pl.color);
        pl.intensity = plJson.value("intensity", pl.intensity);
        pl.range = plJson.value("range", pl.range);
    }

    if (entityJson.contains("skybox")) {
        const auto& skyJson = entityJson["skybox"];
        auto& sky = registry.emplace<SkyboxComponent>(entity);
        sky.zenithColor = JsonToVec3(skyJson.value("zenithColor", nlohmann::json{}), sky.zenithColor);
        sky.horizonColor = JsonToVec3(skyJson.value("horizonColor", nlohmann::json{}), sky.horizonColor);
        sky.groundColor = JsonToVec3(skyJson.value("groundColor", nlohmann::json{}), sky.groundColor);
        sky.enabled = skyJson.value("enabled", sky.enabled);
    }

    if (entityJson.contains("uiText")) {
        const auto& uiJson = entityJson["uiText"];
        auto& ui = registry.emplace<UITextComponent>(entity);
        ui.text = uiJson.value("text", ui.text);
        ui.screenPos = JsonToVec2(uiJson.value("screenPos", nlohmann::json{}), ui.screenPos);
        ui.color = JsonToVec4(uiJson.value("color", nlohmann::json{}), ui.color);
        ui.fontScale = uiJson.value("fontScale", ui.fontScale);
        ui.visible = uiJson.value("visible", ui.visible);
    }

    if (entityJson.contains("camera")) {
        const auto& cJson = entityJson["camera"];
        auto& cam = registry.emplace<CameraComponent>(entity);
        cam.primary = cJson.value("primary", cam.primary);
        cam.isOrthographic = cJson.value("isOrthographic", cam.isOrthographic);
        cam.fov = cJson.value("fov", cam.fov);
        cam.orthoSize = cJson.value("orthoSize", cam.orthoSize);
        cam.nearClip = cJson.value("nearClip", cam.nearClip);
        cam.farClip = cJson.value("farClip", cam.farClip);
    }

    if (entityJson.contains("javaScript")) {
        const auto& jJson = entityJson["javaScript"];
        auto& js = registry.emplace<JavaScriptComponent>(entity);
        js.className = jJson.value("className", js.className);
        js.enabled = jJson.value("enabled", js.enabled);
        js.instanceCreated = false;
    }

    if (entityJson.contains("rigidBody")) {
        const auto& rJson = entityJson["rigidBody"];
        auto& rb = registry.emplace<RigidBodyComponent>(entity);
        rb.bodyType = StringToBodyType(rJson.value("bodyType", "Dynamic"));
        rb.shape = StringToShape(rJson.value("shape", "Box3D"));
        rb.velocity = JsonToVec3(rJson.value("velocity", nlohmann::json{}), rb.velocity);
        rb.angularVelocity = JsonToVec3(rJson.value("angularVelocity", nlohmann::json{}), rb.angularVelocity);
        rb.halfExtents = JsonToVec3(rJson.value("halfExtents", nlohmann::json{}), rb.halfExtents);
        rb.radius = rJson.value("radius", rb.radius);
        rb.mass = rJson.value("mass", rb.mass);
        rb.restitution = rJson.value("restitution", rb.restitution);
        rb.friction = rJson.value("friction", rb.friction);
        rb.useGravity = rJson.value("useGravity", rb.useGravity);
        rb.isTrigger = rJson.value("isTrigger", rb.isTrigger);
    }

    if (entityJson.contains("audioSource")) {
        const auto& aJson = entityJson["audioSource"];
        auto& audio = registry.emplace<AudioSourceComponent>(entity);
        audio.soundPath = aJson.value("soundPath", audio.soundPath);
        audio.volume = aJson.value("volume", audio.volume);
        audio.playOnStart = aJson.value("playOnStart", audio.playOnStart);
        audio.playOnBounce = aJson.value("playOnBounce", audio.playOnBounce);
    }

    return entity;
}

nlohmann::json BuildSceneJson(const Scene& scene) {
    nlohmann::json root;
    root["version"] = 2;
    root["entities"] = nlohmann::json::array();

    const auto& registry = scene.GetRegistry();
    std::vector<entt::entity> entities;
    for (auto entity : registry.view<TagComponent>()) {
        entities.push_back(entity);
    }
    std::reverse(entities.begin(), entities.end());

    for (entt::entity entity : entities) {
        root["entities"].push_back(SerializeEntityJson(registry, entity));
    }
    return root;
}

bool ApplySceneJson(Scene& scene, const nlohmann::json& root, const std::string& sourceLabel) {
    if (!root.contains("entities") || !root["entities"].is_array()) {
        KM_CORE_ERROR("Invalid scene format in '{}'.", sourceLabel);
        return false;
    }

    scene.Clear();
    for (const auto& entityJson : root["entities"]) {
        DeserializeEntityJson(scene, entityJson);
    }
    return true;
}

} // namespace

std::string SceneSerializer::SerializeToString(const Scene& scene) {
    return BuildSceneJson(scene).dump();
}

bool SceneSerializer::DeserializeFromString(Scene& scene, const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const std::exception& ex) {
        KM_CORE_ERROR("Failed to parse scene JSON string: {}", ex.what());
        return false;
    }
    return ApplySceneJson(scene, root, "memory");
}

bool SceneSerializer::SaveToFile(const Scene& scene, const std::filesystem::path& filepath) {
    std::error_code ec;
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path(), ec);
    }

    const nlohmann::json root = BuildSceneJson(scene);

    std::ofstream out(filepath);
    if (!out.is_open()) {
        KM_CORE_ERROR("Failed to save scene to '{}'.", filepath.string());
        return false;
    }

    out << root.dump(2) << '\n';
    KM_CORE_INFO("Scene saved to '{}'.", filepath.string());
    return true;
}

bool SceneSerializer::LoadFromFile(Scene& scene, const std::filesystem::path& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        KM_CORE_ERROR("Failed to open scene file '{}'.", filepath.string());
        return false;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        KM_CORE_ERROR("Failed to parse scene JSON '{}': {}", filepath.string(), ex.what());
        return false;
    }

    if (!ApplySceneJson(scene, root, filepath.string())) {
        return false;
    }

    KM_CORE_INFO("Scene loaded from '{}'.", filepath.string());
    return true;
}

bool SceneSerializer::SavePrefab(const Scene& scene, entt::entity entity, const std::filesystem::path& filepath) {
    if (!scene.IsValid(entity)) {
        return false;
    }
    std::error_code ec;
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path(), ec);
    }

    nlohmann::json root;
    root["type"] = "KyumistPrefab";
    root["version"] = 1;
    root["entity"] = SerializeEntityJson(scene.GetRegistry(), entity);

    std::ofstream out(filepath);
    if (!out.is_open()) {
        KM_CORE_ERROR("Failed to save prefab '{}'.", filepath.string());
        return false;
    }
    out << root.dump(2) << '\n';
    KM_CORE_INFO("Prefab saved to '{}'.", filepath.string());
    return true;
}

entt::entity SceneSerializer::LoadPrefab(Scene& scene, const std::filesystem::path& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        KM_CORE_ERROR("Failed to open prefab '{}'.", filepath.string());
        return entt::null;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception&) {
        return entt::null;
    }

    if (!root.contains("entity") || !root["entity"].is_object()) {
        return entt::null;
    }
    entt::entity spawned = DeserializeEntityJson(scene, root["entity"]);
    KM_INFO("Prefab '{}' instanciado en escena.", filepath.filename().string());
    return spawned;
}

} // namespace Kyumist
