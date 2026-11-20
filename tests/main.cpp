#include "audio/AudioEngine.hpp"
#include "core/InputMap.hpp"
#include "core/Log.hpp"
#include "core/Timestep.hpp"
#include "physics/PhysicsSystem.hpp"
#include "project/ProjectManager.hpp"
#include "renderer/ModelLoader.hpp"
#include "scene/Components.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptEngine.hpp"
#include "ui/TouchControlsOverlay.hpp"
#include "ui/UISystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_Failures = 0;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_Failures;
    }
}

void TestECSAndTransform() {
    Kyumist::Scene scene;
    entt::entity e = scene.CreateEntity("TestCube");
    Require(scene.IsValid(e), "Entity should be valid after CreateEntity");

    auto& reg = scene.GetRegistry();
    Require(reg.get<Kyumist::TagComponent>(e).tag == "TestCube", "TagComponent should match entity name");

    auto& tr = reg.get<Kyumist::TransformComponent>(e);
    tr.translation = {3.0f, 4.0f, -2.0f};
    const glm::mat4 m = tr.GetTransform();
    Require(std::abs(m[3][0] - 3.0f) < 1e-4f, "Matrix translation X should be 3.0");
    Require(std::abs(m[3][1] - 4.0f) < 1e-4f, "Matrix translation Y should be 4.0");
    Require(std::abs(m[3][2] + 2.0f) < 1e-4f, "Matrix translation Z should be -2.0");

    scene.DestroyEntity(e);
    Require(!scene.IsValid(e), "Entity should be invalid after DestroyEntity");
    std::cout << "[PASS] TestECSAndTransform\n";
}

void TestSceneSerializationRoundTrip() {
    Kyumist::Scene srcScene;
    srcScene.CreateDefaultScene();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "kyumist_test_scene.kmscene";
    Require(Kyumist::SceneSerializer::SaveToFile(srcScene, tempPath), "SaveToFile should succeed");
    Require(std::filesystem::exists(tempPath), "Serialized scene file should exist");

    Kyumist::Scene dstScene;
    Require(Kyumist::SceneSerializer::LoadFromFile(dstScene, tempPath), "LoadFromFile should succeed");

    bool foundSphere = false;
    bool foundFloor = false;
    bool foundCube = false;
    auto& reg = dstScene.GetRegistry();
    for (auto entity : reg.view<Kyumist::TagComponent>()) {
        const std::string& tag = reg.get<Kyumist::TagComponent>(entity).tag;
        if (tag == "Esfera PBR") {
            foundSphere = true;
            const auto* rb = reg.try_get<Kyumist::RigidBodyComponent>(entity);
            const auto* audio = reg.try_get<Kyumist::AudioSourceComponent>(entity);
            Require(rb != nullptr, "Esfera PBR should have RigidBodyComponent");
            Require(audio != nullptr, "Esfera PBR should have AudioSourceComponent");
            if (rb) {
                Require(rb->bodyType == Kyumist::RigidBodyType::Dynamic, "Esfera PBR should be Dynamic");
                Require(rb->shape == Kyumist::ColliderShape::Sphere3D, "Esfera PBR should be Sphere3D");
            }
            if (audio) {
                Require(audio->soundPath == "audio/bounce.wav", "AudioSource should keep bounce.wav");
            }
        } else if (tag == "Suelo 3D") {
            foundFloor = true;
            const auto* rb = reg.try_get<Kyumist::RigidBodyComponent>(entity);
            Require(rb != nullptr && rb->bodyType == Kyumist::RigidBodyType::Static, "Suelo 3D should be Static");
        } else if (tag == "Cubo PBR") {
            foundCube = true;
            const auto* js = reg.try_get<Kyumist::JavaScriptComponent>(entity);
            Require(js != nullptr && js->className == "RotatorScript", "Cubo PBR should keep RotatorScript");
        }
    }

    Require(foundSphere && foundFloor && foundCube, "All default entities should survive round-trip");
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    std::cout << "[PASS] TestSceneSerializationRoundTrip\n";
}

void TestPhysicsGravityAndBounce() {
    Kyumist::AudioEngine::Init(true);

    Kyumist::Scene scene;
    entt::entity floor = scene.CreateEntity("Floor");
    {
        auto& tr = scene.GetRegistry().get<Kyumist::TransformComponent>(floor);
        tr.translation = {0.0f, 0.0f, 0.0f};
        auto& rb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(floor);
        rb.bodyType = Kyumist::RigidBodyType::Static;
        rb.shape = Kyumist::ColliderShape::Box3D;
        rb.halfExtents = {4.0f, 0.05f, 4.0f};
        rb.restitution = 0.8f;
    }

    entt::entity sphere = scene.CreateEntity("Ball");
    {
        auto& tr = scene.GetRegistry().get<Kyumist::TransformComponent>(sphere);
        tr.translation = {0.0f, 1.5f, 0.0f};
        auto& rb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(sphere);
        rb.bodyType = Kyumist::RigidBodyType::Dynamic;
        rb.shape = Kyumist::ColliderShape::Sphere3D;
        rb.radius = 0.5f;
        rb.restitution = 0.8f;
        auto& audio = scene.GetRegistry().emplace<Kyumist::AudioSourceComponent>(sphere);
        audio.soundPath = "audio/bounce.wav";
        audio.playOnBounce = true;
    }

    bool observedFall = false;
    bool observedBounceUp = false;
    bool stayedAboveFloor = true;

    for (int step = 0; step < 120; ++step) {
        Kyumist::PhysicsSystem::Step(&scene, Kyumist::Timestep(1.0f / 60.0f));
        const auto& tr = scene.GetRegistry().get<Kyumist::TransformComponent>(sphere);
        const auto& rb = scene.GetRegistry().get<Kyumist::RigidBodyComponent>(sphere);

        if (tr.translation.y < 1.45f) {
            observedFall = true;
        }
        if (tr.translation.y < 0.50f) {
            stayedAboveFloor = false;
        }
        if (observedFall && rb.velocity.y > 0.5f) {
            observedBounceUp = true;
        }
    }

    Require(observedFall, "Sphere should fall under gravity");
    Require(stayedAboveFloor, "Sphere should not tunnel through static floor");
    Require(observedBounceUp, "Sphere should bounce upward after colliding with floor");
    Require(Kyumist::AudioEngine::GetPlayedSoundCount() >= 1, "Bounce should trigger AudioEngine::PlaySound");

    Kyumist::AudioEngine::Shutdown();
    std::cout << "[PASS] TestPhysicsGravityAndBounce\n";
}

void TestAudioWavGeneration() {
    const std::filesystem::path wavPath = Kyumist::AudioEngine::EnsureDefaultBounceWav();
    Require(std::filesystem::exists(wavPath), "bounce.wav should be generated on disk");

    std::ifstream in(wavPath, std::ios::binary);
    char header[12]{};
    in.read(header, sizeof(header));
    Require(in.gcount() == 12, "bounce.wav should contain at least 12 header bytes");
    Require(std::string(header, 4) == "RIFF", "bounce.wav should start with RIFF");
    Require(std::string(header + 8, 4) == "WAVE", "bounce.wav should have WAVE format marker");
    std::cout << "[PASS] TestAudioWavGeneration\n";
}

void TestProjectManagerCreateAndOpen() {
    const std::filesystem::path tempParent = std::filesystem::temp_directory_path() / "kyumist_test_proj_root";
    std::error_code ec;
    std::filesystem::remove_all(tempParent, ec);
    std::filesystem::create_directories(tempParent, ec);

    std::string err;
    const bool created = Kyumist::ProjectManager::CreateNewProject(tempParent, "TestProjTemp", 0, &err);
    Require(created, "ProjectManager::CreateNewProject should succeed: " + err);

    const std::filesystem::path projDir = tempParent / "TestProjTemp";
    const std::filesystem::path projFile = projDir / "project.kmproj";
    Require(std::filesystem::exists(projFile), "project.kmproj should exist");
    Require(std::filesystem::exists(projDir / "scenes" / "main.kmscene"), "scenes/main.kmscene should exist");
    Require(std::filesystem::exists(projDir / "scripts" / "HybridHeroScript.java"), "scripts/HybridHeroScript.java should exist");
    Require(std::filesystem::exists(projDir / "scripts" / "TotemPulse3DScript.java"), "scripts/TotemPulse3DScript.java should exist");
    Require(std::filesystem::exists(projDir / "audio" / "bounce.wav"), "audio/bounce.wav should exist");
    Require(std::filesystem::exists(projDir / ".vscode" / "settings.json"), ".vscode/settings.json should exist");

    const bool opened = Kyumist::ProjectManager::OpenProject(projFile);
    Require(opened, "ProjectManager::OpenProject should succeed");
    Require(Kyumist::ProjectManager::HasActiveProject(), "ProjectManager should report active project");
    Require(Kyumist::ProjectManager::GetActiveProject().name == "TestProjTemp", "Active project name should match");

    Kyumist::ProjectManager::CloseActiveProject();
    Kyumist::ProjectManager::RemoveRecentProject(projFile.string());
    Require(!Kyumist::ProjectManager::HasActiveProject(), "CloseActiveProject should clear active project");

    // Verify legacy demo removal and 3 built-in Sample Projects
    Kyumist::ProjectManager::EnsureSampleProjects();
    const auto samples = Kyumist::ProjectManager::GetSampleProjects();
    Require(samples.size() == 3, "Should expose 3 sample projects");
    for (const auto& s : samples) {
        Require(std::filesystem::exists(s.projectFilePath), "Sample project.kmproj should exist: " + s.name);
        Require(Kyumist::ProjectManager::OpenProject(s.projectFilePath), "Should open sample: " + s.name);
        Kyumist::ProjectManager::CloseActiveProject();
    }
    const auto recents = Kyumist::ProjectManager::GetRecentProjects();
    for (const auto& r : recents) {
        Require(r.name != "Kyumist Demo Project", "Legacy Kyumist Demo Project must not appear in recents");
    }

    std::filesystem::remove_all(tempParent, ec);
    std::cout << "[PASS] TestProjectManagerCreateAndOpen\n";
}

void TestGltfAndMtlLoading() {
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "kyumist_test_models";
    const std::filesystem::path importRoot = tempRoot / "imported";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(tempRoot, ec);

    const std::filesystem::path gltfPath = Kyumist::ModelLoader::EnsureDefaultSampleAssets(tempRoot);
    Require(std::filesystem::exists(gltfPath), "EnsureDefaultSampleAssets should create crystal_totem.gltf");
    Require(std::filesystem::exists(tempRoot / "models" / "pyramid.mtl"), "pyramid.mtl should be created");
    Require(std::filesystem::exists(tempRoot / "materials" / "gold_pbr.kmmat"), "gold_pbr.kmmat should be created");

    Kyumist::LoadedModelData modelData;
    Require(Kyumist::ModelLoader::LoadModel(gltfPath, modelData), "ModelLoader::LoadModel should parse crystal_totem.gltf");
    Require(!modelData.vertices.empty(), "glTF vertices should be non-empty");
    Require(!modelData.indices.empty(), "glTF indices should be non-empty");
    Require(modelData.hasMaterial, "glTF should report primary embedded PBR material");

    Kyumist::PBRMaterialData mtlData;
    Require(Kyumist::ModelLoader::LoadMaterialFile(tempRoot / "models" / "pyramid.mtl", mtlData), "LoadMaterialFile should parse pyramid.mtl");
    Require(std::abs(mtlData.metallic - 0.80f) < 0.02f, "MTL metallic factor should match Pm 0.80");

    Kyumist::PBRMaterialData savedMat;
    savedMat.name = "CustomChrome";
    savedMat.baseColor = {0.75f, 0.82f, 0.90f, 1.0f};
    savedMat.metallic = 0.95f;
    savedMat.roughness = 0.12f;
    savedMat.emissive = {0.10f, 0.25f, 0.35f};
    const std::filesystem::path customMatPath = tempRoot / "materials" / "chrome.kmmat";
    Require(Kyumist::ModelLoader::SaveMaterialFile(customMatPath, savedMat), "SaveMaterialFile should write chrome.kmmat");

    Kyumist::PBRMaterialData reloadedMat;
    Require(Kyumist::ModelLoader::LoadMaterialFile(customMatPath, reloadedMat), "LoadMaterialFile should read chrome.kmmat");
    Require(std::abs(reloadedMat.metallic - 0.95f) < 0.001f, "Reloaded metallic should match");
    Require(std::abs(reloadedMat.roughness - 0.12f) < 0.001f, "Reloaded roughness should match");
    Require(std::abs(reloadedMat.emissive.b - 0.35f) < 0.001f, "Reloaded emissive should match");

    std::filesystem::path importedPath;
    std::string err;
    Require(Kyumist::ModelLoader::ImportExternalAssetToDir(gltfPath, importRoot, &importedPath, &err),
            "ImportExternalAssetToDir should copy gltf: " + err);
    Require(std::filesystem::exists(importedPath), "Imported gltf file should exist in target dir");

    std::filesystem::remove_all(tempRoot, ec);
    std::cout << "[PASS] TestGltfAndMtlLoading\n";
}

void TestFullEngineCapabilities() {
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "kyumist_test_full_caps";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(tempRoot, ec);

    // 1. PNG write & zlib decode roundtrip
    const std::array<uint8_t, 16> rawPixels{
        255,   0,   0, 255,    0, 255,   0, 255,
          0,   0, 255, 255,  250, 220,  40, 128
    };
    const std::filesystem::path pngFile = tempRoot / "test_2x2.png";
    Require(Kyumist::ModelLoader::SavePngRGBA(pngFile, 2, 2, rawPixels.data()), "SavePngRGBA should write 2x2 PNG");

    Kyumist::ImageData img;
    Require(Kyumist::ModelLoader::LoadImageRGBA(pngFile, img), "LoadImageRGBA should decode 2x2 PNG");
    Require(img.width == 2 && img.height == 2 && img.pixels.size() == 16, "Decoded PNG dimensions should match 2x2 RGBA");
    Require(img.pixels[0] == 255 && img.pixels[5] == 255 && img.pixels[10] == 255 && img.pixels[15] == 128,
            "Decoded PNG RGBA pixels should match source pixels");

    // 2. Hierarchy world transform, Primary Camera VP, and Prefab roundtrip
    Kyumist::Scene scene;
    entt::entity parent = scene.CreateEntity("ParentTank");
    scene.GetRegistry().get<Kyumist::TransformComponent>(parent).translation = {10.0f, 0.0f, 0.0f};

    entt::entity child = scene.CreateEntity("ChildTurret");
    scene.GetRegistry().get<Kyumist::TransformComponent>(child).translation = {0.0f, 2.5f, 0.0f};
    scene.GetRegistry().emplace<Kyumist::HierarchyComponent>(child, Kyumist::HierarchyComponent{"ParentTank"});
    scene.GetRegistry().emplace<Kyumist::PointLightComponent>(child);
    scene.GetRegistry().emplace<Kyumist::UITextComponent>(child);

    const glm::mat4 world = scene.GetWorldTransform(child);
    Require(std::abs(world[3].x - 10.0f) < 0.001f && std::abs(world[3].y - 2.5f) < 0.001f,
            "GetWorldTransform should combine parent + child translations");

    entt::entity cam = scene.CreateEntity("MainCam");
    scene.GetRegistry().get<Kyumist::TransformComponent>(cam).translation = {0.0f, 2.0f, 8.0f};
    scene.GetRegistry().emplace<Kyumist::CameraComponent>(cam).primary = true;
    glm::mat4 camVP(1.0f);
    Require(scene.ComputePrimaryCameraViewProjection(16.0f / 9.0f, camVP), "ComputePrimaryCameraViewProjection should succeed");

    const std::filesystem::path prefabFile = tempRoot / "turret.kmprefab";
    Require(Kyumist::SceneSerializer::SavePrefab(scene, child, prefabFile), "SavePrefab should write .kmprefab");
    entt::entity loadedChild = Kyumist::SceneSerializer::LoadPrefab(scene, prefabFile);
    Require(scene.IsValid(loadedChild), "LoadPrefab should spawn valid entity");
    Require(scene.GetRegistry().all_of<Kyumist::PointLightComponent>(loadedChild), "Prefab should preserve PointLightComponent");
    Require(scene.GetRegistry().all_of<Kyumist::UITextComponent>(loadedChild), "Prefab should preserve UITextComponent");

    // 3. Physics Raycast, isTrigger (no impulse separation), and angularVelocity integration
    entt::entity targetBox = scene.CreateEntity("RaycastTarget");
    scene.GetRegistry().get<Kyumist::TransformComponent>(targetBox).translation = {0.0f, 1.0f, -5.0f};
    auto& targetRb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(targetBox);
    targetRb.bodyType = Kyumist::RigidBodyType::Static;
    targetRb.shape = Kyumist::ColliderShape::Box3D;
    targetRb.halfExtents = {1.0f, 1.0f, 1.0f};

    const Kyumist::RaycastHit hit = Kyumist::PhysicsSystem::Raycast(
        &scene,
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        20.0f
    );
    Require(hit.hit && hit.entity == targetBox && std::abs(hit.distance - 4.0f) < 0.01f,
            "PhysicsSystem::Raycast should hit RaycastTarget at distance 4.0");

    entt::entity triggerZone = scene.CreateEntity("CoinTrigger");
    scene.GetRegistry().get<Kyumist::TransformComponent>(triggerZone).translation = {0.0f, 5.0f, 0.0f};
    auto& trigRb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(triggerZone);
    trigRb.bodyType = Kyumist::RigidBodyType::Kinematic;
    trigRb.shape = Kyumist::ColliderShape::Sphere3D;
    trigRb.radius = 1.0f;
    trigRb.isTrigger = true;
    trigRb.angularVelocity = {0.0f, 90.0f, 0.0f};

    entt::entity ball = scene.CreateEntity("TriggerBall");
    scene.GetRegistry().get<Kyumist::TransformComponent>(ball).translation = {0.0f, 5.0f, 0.0f};
    auto& ballRb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(ball);
    ballRb.bodyType = Kyumist::RigidBodyType::Dynamic;
    ballRb.useGravity = false;
    ballRb.velocity = {1.0f, 0.0f, 0.0f};

    Kyumist::PhysicsSystem::Step(&scene, Kyumist::Timestep(0.02f));
    const float rotY = scene.GetRegistry().get<Kyumist::TransformComponent>(triggerZone).rotationDegrees.y;
    const float ballX = scene.GetRegistry().get<Kyumist::TransformComponent>(ball).translation.x;
    Require(std::abs(rotY - 1.8f) < 0.05f, "PhysicsSystem::Step should integrate angularVelocity");
    Require(ballX > 0.0f, "Trigger zone should not block dynamic body movement");

    // 4. Standalone Export Project Build
    const std::filesystem::path projRoot = tempRoot / "proj_parent";
    std::string projErr;
    Require(Kyumist::ProjectManager::CreateNewProject(projRoot, "ExportGame", 0, &projErr),
            "CreateNewProject should succeed for export test: " + projErr);
    const std::filesystem::path exportOut = tempRoot / "export_bundle";
    std::string expErr;
    Require(Kyumist::ProjectManager::ExportProjectBuild(exportOut, &expErr),
            "ExportProjectBuild should succeed: " + expErr);
    Require(std::filesystem::exists(exportOut / "project.kmproj"), "Export bundle should contain project.kmproj");
    Require(std::filesystem::exists(exportOut / "run_game.sh"), "Export bundle should contain run_game.sh");
    Require(std::filesystem::exists(exportOut / "run_game.bat"), "Export bundle should contain run_game.bat");
    Require(std::filesystem::exists(exportOut / "textures" / "checker_albedo.png"), "Export bundle should contain sample PNG texture");

    const std::filesystem::path projFile = projRoot / "ExportGame" / "project.kmproj";
    Kyumist::ProjectManager::CloseActiveProject();
    Kyumist::ProjectManager::RemoveRecentProject(projFile.string());

    std::filesystem::remove_all(tempRoot, ec);
    std::cout << "[PASS] TestFullEngineCapabilities\n";
}

void TestInMemoryUndoRedoSerialization() {
    Kyumist::Scene scene;
    scene.CreateDefaultScene();

    const std::string initialSnapshot = Kyumist::SceneSerializer::SerializeToString(scene);
    Require(!initialSnapshot.empty(), "Initial serialized snapshot should not be empty");

    entt::entity cube = scene.FindEntityByName("Cubo PBR");
    Require(scene.IsValid(cube), "Cubo PBR should exist in default scene");
    scene.GetRegistry().get<Kyumist::TransformComponent>(cube).translation = {5.0f, 3.0f, -2.0f};
    entt::entity extra = scene.CreateEntity("UndoExtraBox");
    scene.GetRegistry().emplace<Kyumist::MeshRendererComponent>(extra).type = Kyumist::MeshType::Cube;

    const std::string modifiedSnapshot = Kyumist::SceneSerializer::SerializeToString(scene);
    Require(modifiedSnapshot != initialSnapshot, "Modified scene snapshot should differ from initial snapshot");

    // Simulate Undo
    Require(Kyumist::SceneSerializer::DeserializeFromString(scene, initialSnapshot),
            "DeserializeFromString should restore initial state on Undo");
    Require(!scene.IsValid(scene.FindEntityByName("UndoExtraBox")),
            "Extra entity should be removed after Undo");
    cube = scene.FindEntityByName("Cubo PBR");
    Require(scene.IsValid(cube), "Cubo PBR should still exist after Undo");
    Require(std::abs(scene.GetRegistry().get<Kyumist::TransformComponent>(cube).translation.x - (-1.2f)) < 0.001f,
            "Cubo PBR position should revert after Undo");

    // Simulate Redo
    Require(Kyumist::SceneSerializer::DeserializeFromString(scene, modifiedSnapshot),
            "DeserializeFromString should restore modified state on Redo");
    Require(scene.IsValid(scene.FindEntityByName("UndoExtraBox")),
            "Extra entity should return after Redo");
    cube = scene.FindEntityByName("Cubo PBR");
    Require(std::abs(scene.GetRegistry().get<Kyumist::TransformComponent>(cube).translation.x - 5.0f) < 0.001f,
            "Cubo PBR position should restore after Redo");

    std::cout << "[PASS] TestInMemoryUndoRedoSerialization\n";
}

void TestKinematicCollisionResolution() {
    Kyumist::Scene scene;

    entt::entity wall = scene.CreateEntity("StaticPillar");
    scene.GetRegistry().get<Kyumist::TransformComponent>(wall).translation = {0.0f, 1.0f, 0.0f};
    auto& wallRb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(wall);
    wallRb.bodyType = Kyumist::RigidBodyType::Static;
    wallRb.shape = Kyumist::ColliderShape::Box3D;
    wallRb.halfExtents = {1.0f, 1.0f, 1.0f};

    entt::entity player = scene.CreateEntity("KinematicPlayer");
    scene.GetRegistry().get<Kyumist::TransformComponent>(player).translation = {1.5f, 1.0f, 0.0f};
    auto& playerRb = scene.GetRegistry().emplace<Kyumist::RigidBodyComponent>(player);
    playerRb.bodyType = Kyumist::RigidBodyType::Kinematic;
    playerRb.shape = Kyumist::ColliderShape::Box3D;
    playerRb.halfExtents = {1.0f, 1.0f, 1.0f};

    Kyumist::PhysicsSystem::Step(&scene, Kyumist::Timestep(0.016f));

    const float playerX = scene.GetRegistry().get<Kyumist::TransformComponent>(player).translation.x;
    Require(playerX >= 1.99f, "Kinematic body should be pushed out of penetrating Static obstacle");

    std::cout << "[PASS] TestKinematicCollisionResolution\n";
}

} // namespace

void TestInputMapAndTouchControls() {
    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path() / "kyumist_test_inputmap";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    Kyumist::InputMap::InitDefaults();
    Kyumist::InputMap::SetActionBindings("dash", {{Kyumist::InputDeviceType::Key, 340}});
    Require(Kyumist::InputMap::SaveToProject(tmpDir), "InputMap should save input_map.json");

    Kyumist::InputMap::RemoveAction("dash");
    Require(Kyumist::InputMap::LoadFromProject(tmpDir), "InputMap should load input_map.json");
    Require(Kyumist::InputMap::GetAllActions().count("dash") == 1, "Loaded InputMap should contain dash");

    Kyumist::InputMap::ClearVirtualStates();
    Kyumist::InputMap::Update(nullptr);
    Require(!Kyumist::InputMap::IsActionPressed("dash"), "dash should not be pressed initially");

    Kyumist::InputMap::SetVirtualActionState("dash", true);
    Kyumist::InputMap::Update(nullptr);
    Require(Kyumist::InputMap::IsActionPressed("dash"), "dash should be pressed");
    Require(Kyumist::InputMap::IsActionJustPressed("dash"), "dash should be just pressed on frame 1");

    Kyumist::InputMap::Update(nullptr);
    Require(!Kyumist::InputMap::IsActionJustPressed("dash"), "dash should not be just pressed on frame 2");

    Kyumist::InputMap::SetVirtualActionState("dash", false);
    Kyumist::InputMap::Update(nullptr);
    Require(Kyumist::InputMap::IsActionJustReleased("dash"), "dash should be just released");

    std::filesystem::remove_all(tmpDir, ec);
    std::cout << "[PASS] TestInputMapAndTouchControls\n";
}

void TestTouchControlsMultiTouch() {
    Kyumist::InputMap::InitDefaults();
    Kyumist::InputMap::ClearVirtualStates();

    auto& cfg = Kyumist::TouchControlsOverlay::GetConfig();
    cfg.enabledOnMobile = true;
    cfg.buttonAAction = "jump";
    cfg.buttonBAction = "attack";

    const float viewW = 1920.0f;
    const float viewH = 1080.0f;
    const float minDim = std::min(viewW, viewH);
    const float stickRadius = std::clamp(minDim * 0.115f, 44.0f, 130.0f);
    const float btnRadius = std::clamp(minDim * 0.075f, 28.0f, 82.0f);
    const float margin = stickRadius + std::clamp(minDim * 0.04f, 24.0f, 52.0f);

    const ImVec2 stickCenter(margin, viewH - margin);
    const ImVec2 btnACenter(viewW - margin + 10.0f, viewH - margin + 12.0f);
    const ImVec2 btnBCenter(btnACenter.x - btnRadius * 2.25f, btnACenter.y - btnRadius * 0.75f);

    // Frame 1: Left thumb moves stick right+up while right thumb simultaneously presses Button A ("jump")
    const std::array<Kyumist::TouchControlsOverlay::TouchPoint, 2> frame1Points{{
        {0, stickCenter.x + stickRadius * 0.75f, stickCenter.y - stickRadius * 0.50f, true},
        {1, btnACenter.x, btnACenter.y, true}
    }};
    Kyumist::TouchControlsOverlay::SetTouchPoints(frame1Points.data(), frame1Points.size());
    Kyumist::TouchControlsOverlay::RenderAndProcess(nullptr, ImVec2(0.0f, 0.0f), ImVec2(viewW, viewH), true);
    Kyumist::InputMap::Update(nullptr);

    Require(Kyumist::InputMap::GetTouchAxisX() > 0.45f, "Multi-touch stick should report positive X axis");
    Require(Kyumist::InputMap::GetTouchAxisY() > 0.30f, "Multi-touch stick should report positive Y axis");
    Require(Kyumist::InputMap::IsActionPressed("move_right"), "Multi-touch stick should activate move_right");
    Require(Kyumist::InputMap::IsActionPressed("move_forward"), "Multi-touch stick should activate move_forward");
    Require(Kyumist::InputMap::IsActionPressed("jump"), "Multi-touch should simultaneously press button A (jump)");
    Require(!Kyumist::InputMap::IsActionPressed("attack"), "Button B (attack) should not be pressed in frame 1");

    // Frame 2: Release stick, press both A and B
    const std::array<Kyumist::TouchControlsOverlay::TouchPoint, 2> frame2Points{{
        {1, btnACenter.x, btnACenter.y, true},
        {2, btnBCenter.x, btnBCenter.y, true}
    }};
    Kyumist::TouchControlsOverlay::SetTouchPoints(frame2Points.data(), frame2Points.size());
    Kyumist::TouchControlsOverlay::RenderAndProcess(nullptr, ImVec2(0.0f, 0.0f), ImVec2(viewW, viewH), true);
    Kyumist::InputMap::Update(nullptr);

    Require(std::abs(Kyumist::InputMap::GetTouchAxisX()) < 0.001f, "Released stick should reset X axis");
    Require(std::abs(Kyumist::InputMap::GetTouchAxisY()) < 0.001f, "Released stick should reset Y axis");
    Require(Kyumist::InputMap::IsActionPressed("jump"), "Button A should stay pressed");
    Require(Kyumist::InputMap::IsActionPressed("attack"), "Button B should be simultaneously pressed");

    // Cleanup
    Kyumist::TouchControlsOverlay::ClearTouchPoints();
    Kyumist::InputMap::ClearVirtualStates();
    Kyumist::InputMap::Update(nullptr);
    Require(!Kyumist::InputMap::IsActionPressed("jump"), "ClearVirtualStates should release jump");
    std::cout << "[PASS] TestTouchControlsMultiTouch\n";
}

void TestRmlUiHtmlCssDocument() {
    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path() / "kyumist_test_rmlui";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    const std::filesystem::path htmlPath = tmpDir / "hud.html";
    {
        std::ofstream out(htmlPath);
        out << "<rml><head><style>body { font-family: Roboto; width: 100%; height: 100%; display: flex; }</style></head>"
            << "<body><div id=\"hp\">HP: 100</div><button id=\"btn_start\">Play</button></body></rml>\n";
    }

    Kyumist::UISystem::Init(nullptr);
    Require(Kyumist::UISystem::IsInitialized(), "UISystem should initialize in headless mode");
    Require(Kyumist::UISystem::LoadDocument(htmlPath.string()), "UISystem should load HTML+CSS document");
    Require(Kyumist::UISystem::SetText("hp", "HP: 80"), "UISystem should update text by element id");
    Require(Kyumist::UISystem::SetVisible("btn_start", false), "UISystem should toggle visibility by element id");
    Kyumist::UISystem::UpdateAndRender(nullptr, ImVec2(0, 0), ImVec2(1280, 720), false);
    Kyumist::UISystem::Shutdown();

    std::filesystem::remove_all(tmpDir, ec);
    std::cout << "[PASS] TestRmlUiHtmlCssDocument\n";
}

int main() {
    Kyumist::Log::Init();

    TestECSAndTransform();
    TestSceneSerializationRoundTrip();
    TestPhysicsGravityAndBounce();
    TestAudioWavGeneration();
    TestProjectManagerCreateAndOpen();
    TestGltfAndMtlLoading();
    TestFullEngineCapabilities();
    TestInMemoryUndoRedoSerialization();
    TestKinematicCollisionResolution();
    TestInputMapAndTouchControls();
    TestTouchControlsMultiTouch();
    TestRmlUiHtmlCssDocument();

    if (g_Failures > 0) {
        std::cerr << g_Failures << " test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All KyumistTests passed!\n";
    return EXIT_SUCCESS;
}

