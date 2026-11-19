#include "project/ProjectManager.hpp"

#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "renderer/ModelLoader.hpp"
#include "scene/Components.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptEngine.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace Kyumist {

namespace {

struct ProjectManagerState {
    bool hasActive = false;
    ProjectConfig activeConfig{};
    std::vector<RecentProjectEntry> recents;
    bool recentsLoaded = false;
};

ProjectManagerState s_Proj;

std::filesystem::path ResolveDefaultAssetsDir() {
    return PlatformPaths::ResolveAssetsDir();
}

std::filesystem::path ResolveExamplesDir() {
    std::error_code ec;
    const std::filesystem::path assetsDir = ResolveDefaultAssetsDir();
    const std::filesystem::path absAssets = std::filesystem::weakly_canonical(assetsDir, ec);
    const std::filesystem::path baseAssets = ec ? assetsDir : absAssets;
    if (baseAssets.has_parent_path()) {
        const std::filesystem::path parent = baseAssets.parent_path();
        if (PlatformPaths::IsDirectoryWritable(parent)) {
            return parent / "examples";
        }
    }
    return PlatformPaths::GetUserDataDir() / "examples";
}

std::string NormalizePathString(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
    return (ec ? p : canon).lexically_normal().string();
}

bool IsDemoOrSampleProjectEntry(const std::string& name, const std::filesystem::path& projectFile) {
    if (name == "Kyumist Demo Project") {
        return true;
    }
    const std::string normTarget = NormalizePathString(projectFile);
    const std::string legacyDemo = NormalizePathString(ResolveDefaultAssetsDir() / "project.kmproj");
    if (normTarget == legacyDemo) {
        return true;
    }
    const std::string examplesRoot = NormalizePathString(ResolveExamplesDir());
    if (!examplesRoot.empty() && normTarget.rfind(examplesRoot, 0) == 0) {
        return true;
    }
    return false;
}

std::filesystem::path ResolveRecentProjectsFile() {
    std::error_code ec;
#ifdef KM_ASSETS_DIR
    const std::filesystem::path devRoot = std::filesystem::path(KM_ASSETS_DIR).parent_path();
    const std::filesystem::path devBuild = devRoot / "build";
    if (std::filesystem::exists(devBuild, ec) && PlatformPaths::IsDirectoryWritable(devBuild)) {
        return devBuild / "kyumist_projects.json";
    }
#endif
    if (std::filesystem::exists("build", ec) && PlatformPaths::IsDirectoryWritable("build")) {
        return std::filesystem::path("build/kyumist_projects.json");
    }
    return PlatformPaths::GetUserDataDir() / "kyumist_projects.json";
}

void SaveRecentProjects();

void LoadRecentProjects() {
    if (s_Proj.recentsLoaded) {
        return;
    }
    s_Proj.recentsLoaded = true;
    s_Proj.recents.clear();

    const std::filesystem::path recentsFile = ResolveRecentProjectsFile();
    if (!std::filesystem::exists(recentsFile)) {
        return;
    }

    std::ifstream in(recentsFile);
    if (!in.is_open()) {
        return;
    }
    try {
        nlohmann::json root;
        in >> root;
        bool pruned = false;
        if (root.contains("projects") && root["projects"].is_array()) {
            for (const auto& item : root["projects"]) {
                RecentProjectEntry entry;
                entry.name = item.value("name", "");
                entry.projectFilePath = item.value("path", "");
                if (!entry.projectFilePath.empty()) {
                    if (IsDemoOrSampleProjectEntry(entry.name, entry.projectFilePath)) {
                        pruned = true;
                        continue;
                    }
                    s_Proj.recents.push_back(std::move(entry));
                }
            }
        }
        if (pruned) {
            SaveRecentProjects();
        }
    } catch (const std::exception&) {
        s_Proj.recents.clear();
    }
}

void SaveRecentProjects() {
    const std::filesystem::path recentsFile = ResolveRecentProjectsFile();
    std::error_code ec;
    if (recentsFile.has_parent_path()) {
        std::filesystem::create_directories(recentsFile.parent_path(), ec);
    }

    nlohmann::json root;
    root["projects"] = nlohmann::json::array();
    for (const auto& entry : s_Proj.recents) {
        root["projects"].push_back({
            {"name", entry.name},
            {"path", entry.projectFilePath}
        });
    }

    std::ofstream out(recentsFile);
    if (out.is_open()) {
        out << root.dump(2) << '\n';
    }
}

void RegisterRecentProject(const std::string& name, const std::filesystem::path& projectFile) {
    LoadRecentProjects();
    if (IsDemoOrSampleProjectEntry(name, projectFile)) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path absPath = std::filesystem::weakly_canonical(projectFile, ec);
    const std::string pathStr = ec ? projectFile.string() : absPath.string();

    s_Proj.recents.erase(
        std::remove_if(s_Proj.recents.begin(), s_Proj.recents.end(), [&](const RecentProjectEntry& e) {
            return e.projectFilePath == pathStr;
        }),
        s_Proj.recents.end()
    );
    s_Proj.recents.insert(s_Proj.recents.begin(), RecentProjectEntry{name, pathStr});
    if (s_Proj.recents.size() > 12) {
        s_Proj.recents.resize(12);
    }
    SaveRecentProjects();
}

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path);
    if (out.is_open()) {
        out << content;
    }
}

void BuildHybridSanctuaryProjectFiles(Scene& scene, const std::filesystem::path& projectDir) {
    auto& reg = scene.GetRegistry();
    scene.Clear();

    {
        entt::entity light = scene.CreateEntity("Sol Santuario");
        auto& tr = reg.get<TransformComponent>(light);
        tr.translation = {0.0f, 7.0f, 0.0f};
        tr.rotationDegrees = {-42.0f, -30.0f, 0.0f};
        reg.emplace<DirectionalLightComponent>(light).intensity = 1.35f;
        reg.emplace<SkyboxComponent>(light);
    }

    {
        entt::entity altarLight = scene.CreateEntity("Luz Altar");
        reg.get<TransformComponent>(altarLight).translation = {0.0f, 3.0f, -2.5f};
        auto& pl = reg.emplace<PointLightComponent>(altarLight);
        pl.color = {1.0f, 0.84f, 0.45f};
        pl.intensity = 2.8f;
        pl.range = 10.0f;
    }

    {
        entt::entity cam = scene.CreateEntity("Camara Santuario");
        auto& tr = reg.get<TransformComponent>(cam);
        tr.translation = {0.0f, 4.8f, 9.2f};
        tr.rotationDegrees = {-22.0f, 0.0f, 0.0f};
        auto& cc = reg.emplace<CameraComponent>(cam);
        cc.primary = true;
        cc.isOrthographic = false;
        cc.fov = 46.0f;
    }

    {
        entt::entity floor = scene.CreateEntity("Suelo Santuario");
        auto& tr = reg.get<TransformComponent>(floor);
        tr.translation = {0.0f, 0.0f, 0.0f};
        tr.scale = {14.0f, 1.0f, 14.0f};
        auto& mesh = reg.emplace<MeshRendererComponent>(floor);
        mesh.type = MeshType::Plane;
        mesh.texturePath = "textures/checker_albedo.png";
        mesh.color = {0.48f, 0.54f, 0.62f, 1.0f};
        mesh.roughness = 0.80f;
        auto& rb = reg.emplace<RigidBodyComponent>(floor);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {7.0f, 0.05f, 7.0f};
        rb.restitution = 0.0f;
    }

    {
        entt::entity altar = scene.CreateEntity("Altar Central");
        auto& tr = reg.get<TransformComponent>(altar);
        tr.translation = {0.0f, 0.3f, -2.5f};
        tr.scale = {2.4f, 0.6f, 2.4f};
        auto& mesh = reg.emplace<MeshRendererComponent>(altar);
        mesh.type = MeshType::Cube;
        mesh.materialPath = "materials/gold_pbr.kmmat";
        mesh.color = {0.62f, 0.66f, 0.74f, 1.0f};
        mesh.metallic = 0.25f;
        mesh.roughness = 0.45f;
        auto& rb = reg.emplace<RigidBodyComponent>(altar);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {1.2f, 0.3f, 1.2f};
        rb.restitution = 0.0f;
    }

    {
        entt::entity totem = scene.CreateEntity("Totem Cristal");
        auto& tr = reg.get<TransformComponent>(totem);
        tr.translation = {0.0f, 1.45f, -2.5f};
        tr.scale = {0.95f, 0.95f, 0.95f};
        auto& mesh = reg.emplace<MeshRendererComponent>(totem);
        mesh.type = MeshType::CustomOBJ;
        mesh.objPath = "models/crystal_totem.gltf";
        mesh.color = {0.42f, 0.90f, 1.0f, 1.0f};
        mesh.emissive = {0.12f, 0.35f, 0.55f};
        mesh.metallic = 0.40f;
        mesh.roughness = 0.20f;
        auto& rb = reg.emplace<RigidBodyComponent>(totem);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {0.55f, 1.0f, 0.55f};
        rb.restitution = 0.0f;
        reg.emplace<JavaScriptComponent>(totem, JavaScriptComponent{"TotemPulse3DScript", true, false});
    }

    const glm::vec3 orbPos[3] = {
        { 3.0f, 0.88f, -2.5f},
        {-1.5f, 0.88f,  0.1f},
        {-1.5f, 0.88f, -5.1f}
    };
    for (int i = 0; i < 3; ++i) {
        entt::entity orb = scene.CreateEntity("Orbe_Guardian_" + std::to_string(i + 1));
        auto& tr = reg.get<TransformComponent>(orb);
        tr.translation = orbPos[i];
        tr.scale = {0.72f, 0.72f, 0.72f};
        auto& mesh = reg.emplace<MeshRendererComponent>(orb);
        mesh.type = MeshType::Sphere;
        mesh.materialPath = "materials/gold_pbr.kmmat";
        mesh.color = {0.98f, 0.42f, 0.24f, 1.0f};
        mesh.emissive = {0.35f, 0.12f, 0.05f};
        mesh.metallic = 0.80f;
        mesh.roughness = 0.22f;
        auto& rb = reg.emplace<RigidBodyComponent>(orb);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Sphere3D;
        rb.radius = 0.50f;
        rb.isTrigger = false;
        reg.emplace<JavaScriptComponent>(orb, JavaScriptComponent{"GuardianOrb3DScript", true, false});
    }

    const glm::vec3 runePos[3] = {{-4.0f, 0.85f, 1.2f}, {4.0f, 0.85f, 1.2f}, {0.0f, 1.30f, 0.0f}};
    for (int i = 0; i < 3; ++i) {
        entt::entity rune = scene.CreateEntity("Runa_2D_" + std::to_string(i + 1));
        auto& tr = reg.get<TransformComponent>(rune);
        tr.translation = runePos[i];
        tr.scale = {0.9f, 0.9f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(rune);
        spr.texturePath = "textures/coin_spritesheet.png";
        spr.color = {0.45f, 1.0f, 0.85f, 1.0f};
        spr.hFrames = 4;
        spr.vFrames = 1;
        spr.animFps = 8.0f;
        auto& rb = reg.emplace<RigidBodyComponent>(rune);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {0.45f, 0.50f, 0.45f};
        rb.isTrigger = true;
    }

    {
        entt::entity hero = scene.CreateEntity("Heroe 2D");
        auto& tr = reg.get<TransformComponent>(hero);
        tr.translation = {0.0f, 0.85f, 3.6f};
        tr.scale = {1.15f, 1.35f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(hero);
        spr.texturePath = "textures/checker_albedo.png";
        spr.color = {0.98f, 0.48f, 0.34f, 1.0f};
        spr.sortingOrder = 4;
        auto& rb = reg.emplace<RigidBodyComponent>(hero);
        rb.bodyType = RigidBodyType::Dynamic;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {0.55f, 0.65f, 0.45f};
        rb.restitution = 0.0f;
        rb.friction = 0.35f;
        rb.useGravity = true;
        auto& audio = reg.emplace<AudioSourceComponent>(hero);
        audio.soundPath = "audio/bounce.wav";
        audio.playOnBounce = false;
        reg.emplace<JavaScriptComponent>(hero, JavaScriptComponent{"HybridHeroScript", true, false});
    }

    {
        entt::entity hud = scene.CreateEntity("HUD_Hybrid");
        auto& ui = reg.emplace<UITextComponent>(hud);
        ui.text = "Aether Guardian (3D+2D) | Vidas: 3 | Runas 2D: 0/3 | WASD: Mover | Espacio: Saltar";
        ui.screenPos = {22.0f, 54.0f};
        ui.color = {0.95f, 0.98f, 1.0f, 1.0f};
        ui.fontScale = 1.15f;
    }

    WriteTextFile(
        projectDir / "scripts" / "HybridHeroScript.java",
        "import dev.kyumist.Audio;\n"
        "import dev.kyumist.Entity;\n"
        "import dev.kyumist.Input;\n"
        "import dev.kyumist.KyumistScript;\n\n"
        "public class HybridHeroScript extends KyumistScript {\n"
        "    private boolean grounded = false;\n"
        "    private float jumpCooldown = 0.0f;\n"
        "    private float hitCooldown = 0.0f;\n"
        "    private int runesCollected = 0;\n"
        "    private int lives = 3;\n"
        "    private final float[] vel = new float[3];\n"
        "    private Entity hudEntity;\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        hudEntity = findEntity(\"HUD_Hybrid\");\n"
        "        updateHud();\n"
        "        logInfo(\"Aether Guardian (Hibrido 3D+2D) iniciado en entidad \" + entity.getId());\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        entity.getVelocity(vel);\n"
        "        float vx = 0.0f;\n"
        "        float vz = 0.0f;\n"
        "        if (Input.isKeyDown(Input.KEY_A) || Input.isKeyDown(Input.KEY_LEFT))  vx -= 4.8f;\n"
        "        if (Input.isKeyDown(Input.KEY_D) || Input.isKeyDown(Input.KEY_RIGHT)) vx += 4.8f;\n"
        "        if (Input.isKeyDown(Input.KEY_W) || Input.isKeyDown(Input.KEY_UP))    vz -= 4.8f;\n"
        "        if (Input.isKeyDown(Input.KEY_S) || Input.isKeyDown(Input.KEY_DOWN))  vz += 4.8f;\n\n"
        "        if (jumpCooldown > 0.0f) {\n"
        "            jumpCooldown -= dt;\n"
        "        }\n"
        "        if (hitCooldown > 0.0f) {\n"
        "            hitCooldown -= dt;\n"
        "            if (hitCooldown <= 0.0f) {\n"
        "                entity.setColor(0.98f, 0.48f, 0.34f, 1.0f);\n"
        "            }\n"
        "        }\n\n"
        "        float vy = vel[1];\n"
        "        if (Input.isKeyDown(Input.KEY_SPACE) && (grounded || Math.abs(vy) < 0.25f) && jumpCooldown <= 0.0f) {\n"
        "            vy = 6.8f;\n"
        "            grounded = false;\n"
        "            jumpCooldown = 0.25f;\n"
        "            Audio.playSound(\"audio/bounce.wav\", 0.5f);\n"
        "        }\n\n"
        "        entity.setVelocity(vx, vy, vz);\n\n"
        "        if (entity.getY() < -5.0f) {\n"
        "            entity.setPosition(0.0f, 1.5f, 3.6f);\n"
        "            entity.setVelocity(0.0f, 0.0f, 0.0f);\n"
        "        }\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onCollisionEnter(Entity other) {\n"
        "        String otherName = other.getName();\n"
        "        if (otherName.equals(\"Suelo Santuario\") || otherName.equals(\"Altar Central\") || otherName.equals(\"Totem Cristal\")) {\n"
        "            grounded = true;\n"
        "        } else if (otherName.startsWith(\"Runa_2D_\")) {\n"
        "            other.setPosition(0.0f, -60.0f, 0.0f);\n"
        "            runesCollected++;\n"
        "            Audio.playSound(\"audio/bounce.wav\", 0.95f);\n"
        "            updateHud();\n"
        "        } else if (otherName.startsWith(\"Orbe_Guardian_\") && hitCooldown <= 0.0f) {\n"
        "            lives--;\n"
        "            hitCooldown = 1.2f;\n"
        "            entity.setColor(1.0f, 0.16f, 0.16f, 1.0f);\n"
        "            Audio.playSound(\"audio/bounce.wav\", 1.0f);\n"
        "            if (lives <= 0) {\n"
        "                lives = 3;\n"
        "                runesCollected = 0;\n"
        "                resetRunes();\n"
        "            }\n"
        "            entity.setPosition(0.0f, 1.1f, 3.8f);\n"
        "            entity.setVelocity(0.0f, 0.0f, 0.0f);\n"
        "            updateHud();\n"
        "        }\n"
        "    }\n\n"
        "    private void resetRunes() {\n"
        "        Entity r1 = findEntity(\"Runa_2D_1\"); if (r1 != null) r1.setPosition(-4.0f, 0.85f, 1.2f);\n"
        "        Entity r2 = findEntity(\"Runa_2D_2\"); if (r2 != null) r2.setPosition(4.0f, 0.85f, 1.2f);\n"
        "        Entity r3 = findEntity(\"Runa_2D_3\"); if (r3 != null) r3.setPosition(0.0f, 1.30f, 0.0f);\n"
        "    }\n\n"
        "    private void updateHud() {\n"
        "        if (hudEntity == null) {\n"
        "            hudEntity = findEntity(\"HUD_Hybrid\");\n"
        "        }\n"
        "        if (hudEntity != null) {\n"
        "            if (runesCollected >= 3) {\n"
        "                hudEntity.setUIText(\"Aether Guardian | SANTUARIO ACTIVADO! Vidas: \" + lives + \" | Runas: 3/3\");\n"
        "            } else {\n"
        "                hudEntity.setUIText(\"Aether Guardian (3D+2D) | Vidas: \" + lives + \" | Runas 2D: \" + runesCollected + \"/3 | WASD: Mover | Espacio: Saltar\");\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n"
    );

    WriteTextFile(
        projectDir / "scripts" / "TotemPulse3DScript.java",
        "import dev.kyumist.KyumistScript;\n\n"
        "public class TotemPulse3DScript extends KyumistScript {\n"
        "    private float elapsed = 0.0f;\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        elapsed += dt;\n"
        "        entity.rotate(0.0f, 55.0f * dt, 0.0f);\n"
        "        entity.setPosition(0.0f, 1.45f + (float) Math.sin(elapsed * 2.2) * 0.16f, -2.5f);\n"
        "    }\n"
        "}\n"
    );

    WriteTextFile(
        projectDir / "scripts" / "GuardianOrb3DScript.java",
        "import dev.kyumist.KyumistScript;\n\n"
        "public class GuardianOrb3DScript extends KyumistScript {\n"
        "    private float angle = 0.0f;\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        String name = entity.getName();\n"
        "        if (name.endsWith(\"2\")) {\n"
        "            angle = 2.094395f;\n"
        "        } else if (name.endsWith(\"3\")) {\n"
        "            angle = 4.18879f;\n"
        "        }\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        angle += 1.45f * dt;\n"
        "        float ox = (float) Math.cos(angle) * 3.0f;\n"
        "        float oz = -2.5f + (float) Math.sin(angle) * 3.0f;\n"
        "        float oy = 0.88f + (float) Math.sin(angle * 2.0) * 0.12f;\n"
        "        entity.setPosition(ox, oy, oz);\n"
        "        entity.rotate(0.0f, 90.0f * dt, 0.0f);\n"
        "    }\n"
        "}\n"
    );
}

void BuildShooter3DProjectFiles(Scene& scene, const std::filesystem::path& projectDir) {
    auto& reg = scene.GetRegistry();
    scene.Clear();

    {
        entt::entity light = scene.CreateEntity("Sol Arena");
        auto& tr = reg.get<TransformComponent>(light);
        tr.translation = {0.0f, 8.0f, 0.0f};
        tr.rotationDegrees = {-48.0f, -32.0f, 0.0f};
        reg.emplace<DirectionalLightComponent>(light).intensity = 1.35f;
        auto& sky = reg.emplace<SkyboxComponent>(light);
        sky.zenithColor = {0.18f, 0.38f, 0.72f};
        sky.horizonColor = {0.55f, 0.74f, 0.92f};
        sky.groundColor = {0.28f, 0.32f, 0.38f};
    }

    {
        entt::entity centerLight = scene.CreateEntity("Luz Central");
        reg.get<TransformComponent>(centerLight).translation = {0.0f, 3.2f, 0.0f};
        auto& pl = reg.emplace<PointLightComponent>(centerLight);
        pl.color = {0.40f, 0.85f, 1.0f};
        pl.intensity = 2.4f;
        pl.range = 14.0f;
    }

    {
        entt::entity cam = scene.CreateEntity("Camara Arena");
        auto& tr = reg.get<TransformComponent>(cam);
        tr.translation = {0.0f, 10.5f, 13.0f};
        tr.rotationDegrees = {-40.0f, 0.0f, 0.0f};
        auto& cc = reg.emplace<CameraComponent>(cam);
        cc.primary = true;
        cc.isOrthographic = false;
        cc.fov = 50.0f;
    }

    {
        entt::entity floor = scene.CreateEntity("Suelo Arena");
        auto& tr = reg.get<TransformComponent>(floor);
        tr.translation = {0.0f, 0.0f, 0.0f};
        tr.scale = {18.0f, 1.0f, 18.0f};
        auto& mesh = reg.emplace<MeshRendererComponent>(floor);
        mesh.type = MeshType::Plane;
        mesh.texturePath = "textures/checker_albedo.png";
        mesh.color = {0.44f, 0.50f, 0.58f, 1.0f};
        mesh.roughness = 0.75f;
        auto& rb = reg.emplace<RigidBodyComponent>(floor);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {9.0f, 0.05f, 9.0f};
    }

    const glm::vec3 pillars[4] = {
        {-4.0f, 0.9f, -0.5f},
        { 4.0f, 0.9f, -0.5f},
        {-4.0f, 0.9f,  3.5f},
        { 4.0f, 0.9f,  3.5f}
    };
    for (int i = 0; i < 4; ++i) {
        entt::entity p = scene.CreateEntity("Pilar_" + std::to_string(i + 1));
        auto& tr = reg.get<TransformComponent>(p);
        tr.translation = pillars[i];
        tr.scale = {1.1f, 1.8f, 1.1f};
        auto& mesh = reg.emplace<MeshRendererComponent>(p);
        mesh.type = MeshType::Cube;
        mesh.color = {0.34f, 0.40f, 0.52f, 1.0f};
        mesh.roughness = 0.60f;
        auto& rb = reg.emplace<RigidBodyComponent>(p);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {0.55f, 0.90f, 0.55f};
    }

    {
        entt::entity player = scene.CreateEntity("Jugador");
        auto& tr = reg.get<TransformComponent>(player);
        tr.translation = {0.0f, 0.55f, 4.5f};
        tr.scale = {1.1f, 0.7f, 1.3f};
        auto& mesh = reg.emplace<MeshRendererComponent>(player);
        mesh.type = MeshType::Cube;
        mesh.color = {0.20f, 0.72f, 0.96f, 1.0f};
        mesh.metallic = 0.35f;
        mesh.roughness = 0.35f;
        auto& rb = reg.emplace<RigidBodyComponent>(player);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Box3D;
        rb.halfExtents = {0.55f, 0.35f, 0.65f};
        reg.emplace<JavaScriptComponent>(player, JavaScriptComponent{"ShooterPlayer3DScript", true, false});
    }

    {
        entt::entity barrel = scene.CreateEntity("Canon_Torreta");
        auto& tr = reg.get<TransformComponent>(barrel);
        tr.translation = {0.0f, 0.25f, -0.75f};
        tr.scale = {0.30f, 0.28f, 1.10f};
        auto& mesh = reg.emplace<MeshRendererComponent>(barrel);
        mesh.type = MeshType::Cube;
        mesh.color = {0.85f, 0.90f, 0.98f, 1.0f};
        mesh.metallic = 0.80f;
        mesh.roughness = 0.20f;
        reg.emplace<HierarchyComponent>(barrel, HierarchyComponent{"Jugador"});
    }

    {
        entt::entity laser = scene.CreateEntity("LaserBeam");
        auto& tr = reg.get<TransformComponent>(laser);
        tr.translation = {0.0f, -50.0f, 0.0f};
        tr.scale = {0.14f, 0.14f, 4.2f};
        auto& mesh = reg.emplace<MeshRendererComponent>(laser);
        mesh.type = MeshType::Cube;
        mesh.color = {0.25f, 0.98f, 1.0f, 1.0f};
        mesh.emissive = {0.40f, 1.0f, 1.0f};
        mesh.castShadows = false;
    }

    struct DroneSpec {
        const char* tag;
        glm::vec3 pos;
        MeshType meshType;
        std::string objPath;
        glm::vec4 color;
    };
    const DroneSpec drones[3] = {
        {"Dron_1", {-4.5f, 0.9f, -3.5f}, MeshType::Cube, "", {0.95f, 0.28f, 0.26f, 1.0f}},
        {"Dron_2", {0.0f, 0.95f, -5.5f}, MeshType::Sphere, "", {0.98f, 0.58f, 0.18f, 1.0f}},
        {"Dron_3", {4.5f, 0.95f, -3.5f}, MeshType::CustomOBJ, "models/pyramid.obj", {0.72f, 0.36f, 0.98f, 1.0f}}
    };
    for (const auto& d : drones) {
        entt::entity drone = scene.CreateEntity(d.tag);
        auto& tr = reg.get<TransformComponent>(drone);
        tr.translation = d.pos;
        auto& mesh = reg.emplace<MeshRendererComponent>(drone);
        mesh.type = d.meshType;
        mesh.objPath = d.objPath;
        mesh.color = d.color;
        mesh.roughness = 0.30f;
        auto& rb = reg.emplace<RigidBodyComponent>(drone);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Sphere3D;
        rb.radius = 0.65f;
        rb.angularVelocity = {0.0f, 75.0f, 0.0f};
        rb.isTrigger = false;
        reg.emplace<JavaScriptComponent>(drone, JavaScriptComponent{"EnemyDrone3DScript", true, false});
    }

    {
        entt::entity hud = scene.CreateEntity("HUD_Shooter");
        auto& ui = reg.emplace<UITextComponent>(hud);
        ui.text = "Vortex Arena 3D | Vidas: 3 | Bajas: 0 | WASD: Mover | Q/E: Girar | Espacio/Clic: Disparar";
        ui.screenPos = {22.0f, 54.0f};
        ui.color = {0.95f, 0.98f, 1.0f, 1.0f};
        ui.fontScale = 1.15f;
    }

    WriteTextFile(
        projectDir / "scripts" / "ShooterPlayer3DScript.java",
        "import dev.kyumist.Audio;\n"
        "import dev.kyumist.Entity;\n"
        "import dev.kyumist.Input;\n"
        "import dev.kyumist.KyumistScript;\n\n"
        "public class ShooterPlayer3DScript extends KyumistScript {\n"
        "    private float yawDeg = 0.0f;\n"
        "    private float fireCooldown = 0.0f;\n"
        "    private float laserTimer = 0.0f;\n"
        "    private float hitCooldown = 0.0f;\n"
        "    private int kills = 0;\n"
        "    private int lives = 3;\n"
        "    private Entity laserEntity;\n"
        "    private Entity hudEntity;\n"
        "    private final float[] pos = new float[3];\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        float[] rot = new float[3];\n"
        "        entity.getRotation(rot);\n"
        "        yawDeg = rot[1];\n"
        "        laserEntity = findEntity(\"LaserBeam\");\n"
        "        hudEntity = findEntity(\"HUD_Shooter\");\n"
        "        updateHud();\n"
        "        logInfo(\"Vortex Arena 3D iniciado en entidad \" + entity.getId());\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        if (Input.isKeyDown(Input.KEY_Q) || Input.isKeyDown(Input.KEY_LEFT)) {\n"
        "            yawDeg += 130.0f * dt;\n"
        "        }\n"
        "        if (Input.isKeyDown(Input.KEY_E) || Input.isKeyDown(Input.KEY_RIGHT)) {\n"
        "            yawDeg -= 130.0f * dt;\n"
        "        }\n"
        "        entity.setRotation(0.0f, yawDeg, 0.0f);\n\n"
        "        double rad = Math.toRadians(yawDeg);\n"
        "        float fwdX = (float) -Math.sin(rad);\n"
        "        float fwdZ = (float) -Math.cos(rad);\n"
        "        float rightX = (float) Math.cos(rad);\n"
        "        float rightZ = (float) -Math.sin(rad);\n\n"
        "        float moveFwd = 0.0f;\n"
        "        float moveRight = 0.0f;\n"
        "        if (Input.isKeyDown(Input.KEY_W) || Input.isKeyDown(Input.KEY_UP))   moveFwd += 1.0f;\n"
        "        if (Input.isKeyDown(Input.KEY_S) || Input.isKeyDown(Input.KEY_DOWN)) moveFwd -= 1.0f;\n"
        "        if (Input.isKeyDown(Input.KEY_D)) moveRight += 1.0f;\n"
        "        if (Input.isKeyDown(Input.KEY_A)) moveRight -= 1.0f;\n\n"
        "        entity.getPosition(pos);\n"
        "        float speed = 6.2f * dt;\n"
        "        float nx = clamp(pos[0] + (fwdX * moveFwd + rightX * moveRight) * speed, -7.8f, 7.8f);\n"
        "        float nz = clamp(pos[2] + (fwdZ * moveFwd + rightZ * moveRight) * speed, -7.8f, 7.8f);\n"
        "        entity.setPosition(nx, pos[1], nz);\n\n"
        "        if (fireCooldown > 0.0f) {\n"
        "            fireCooldown -= dt;\n"
        "        }\n"
        "        if (hitCooldown > 0.0f) {\n"
        "            hitCooldown -= dt;\n"
        "            if (hitCooldown <= 0.0f) {\n"
        "                entity.setColor(0.20f, 0.72f, 0.96f, 1.0f);\n"
        "            }\n"
        "        }\n"
        "        if (laserTimer > 0.0f) {\n"
        "            laserTimer -= dt;\n"
        "            if (laserTimer <= 0.0f && laserEntity != null) {\n"
        "                laserEntity.setPosition(0.0f, -50.0f, 0.0f);\n"
        "            }\n"
        "        }\n\n"
        "        boolean shootPressed = Input.isKeyDown(Input.KEY_SPACE) || Input.isMouseButtonDown(Input.MOUSE_LEFT);\n"
        "        if (shootPressed && fireCooldown <= 0.0f) {\n"
        "            fireCooldown = 0.24f;\n"
        "            shootLaser(nx, pos[1] + 0.25f, nz, fwdX, fwdZ);\n"
        "        }\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onCollisionEnter(Entity other) {\n"
        "        String otherName = other.getName();\n"
        "        if (otherName.startsWith(\"Dron_\") && hitCooldown <= 0.0f) {\n"
        "            lives--;\n"
        "            hitCooldown = 1.2f;\n"
        "            entity.setColor(1.0f, 0.18f, 0.18f, 1.0f);\n"
        "            Audio.playSound(\"audio/bounce.wav\", 1.0f);\n"
        "            other.setPosition(-5.2f, 0.9f, -5.8f);\n"
        "            if (lives <= 0) {\n"
        "                lives = 3;\n"
        "                kills = 0;\n"
        "                entity.setPosition(0.0f, 0.55f, 4.5f);\n"
        "            }\n"
        "            updateHud();\n"
        "        }\n"
        "    }\n\n"
        "    private void shootLaser(float px, float py, float pz, float fwdX, float fwdZ) {\n"
        "        Audio.playSound(\"audio/bounce.wav\", 0.65f);\n"
        "        if (laserEntity == null) {\n"
        "            laserEntity = findEntity(\"LaserBeam\");\n"
        "        }\n"
        "        if (laserEntity != null) {\n"
        "            laserEntity.setRotation(0.0f, yawDeg, 0.0f);\n"
        "            laserEntity.setPosition(px + fwdX * 3.0f, py, pz + fwdZ * 3.0f);\n"
        "            laserTimer = 0.07f;\n"
        "        }\n\n"
        "        Entity hit = raycast(px + fwdX * 0.9f, py, pz + fwdZ * 0.9f, fwdX, 0.0f, fwdZ, 26.0f);\n"
        "        if (hit != null && hit.getName().startsWith(\"Dron_\")) {\n"
        "            kills++;\n"
        "            Audio.playSound(\"audio/bounce.wav\", 1.0f);\n"
        "            float angle = kills * 1.9f;\n"
        "            float rx = (float) Math.cos(angle) * 5.6f;\n"
        "            float rz = -Math.abs((float) Math.sin(angle) * 5.2f) - 1.5f;\n"
        "            hit.setPosition(rx, 0.9f, rz);\n"
        "            updateHud();\n"
        "        }\n"
        "    }\n\n"
        "    private void updateHud() {\n"
        "        if (hudEntity == null) {\n"
        "            hudEntity = findEntity(\"HUD_Shooter\");\n"
        "        }\n"
        "        if (hudEntity != null) {\n"
        "            hudEntity.setUIText(\"Vortex Arena 3D | Vidas: \" + lives + \" | Bajas: \" + kills + \" | WASD: Mover | Q/E: Girar | Espacio/Clic: Disparar\");\n"
        "        }\n"
        "    }\n\n"
        "    private static float clamp(float val, float minV, float maxV) {\n"
        "        return Math.max(minV, Math.min(maxV, val));\n"
        "    }\n"
        "}\n"
    );

    WriteTextFile(
        projectDir / "scripts" / "EnemyDrone3DScript.java",
        "import dev.kyumist.Entity;\n"
        "import dev.kyumist.KyumistScript;\n\n"
        "public class EnemyDrone3DScript extends KyumistScript {\n"
        "    private float elapsed = 0.0f;\n"
        "    private float speed = 1.85f;\n"
        "    private final float[] pos = new float[3];\n"
        "    private final float[] targetPos = new float[3];\n"
        "    private Entity player;\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        player = findEntity(\"Jugador\");\n"
        "        String name = entity.getName();\n"
        "        if (name.endsWith(\"2\")) {\n"
        "            speed = 2.25f;\n"
        "        } else if (name.endsWith(\"3\")) {\n"
        "            speed = 2.60f;\n"
        "        }\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        elapsed += dt;\n"
        "        entity.getPosition(pos);\n"
        "        if (player == null) {\n"
        "            player = findEntity(\"Jugador\");\n"
        "        }\n"
        "        float nx = pos[0];\n"
        "        float nz = pos[2];\n"
        "        if (player != null) {\n"
        "            player.getPosition(targetPos);\n"
        "            float dx = targetPos[0] - pos[0];\n"
        "            float dz = targetPos[2] - pos[2];\n"
        "            float len = (float) Math.sqrt(dx * dx + dz * dz);\n"
        "            if (len > 0.05f) {\n"
        "                nx += (dx / len) * speed * dt;\n"
        "                nz += (dz / len) * speed * dt;\n"
        "            }\n"
        "        }\n"
        "        float ny = 0.72f + (float) Math.sin(elapsed * 3.4) * 0.14f;\n"
        "        entity.setPosition(nx, ny, nz);\n"
        "    }\n"
        "}\n"
    );
}

void BuildPlatformer2DProjectFiles(Scene& scene, const std::filesystem::path& projectDir) {
    auto& reg = scene.GetRegistry();
    scene.Clear();

    {
        entt::entity cam = scene.CreateEntity("Camara 2D");
        auto& tr = reg.get<TransformComponent>(cam);
        tr.translation = {0.0f, 1.5f, 10.0f};
        auto& cc = reg.emplace<CameraComponent>(cam);
        cc.primary = true;
        cc.isOrthographic = true;
        cc.orthoSize = 9.0f;
    }

    {
        entt::entity ground = scene.CreateEntity("Suelo Base");
        auto& tr = reg.get<TransformComponent>(ground);
        tr.translation = {0.0f, -2.5f, 0.0f};
        tr.scale = {16.0f, 0.8f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(ground);
        spr.color = {0.32f, 0.38f, 0.48f, 1.0f};
        spr.checkerPattern = true;
        auto& rb = reg.emplace<RigidBodyComponent>(ground);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box2D;
        rb.halfExtents = {8.0f, 0.4f, 0.5f};
        rb.restitution = 0.0f;
        rb.friction = 0.4f;
    }

    {
        entt::entity p1 = scene.CreateEntity("Plat_Izquierda");
        auto& tr = reg.get<TransformComponent>(p1);
        tr.translation = {-4.0f, -0.5f, 0.0f};
        tr.scale = {3.2f, 0.5f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(p1);
        spr.color = {0.35f, 0.55f, 0.82f, 1.0f};
        spr.checkerPattern = true;
        auto& rb = reg.emplace<RigidBodyComponent>(p1);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box2D;
        rb.halfExtents = {1.6f, 0.25f, 0.5f};
        rb.restitution = 0.0f;
    }

    {
        entt::entity pMov = scene.CreateEntity("Plat_Movil");
        auto& tr = reg.get<TransformComponent>(pMov);
        tr.translation = {0.0f, 0.8f, 0.0f};
        tr.scale = {2.8f, 0.45f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(pMov);
        spr.color = {0.92f, 0.62f, 0.22f, 1.0f};
        auto& rb = reg.emplace<RigidBodyComponent>(pMov);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Box2D;
        rb.halfExtents = {1.4f, 0.225f, 0.5f};
        rb.restitution = 0.0f;
        reg.emplace<JavaScriptComponent>(pMov, JavaScriptComponent{"MovingPlatform2DScript", true, false});
    }

    {
        entt::entity p2 = scene.CreateEntity("Plat_Derecha");
        auto& tr = reg.get<TransformComponent>(p2);
        tr.translation = {4.2f, 2.1f, 0.0f};
        tr.scale = {3.4f, 0.5f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(p2);
        spr.color = {0.35f, 0.72f, 0.55f, 1.0f};
        spr.checkerPattern = true;
        auto& rb = reg.emplace<RigidBodyComponent>(p2);
        rb.bodyType = RigidBodyType::Static;
        rb.shape = ColliderShape::Box2D;
        rb.halfExtents = {1.7f, 0.25f, 0.5f};
        rb.restitution = 0.0f;
    }

    const glm::vec3 coinPos[3] = {{-4.0f, 0.5f, 0.0f}, {0.0f, 1.9f, 0.0f}, {4.2f, 3.1f, 0.0f}};
    for (int i = 0; i < 3; ++i) {
        entt::entity coin = scene.CreateEntity("Coin_" + std::to_string(i + 1));
        auto& tr = reg.get<TransformComponent>(coin);
        tr.translation = coinPos[i];
        tr.scale = {0.75f, 0.75f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(coin);
        spr.texturePath = "textures/coin_spritesheet.png";
        spr.color = {1.0f, 0.92f, 0.25f, 1.0f};
        spr.hFrames = 4;
        spr.vFrames = 1;
        spr.animFps = 8.0f;
        auto& rb = reg.emplace<RigidBodyComponent>(coin);
        rb.bodyType = RigidBodyType::Kinematic;
        rb.shape = ColliderShape::Box2D;
        rb.halfExtents = {0.35f, 0.35f, 0.5f};
        rb.isTrigger = true;
        reg.emplace<JavaScriptComponent>(coin, JavaScriptComponent{"CoinPickup2DScript", true, false});
    }

    {
        entt::entity player = scene.CreateEntity("Jugador 2D");
        auto& tr = reg.get<TransformComponent>(player);
        tr.translation = {-5.5f, -1.0f, 0.0f};
        tr.scale = {0.9f, 1.1f, 1.0f};
        auto& spr = reg.emplace<SpriteRendererComponent>(player);
        spr.texturePath = "textures/checker_albedo.png";
        spr.color = {0.25f, 0.88f, 0.52f, 1.0f};
        spr.sortingOrder = 5;
        auto& rb = reg.emplace<RigidBodyComponent>(player);
        rb.bodyType = RigidBodyType::Dynamic;
        rb.shape = ColliderShape::Box2D;
        rb.halfExtents = {0.45f, 0.55f, 0.5f};
        rb.restitution = 0.0f;
        rb.friction = 0.3f;
        rb.useGravity = true;
        auto& audio = reg.emplace<AudioSourceComponent>(player);
        audio.soundPath = "audio/bounce.wav";
        audio.playOnBounce = false;
        reg.emplace<JavaScriptComponent>(player, JavaScriptComponent{"PlatformerPlayerScript", true, false});
    }

    {
        entt::entity hud = scene.CreateEntity("HUD_Platformer");
        auto& ui = reg.emplace<UITextComponent>(hud);
        ui.text = "SkyHop 2D | Monedas: 0/3 | A/D: Mover | Espacio/W: Saltar";
        ui.screenPos = {22.0f, 54.0f};
        ui.color = {0.95f, 0.98f, 1.0f, 1.0f};
        ui.fontScale = 1.15f;
    }

    WriteTextFile(
        projectDir / "scripts" / "PlatformerPlayerScript.java",
        "import dev.kyumist.Audio;\n"
        "import dev.kyumist.Entity;\n"
        "import dev.kyumist.Input;\n"
        "import dev.kyumist.KyumistScript;\n\n"
        "public class PlatformerPlayerScript extends KyumistScript {\n"
        "    private boolean grounded = false;\n"
        "    private float coyoteTimer = 0.0f;\n"
        "    private float jumpCooldown = 0.0f;\n"
        "    private int coinsCollected = 0;\n"
        "    private final float[] vel = new float[3];\n"
        "    private Entity hudEntity;\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        hudEntity = findEntity(\"HUD_Platformer\");\n"
        "        updateHud();\n"
        "        logInfo(\"SkyHop 2D iniciado en entidad \" + entity.getId());\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        entity.getVelocity(vel);\n"
        "        float moveX = 0.0f;\n"
        "        if (Input.isKeyDown(Input.KEY_A) || Input.isKeyDown(Input.KEY_LEFT))  moveX -= 5.6f;\n"
        "        if (Input.isKeyDown(Input.KEY_D) || Input.isKeyDown(Input.KEY_RIGHT)) moveX += 5.6f;\n\n"
        "        if (jumpCooldown > 0.0f) jumpCooldown -= dt;\n"
        "        if (coyoteTimer > 0.0f)  coyoteTimer -= dt;\n"
        "        if (Math.abs(vel[1]) < 0.25f) {\n"
        "            coyoteTimer = 0.09f;\n"
        "        }\n\n"
        "        float velY = vel[1];\n"
        "        boolean jumpPressed = Input.isKeyDown(Input.KEY_SPACE) || Input.isKeyDown(Input.KEY_W) || Input.isKeyDown(Input.KEY_UP);\n"
        "        if (jumpPressed && (grounded || coyoteTimer > 0.0f) && jumpCooldown <= 0.0f) {\n"
        "            velY = 8.4f;\n"
        "            grounded = false;\n"
        "            coyoteTimer = 0.0f;\n"
        "            jumpCooldown = 0.22f;\n"
        "            Audio.playSound(\"audio/bounce.wav\", 0.5f);\n"
        "        }\n\n"
        "        entity.setVelocity(moveX, velY, 0.0f);\n\n"
        "        if (entity.getY() < -7.5f) {\n"
        "            entity.setPosition(-5.5f, -0.5f, 0.0f);\n"
        "            entity.setVelocity(0.0f, 0.0f, 0.0f);\n"
        "        }\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onCollisionEnter(Entity other) {\n"
        "        String otherName = other.getName();\n"
        "        if (otherName.startsWith(\"Plat\") || otherName.equals(\"Suelo Base\")) {\n"
        "            entity.getVelocity(vel);\n"
        "            if (vel[1] <= 0.5f) {\n"
        "                grounded = true;\n"
        "                coyoteTimer = 0.12f;\n"
        "            }\n"
        "        } else if (otherName.startsWith(\"Coin_\")) {\n"
        "            other.setPosition(0.0f, -60.0f, 0.0f);\n"
        "            coinsCollected++;\n"
        "            Audio.playSound(\"audio/bounce.wav\", 0.95f);\n"
        "            updateHud();\n"
        "        }\n"
        "    }\n\n"
        "    private void updateHud() {\n"
        "        if (hudEntity == null) {\n"
        "            hudEntity = findEntity(\"HUD_Platformer\");\n"
        "        }\n"
        "        if (hudEntity != null) {\n"
        "            if (coinsCollected >= 3) {\n"
        "                hudEntity.setUIText(\"SkyHop 2D | VICTORIA! Monedas: 3/3\");\n"
        "            } else {\n"
        "                hudEntity.setUIText(\"SkyHop 2D | Monedas: \" + coinsCollected + \"/3 | A/D: Mover | Espacio/W: Saltar\");\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n"
    );

    WriteTextFile(
        projectDir / "scripts" / "MovingPlatform2DScript.java",
        "import dev.kyumist.KyumistScript;\n\n"
        "public class MovingPlatform2DScript extends KyumistScript {\n"
        "    private float elapsed = 0.0f;\n"
        "    private float baseX = 0.0f;\n"
        "    private float baseY = 0.8f;\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        float[] pos = new float[3];\n"
        "        entity.getPosition(pos);\n"
        "        baseX = pos[0];\n"
        "        baseY = pos[1];\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        elapsed += dt;\n"
        "        float offsetX = (float) Math.sin(elapsed * 1.4) * 1.6f;\n"
        "        float offsetY = (float) Math.cos(elapsed * 1.4) * 0.45f;\n"
        "        entity.setPosition(baseX + offsetX, baseY + offsetY, 0.0f);\n"
        "    }\n"
        "}\n"
    );

    WriteTextFile(
        projectDir / "scripts" / "CoinPickup2DScript.java",
        "import dev.kyumist.KyumistScript;\n\n"
        "public class CoinPickup2DScript extends KyumistScript {\n"
        "    private float elapsed = 0.0f;\n"
        "    private float baseX = 0.0f;\n"
        "    private float baseY = 0.0f;\n\n"
        "    @Override\n"
        "    public void onCreate() {\n"
        "        float[] pos = new float[3];\n"
        "        entity.getPosition(pos);\n"
        "        baseX = pos[0];\n"
        "        baseY = pos[1];\n"
        "    }\n\n"
        "    @Override\n"
        "    public void onUpdate(float dt) {\n"
        "        elapsed += dt;\n"
        "        if (entity.getY() > -20.0f) {\n"
        "            entity.setPosition(baseX, baseY + (float) Math.sin(elapsed * 4.0) * 0.14f, 0.0f);\n"
        "        }\n"
        "    }\n"
        "}\n"
    );
}

bool PopulateProjectFromTemplate(
    const std::filesystem::path& projectDir,
    const std::string& projectName,
    int templateIndex,
    std::string* outError
) {
    auto setErr = [&](const std::string& msg) {
        if (outError) {
            *outError = msg;
        }
        KM_CORE_ERROR("Project setup failed: {}", msg);
        return false;
    };

    std::error_code ec;
    std::filesystem::create_directories(projectDir / "scenes", ec);
    std::filesystem::create_directories(projectDir / "scripts", ec);
    std::filesystem::create_directories(projectDir / "models", ec);
    std::filesystem::create_directories(projectDir / "audio", ec);
    std::filesystem::create_directories(projectDir / ".vscode", ec);
    if (ec) {
        return setErr("No se pudieron crear las carpetas del proyecto: " + ec.message());
    }

    nlohmann::json projJson;
    projJson["name"] = projectName;
    projJson["version"] = "0.1.0";
    projJson["engineVersion"] = KM_ENGINE_VERSION;
    projJson["sampleRevision"] = 2;
    projJson["startScene"] = "scenes/main.kmscene";
    WriteTextFile(projectDir / "project.kmproj", projJson.dump(2) + "\n");

#ifdef KM_JAVA_API_JAR
    const std::string jarPathStr = KM_JAVA_API_JAR;
#else
    const std::string jarPathStr = "kyumist-api.jar";
#endif
    nlohmann::json vscodeJson;
    vscodeJson["java.project.sourcePaths"] = nlohmann::json::array({"scripts"});
    vscodeJson["java.project.referencedLibraries"] = nlohmann::json::array({jarPathStr});
    WriteTextFile(projectDir / ".vscode" / "settings.json", vscodeJson.dump(2) + "\n");

    const std::filesystem::path sampleObj = ResolveDefaultAssetsDir() / "models" / "pyramid.obj";
    if (std::filesystem::exists(sampleObj, ec)) {
        std::filesystem::copy_file(
            sampleObj,
            projectDir / "models" / "pyramid.obj",
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
    }

    AudioEngine::EnsureBounceWavInDir(projectDir);
    ModelLoader::EnsureDefaultSampleAssets(projectDir);

    Scene scene;
    if (templateIndex == 1) {
        BuildShooter3DProjectFiles(scene, projectDir);
    } else if (templateIndex == 2) {
        BuildPlatformer2DProjectFiles(scene, projectDir);
    } else {
        BuildHybridSanctuaryProjectFiles(scene, projectDir);
    }

    SceneSerializer::SaveToFile(scene, projectDir / "scenes" / "main.kmscene");
    return true;
}

struct SampleTemplateSpec {
    const char* id;
    const char* name;
    const char* badge;
    const char* description;
    int templateIndex;
};

constexpr SampleTemplateSpec kSampleSpecs[3] = {
    {
        "platformer_2d",
        "SkyHop 2D",
        "PLATAFORMAS 2D",
        "Camara ortografica 2D, saltos con fisicas Box2D, plataforma movil y monedas animadas.",
        2
    },
    {
        "shooter_3d",
        "Vortex Arena 3D",
        "SHOOTER 3D",
        "Arena 3D PBR con torreta jerarquica, raycast laser con Espacio/Clic y drones enemigos.",
        1
    },
    {
        "hybrid_sanctuary",
        "Aether Guardian",
        "HIBRIDO 3D + 2D",
        "Santuario estilo HD-2D: heroe y runas 2D animadas en escenario 3D con Totem glTF y luz puntual.",
        0
    }
};

} // namespace

void ProjectManager::EnsureDefaultSampleProject() {
    EnsureSampleProjects();
}

void ProjectManager::EnsureSampleProjects() {
    std::error_code ec;
    const std::filesystem::path legacyDemoFile = ResolveDefaultAssetsDir() / "project.kmproj";
    if (std::filesystem::exists(legacyDemoFile, ec)) {
        std::filesystem::remove(legacyDemoFile, ec);
        KM_CORE_INFO("Removed legacy demo project file '{}'.", legacyDemoFile.string());
    }

    // Prune any legacy demo or sample entries from recent projects
    LoadRecentProjects();
    const size_t oldSize = s_Proj.recents.size();
    s_Proj.recents.erase(
        std::remove_if(s_Proj.recents.begin(), s_Proj.recents.end(), [&](const RecentProjectEntry& e) {
            return IsDemoOrSampleProjectEntry(e.name, e.projectFilePath);
        }),
        s_Proj.recents.end()
    );
    if (s_Proj.recents.size() != oldSize) {
        SaveRecentProjects();
    }

    const std::filesystem::path examplesRoot = ResolveExamplesDir();
    std::filesystem::create_directories(examplesRoot, ec);

    for (const auto& spec : kSampleSpecs) {
        const std::filesystem::path dir = examplesRoot / spec.id;
        const std::filesystem::path projFile = dir / "project.kmproj";
        bool needsRegenerate =
            !std::filesystem::exists(projFile, ec) ||
            !std::filesystem::exists(dir / "scenes" / "main.kmscene", ec);
        if (!needsRegenerate) {
            std::ifstream pf(projFile);
            if (pf.is_open()) {
                try {
                    nlohmann::json parsed;
                    pf >> parsed;
                    if (parsed.value("sampleRevision", 0) < 2) {
                        needsRegenerate = true;
                    }
                } catch (const std::exception&) {
                    needsRegenerate = true;
                }
            }
        }
        if (needsRegenerate) {
            std::filesystem::remove_all(dir, ec);
            std::string err;
            if (PopulateProjectFromTemplate(dir, spec.name, spec.templateIndex, &err)) {
                KM_CORE_INFO("Generated sample project '{}' (rev 2) at '{}'.", spec.name, dir.string());
            }
        }
    }
}

std::vector<SampleProjectEntry> ProjectManager::GetSampleProjects() {
    const std::filesystem::path examplesRoot = ResolveExamplesDir();
    std::vector<SampleProjectEntry> list;
    list.reserve(3);
    for (const auto& spec : kSampleSpecs) {
        SampleProjectEntry entry;
        entry.id = spec.id;
        entry.name = spec.name;
        entry.categoryBadge = spec.badge;
        entry.description = spec.description;
        entry.templateIndex = spec.templateIndex;
        entry.projectFilePath = NormalizePathString(examplesRoot / spec.id / "project.kmproj");
        list.push_back(std::move(entry));
    }
    return list;
}

bool ProjectManager::ResetSampleProject(int sampleIndex, std::string* outError) {
    if (sampleIndex < 0 || sampleIndex >= 3) {
        if (outError) {
            *outError = "Indice de ejemplo invalido.";
        }
        return false;
    }

    const auto& spec = kSampleSpecs[sampleIndex];
    const std::filesystem::path dir = ResolveExamplesDir() / spec.id;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return PopulateProjectFromTemplate(dir, spec.name, spec.templateIndex, outError);
}

bool ProjectManager::CreateNewProject(
    const std::filesystem::path& parentDir,
    const std::string& projectName,
    int templateIndex,
    std::string* outError
) {
    auto setErr = [&](const std::string& msg) {
        if (outError) {
            *outError = msg;
        }
        KM_CORE_ERROR("Project creation failed: {}", msg);
        return false;
    };

    if (projectName.empty()) {
        return setErr("El nombre del proyecto no puede estar vacio.");
    }
    if (parentDir.empty()) {
        return setErr("Selecciona un directorio padre valido.");
    }

    std::error_code ec;
    if (!std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            return setErr("No se pudo crear el directorio padre: " + ec.message());
        }
    }

    const std::filesystem::path projectDir = parentDir / projectName;
    if (std::filesystem::exists(projectDir / "project.kmproj", ec)) {
        return setErr("Ya existe un proyecto llamado '" + projectName + "' en esa ruta.");
    }

    if (!PopulateProjectFromTemplate(projectDir, projectName, templateIndex, outError)) {
        return false;
    }

    return OpenProject(projectDir / "project.kmproj");
}

bool ProjectManager::OpenProject(const std::filesystem::path& projectFileOrDir) {
    std::filesystem::path projectFile = projectFileOrDir;
    std::error_code ec;
    if (std::filesystem::is_directory(projectFile, ec)) {
        projectFile /= "project.kmproj";
    }
    if (!std::filesystem::exists(projectFile, ec)) {
        KM_CORE_ERROR("Project file not found: '{}'.", projectFile.string());
        return false;
    }

    std::ifstream in(projectFile);
    if (!in.is_open()) {
        KM_CORE_ERROR("Unable to open project file '{}'.", projectFile.string());
        return false;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        KM_CORE_ERROR("Invalid project.kmproj '{}': {}", projectFile.string(), ex.what());
        return false;
    }

    const std::filesystem::path absFile = std::filesystem::weakly_canonical(projectFile, ec);
    const std::filesystem::path resolvedFile = ec ? projectFile : absFile;

    ProjectConfig cfg;
    cfg.name = root.value("name", resolvedFile.parent_path().filename().string());
    cfg.version = root.value("version", "0.1.0");
    cfg.engineVersion = root.value("engineVersion", cfg.version);
    cfg.startScene = root.value("startScene", "scenes/main.kmscene");
    cfg.projectDir = resolvedFile.parent_path();

    s_Proj.activeConfig = cfg;
    s_Proj.hasActive = true;

    RegisterRecentProject(cfg.name, resolvedFile);

    AudioEngine::SetAssetsDirectory(cfg.projectDir);
    AudioEngine::EnsureBounceWavInDir(cfg.projectDir);

    ScriptEngine::SetScriptsDirectory(cfg.projectDir / "scripts");
    if (ScriptEngine::IsInitialized()) {
        ScriptEngine::CompileAndReloadScripts(nullptr, false);
    }

    KM_CORE_INFO("Opened project '{}' at '{}'.", cfg.name, cfg.projectDir.string());
    return true;
}

void ProjectManager::CloseActiveProject() {
    s_Proj.hasActive = false;
    s_Proj.activeConfig = {};
    KM_CORE_INFO("Closed active project.");
}

bool ProjectManager::HasActiveProject() {
    return s_Proj.hasActive;
}

const ProjectConfig& ProjectManager::GetActiveProject() {
    return s_Proj.activeConfig;
}

std::filesystem::path ProjectManager::GetActiveProjectDir() {
    return s_Proj.hasActive ? s_Proj.activeConfig.projectDir : ResolveDefaultAssetsDir();
}

std::filesystem::path ProjectManager::GetStartScenePath() {
    return GetActiveProjectDir() / s_Proj.activeConfig.startScene;
}

std::vector<RecentProjectEntry> ProjectManager::GetRecentProjects() {
    LoadRecentProjects();
    return s_Proj.recents;
}

void ProjectManager::RemoveRecentProject(const std::string& projectFilePath) {
    LoadRecentProjects();
    s_Proj.recents.erase(
        std::remove_if(s_Proj.recents.begin(), s_Proj.recents.end(), [&](const RecentProjectEntry& e) {
            return e.projectFilePath == projectFilePath;
        }),
        s_Proj.recents.end()
    );
    SaveRecentProjects();
}

bool ProjectManager::ExportProjectBuild(const std::filesystem::path& outputDir, std::string* outError) {
    auto setErr = [&](const std::string& msg) {
        if (outError) {
            *outError = msg;
        }
        KM_CORE_ERROR("Project export failed: {}", msg);
        return false;
    };

    if (!HasActiveProject()) {
        return setErr("No hay ningun proyecto activo para exportar.");
    }
    if (outputDir.empty()) {
        return setErr("Ruta de exportacion invalida.");
    }

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        return setErr("No se pudo crear el directorio de salida: " + ec.message());
    }

    const std::filesystem::path projDir = GetActiveProjectDir();
    const std::vector<std::string> foldersToCopy = {
        "scenes", "scripts", "models", "materials", "textures", "audio", "prefabs"
    };

    for (const auto& folder : foldersToCopy) {
        const std::filesystem::path src = projDir / folder;
        const std::filesystem::path dst = outputDir / folder;
        if (std::filesystem::exists(src, ec) && std::filesystem::is_directory(src, ec)) {
            std::filesystem::create_directories(dst, ec);
            std::filesystem::copy(
                src,
                dst,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                ec
            );
        }
    }

    const std::filesystem::path srcProjFile = projDir / "project.kmproj";
    if (std::filesystem::exists(srcProjFile, ec)) {
        std::filesystem::copy_file(
            srcProjFile,
            outputDir / "project.kmproj",
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
    }

    // Copy Runtime executable, kyumist-api.jar & compiled shaders if found in build/
    std::filesystem::path buildDir;
#ifdef KM_JAVA_API_JAR
    buildDir = std::filesystem::path(KM_JAVA_API_JAR).parent_path().parent_path();
#else
    buildDir = std::filesystem::path("build");
#endif

    const std::filesystem::path runtimeCandidates[] = {
        buildDir / "runtime" / "KyumistRuntime.exe",
        buildDir / "runtime" / "Release" / "KyumistRuntime.exe",
        buildDir / "runtime" / "Debug" / "KyumistRuntime.exe",
        buildDir / "runtime" / "KyumistRuntime"
    };
    for (const auto& runtimeExe : runtimeCandidates) {
        if (std::filesystem::exists(runtimeExe, ec) && std::filesystem::is_regular_file(runtimeExe, ec)) {
            std::filesystem::copy_file(
                runtimeExe,
                outputDir / runtimeExe.filename(),
                std::filesystem::copy_options::overwrite_existing,
                ec
            );
            break;
        }
    }

    const std::filesystem::path jarSrc = PlatformPaths::ResolveJavaApiJar();
    if (std::filesystem::exists(jarSrc, ec)) {
        std::filesystem::create_directories(outputDir / "java", ec);
        std::filesystem::copy_file(
            jarSrc,
            outputDir / "java" / "kyumist-api.jar",
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
    }

    const std::filesystem::path shadersSrc = PlatformPaths::ResolveShadersDir();
    if (std::filesystem::exists(shadersSrc, ec)) {
        std::filesystem::create_directories(outputDir / "shaders", ec);
        for (const auto& entry : std::filesystem::directory_iterator(shadersSrc, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".spv") {
                std::filesystem::copy_file(
                    entry.path(),
                    outputDir / "shaders" / entry.path().filename(),
                    std::filesystem::copy_options::overwrite_existing,
                    ec
                );
            }
        }
    }

    const std::string startScene = s_Proj.activeConfig.startScene.empty()
        ? "scenes/main.kmscene"
        : s_Proj.activeConfig.startScene;

    const std::filesystem::path launcherPath = outputDir / "run_game.sh";
    WriteTextFile(
        launcherPath,
        "#!/usr/bin/env bash\n"
        "cd \"$(dirname \"$0\")\"\n"
        "if [ -x ./KyumistRuntime ]; then\n"
        "  ./KyumistRuntime --scene \"" + startScene + "\" \"$@\"\n"
        "else\n"
        "  echo 'KyumistRuntime executable not found in export bundle.'\n"
        "  exit 1\n"
        "fi\n"
    );
    std::filesystem::permissions(
        launcherPath,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add,
        ec
    );

    const std::filesystem::path winLauncherPath = outputDir / "run_game.bat";
    WriteTextFile(
        winLauncherPath,
        "@echo off\r\n"
        "cd /d \"%~dp0\"\r\n"
        "if defined JAVA_HOME set \"PATH=%JAVA_HOME%\\bin\\server;%JAVA_HOME%\\bin;%PATH%\"\r\n"
        "if exist \"KyumistRuntime.exe\" (\r\n"
        "  \"KyumistRuntime.exe\" --scene \"" + startScene + "\" %*\r\n"
        ") else (\r\n"
        "  echo KyumistRuntime.exe not found in export bundle.\r\n"
        "  exit /b 1\r\n"
        ")\r\n"
    );

    KM_INFO("Proyecto '{}' exportado a '{}'.", s_Proj.activeConfig.name, outputDir.string());
    return true;
}

} // namespace Kyumist

