#include "audio/AudioEngine.hpp"
#include "core/Application.hpp"
#include "core/InputMap.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "panels/EditorPanels.hpp"
#include "physics/PhysicsSystem.hpp"
#include "project/ProjectManager.hpp"
#include "scene/EditorCamera.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptEngine.hpp"
#include "ui/UISystem.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

class EditorApplication final : public Kyumist::Application {
public:
    EditorApplication(const Kyumist::ApplicationSpecification& spec, std::filesystem::path initialProject)
        : Kyumist::Application(spec), m_InitialProject(std::move(initialProject)) {}

protected:
    void OnInit() override {
        m_Panels.SetHeadlessMode(GetSpecification().headless);
        Kyumist::AudioEngine::Init(GetSpecification().headless);
        Kyumist::UISystem::Init(GetRenderer());
        Kyumist::ScriptEngine::Init(GetWindow());
        Kyumist::ProjectManager::EnsureSampleProjects();

        bool projectOpened = false;
        if (!m_InitialProject.empty()) {
            projectOpened = Kyumist::ProjectManager::OpenProject(m_InitialProject);
        } else if (GetSpecification().headless || GetSpecification().maxFrames > 0) {
            const auto samples = Kyumist::ProjectManager::GetSampleProjects();
            if (!samples.empty()) {
                projectOpened = Kyumist::ProjectManager::OpenProject(samples.back().projectFilePath);
            }
        }

        if (projectOpened) {
            m_Panels.SyncProjectState(m_Scene);
            if (GetSpecification().maxFrames > 0) {
                m_Panels.StartPlayMode(m_Scene);
            }
        } else {
            m_Scene.CreateDefaultScene();
        }

        KM_INFO("Kyumist Editor initialized (Project Manager + Integrated Script Editor ready).");
    }

    void OnUpdate(Kyumist::Timestep ts) override {
        ++m_FrameCount;
        m_LastTimestep = ts;

        GLFWwindow* nativeWindow = GetWindow() ? GetWindow()->GetNativeWindow() : nullptr;

        if (m_Panels.IsPlayingInProcess()) {
            Kyumist::InputMap::Update(nativeWindow);
            Kyumist::ScriptEngine::OnRuntimeUpdate(&m_Scene, ts);
            Kyumist::PhysicsSystem::Step(&m_Scene, ts);

            if (Kyumist::ScriptEngine::HasPendingSceneLoad()) {
                const std::string relScene = Kyumist::ScriptEngine::ConsumePendingSceneLoad();
                std::filesystem::path target = Kyumist::PlatformPaths::Utf8ToPath(relScene);
                std::error_code ec;
                if (!std::filesystem::exists(target, ec) && Kyumist::ProjectManager::HasActiveProject()) {
                    target = Kyumist::ProjectManager::GetActiveProjectDir() / target;
                }
                if (std::filesystem::exists(target, ec)) {
                    Kyumist::ScriptEngine::OnRuntimeStop(&m_Scene);
                    Kyumist::UISystem::UnloadAllDocuments();
                    Kyumist::SceneSerializer::LoadFromFile(m_Scene, target);
                    Kyumist::ScriptEngine::OnRuntimeStart(&m_Scene);
                    Kyumist::PhysicsSystem::OnRuntimeStart(&m_Scene);
                }
            }
        }

        m_EditorCamera.OnUpdate(ts, nativeWindow, m_Panels.IsViewportHovered(), m_Panels.ConsumeScrollDelta());

        const VkExtent2D extent = GetRenderer()->GetSceneExtent();
        const float aspect = extent.height > 0
            ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
            : 16.0f / 9.0f;

        glm::mat4 viewProj = m_EditorCamera.GetViewProjection(aspect);
        if (m_Panels.IsPlaying()) {
            glm::mat4 gameCamVP(1.0f);
            if (m_Scene.ComputePrimaryCameraViewProjection(aspect, gameCamVP)) {
                viewProj = gameCamVP;
            }
        }
        GetRenderer()->SetActiveScene(&m_Scene, viewProj);

        if (GetSpecification().maxFrames > 0) {
            KM_TRACE("Frame {} - dt: {:.3f} ms", m_FrameCount, ts.GetMilliseconds());
        }
    }

    void OnImGuiRender() override {
        bool shouldClose = false;
        m_Panels.Render(GetRenderer(), m_Scene, m_EditorCamera, m_LastTimestep, m_FrameCount, shouldClose);
        if (shouldClose) {
            RequestClose();
        }
    }

    void OnShutdown() override {
        if (m_Panels.IsPlaying()) {
            m_Panels.StopPlayMode(m_Scene);
        }
        Kyumist::UISystem::Shutdown();
        Kyumist::ScriptEngine::Shutdown();
        Kyumist::AudioEngine::Shutdown();
        KM_INFO("Kyumist Editor shutting down cleanly.");
    }

private:
    std::filesystem::path m_InitialProject;
    Kyumist::Scene m_Scene;
    Kyumist::EditorCamera m_EditorCamera;
    Kyumist::EditorPanels m_Panels;
    Kyumist::Timestep m_LastTimestep;
    uint32_t m_FrameCount = 0;
};

int main(int argc, char** argv) {
    Kyumist::ApplicationSpecification spec;
    spec.name = "KymEngine";
    spec.width = 1280;
    spec.height = 720;
    std::filesystem::path initialProject;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            spec.headless = true;
        } else if (arg == "--max-frames" && i + 1 < argc) {
            try {
                const unsigned long parsed = std::stoul(argv[++i]);
                spec.maxFrames = static_cast<uint32_t>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid value for --max-frames\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--project" && i + 1 < argc) {
            initialProject = argv[++i];
        }
    }

    try {
        EditorApplication app(spec, std::move(initialProject));
        app.Run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
