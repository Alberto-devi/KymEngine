#include "audio/AudioEngine.hpp"
#include "core/Application.hpp"
#include "core/InputMap.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "physics/PhysicsSystem.hpp"
#include "project/ProjectManager.hpp"
#include "scene/Components.hpp"
#include "scene/EditorCamera.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptEngine.hpp"
#include "ui/TouchControlsOverlay.hpp"
#include "ui/UISystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

class RuntimeApplication final : public Kyumist::Application {
public:
    RuntimeApplication(
        const Kyumist::ApplicationSpecification& spec,
        std::filesystem::path scenePath,
        std::filesystem::path projectPath
    )
        : Kyumist::Application(spec),
          m_ScenePath(std::move(scenePath)),
          m_ProjectPath(std::move(projectPath)) {}

protected:
    void OnInit() override {
        Kyumist::AudioEngine::Init(GetSpecification().headless);
        Kyumist::UISystem::Init(GetRenderer());
        Kyumist::ScriptEngine::Init(GetWindow());

        bool projectOpened = false;
        if (!m_ProjectPath.empty() && std::filesystem::exists(m_ProjectPath)) {
            projectOpened = Kyumist::ProjectManager::OpenProject(m_ProjectPath);
        } else if (!m_ScenePath.empty()) {
            const std::filesystem::path parentDir = m_ScenePath.parent_path();
            const std::filesystem::path projRoot = (parentDir.filename() == "scenes")
                ? parentDir.parent_path()
                : parentDir;
            if (!projRoot.empty() && std::filesystem::exists(projRoot / "project.kmproj")) {
                projectOpened = Kyumist::ProjectManager::OpenProject(projRoot / "project.kmproj");
            } else if (!projRoot.empty() && std::filesystem::exists(projRoot)) {
                Kyumist::AudioEngine::SetAssetsDirectory(projRoot);
                if (std::filesystem::exists(projRoot / "scripts")) {
                    Kyumist::ScriptEngine::SetScriptsDirectory(projRoot / "scripts");
                    Kyumist::ScriptEngine::CompileAndReloadScripts(nullptr, false);
                }
            }
        }

        if (m_ScenePath.empty() && projectOpened) {
            m_ScenePath = Kyumist::ProjectManager::GetStartScenePath();
        }

        if (!m_ScenePath.empty() && std::filesystem::exists(m_ScenePath)) {
            if (!Kyumist::SceneSerializer::LoadFromFile(m_Scene, m_ScenePath)) {
                m_Scene.CreateDefaultScene();
            }
        } else {
            m_Scene.CreateDefaultScene();
        }

        Kyumist::ScriptEngine::OnRuntimeStart(&m_Scene);
        Kyumist::PhysicsSystem::OnRuntimeStart(&m_Scene);

        KM_INFO("Kyumist Runtime initialized.");
    }

    void OnUpdate(Kyumist::Timestep ts) override {
        ++m_FrameCount;
        m_LastTimestep = ts;

        GLFWwindow* nativeWindow = GetWindow() ? GetWindow()->GetNativeWindow() : nullptr;
        Kyumist::InputMap::Update(nativeWindow);

        Kyumist::ScriptEngine::OnRuntimeUpdate(&m_Scene, ts);
        Kyumist::PhysicsSystem::Step(&m_Scene, ts);
        m_Scene.UpdateAnimations(ts.GetSeconds());

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
            } else {
                KM_WARN("Requested scene not found: '{}'.", relScene);
            }
        }

        const VkExtent2D extent = GetRenderer()->GetSceneExtent();
        const float aspect = extent.height > 0
            ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
            : 16.0f / 9.0f;

        glm::mat4 viewProj = m_Camera.GetViewProjection(aspect);
        glm::mat4 gameCamVP(1.0f);
        if (m_Scene.ComputePrimaryCameraViewProjection(aspect, gameCamVP)) {
            viewProj = gameCamVP;
        }
        GetRenderer()->SetActiveScene(&m_Scene, viewProj);

        if (GetSpecification().maxFrames > 0) {
            KM_TRACE("Runtime Frame {} - dt: {:.3f} ms", m_FrameCount, ts.GetMilliseconds());
        }
    }

    void OnImGuiRender() override {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x >= 64.0f && io.DisplaySize.y >= 64.0f) {
            const uint32_t w = static_cast<uint32_t>(io.DisplaySize.x);
            const uint32_t h = static_cast<uint32_t>(io.DisplaySize.y);
            const VkExtent2D extent = GetRenderer()->GetSceneExtent();
            if (w != extent.width || h != extent.height) {
                GetRenderer()->RequestSceneResize(w, h);
            }
        }

        VkDescriptorSet sceneDesc = GetRenderer()->GetSceneTextureDescriptor();
        if (sceneDesc != VK_NULL_HANDLE) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            bg->AddImage(reinterpret_cast<ImTextureID>(sceneDesc), ImVec2(0.0f, 0.0f), io.DisplaySize);

            // Render in-game UI Text HUD components
            const auto& reg = m_Scene.GetRegistry();
            ImFont* font = ImGui::GetFont();
            const float baseSize = ImGui::GetFontSize();
            for (auto entity : reg.view<Kyumist::UITextComponent>()) {
                const auto& ui = reg.get<Kyumist::UITextComponent>(entity);
                if (!ui.visible || ui.text.empty()) {
                    continue;
                }
                const ImU32 col = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(ui.color.r, ui.color.g, ui.color.b, ui.color.a)
                );
                bg->AddText(
                    font,
                    baseSize * std::clamp(ui.fontScale, 0.5f, 4.0f),
                    ImVec2(ui.screenPos.x, ui.screenPos.y),
                    col,
                    ui.text.c_str()
                );
            }

            const float fps = (m_LastTimestep.GetSeconds() > 0.00001f)
                ? (1.0f / m_LastTimestep.GetSeconds())
                : 0.0f;
            char hud[96]{};
            std::snprintf(hud, sizeof(hud), "Modo Juego (Sin Edicion) | %.0f FPS | ESC para salir", fps);
            const ImVec2 txtSize = ImGui::CalcTextSize(hud);
            const float boxW = txtSize.x + 20.0f;
            const float boxX = std::max(12.0f, io.DisplaySize.x - boxW - 14.0f);
            bg->AddRectFilled(
                ImVec2(boxX, 12.0f),
                ImVec2(boxX + boxW, 26.0f + txtSize.y),
                IM_COL32(15, 17, 24, 190),
                6.0f
            );
            bg->AddText(ImVec2(boxX + 10.0f, 19.0f), IM_COL32(230, 236, 245, 255), hud);

            Kyumist::UISystem::UpdateAndRender(bg, ImVec2(0.0f, 0.0f), io.DisplaySize, true);
#ifdef __ANDROID__
            constexpr bool isMobile = true;
#else
            constexpr bool isMobile = false;
#endif
            if (Kyumist::TouchControlsOverlay::ShouldShow(isMobile)) {
                Kyumist::TouchControlsOverlay::RenderAndProcess(bg, ImVec2(0.0f, 0.0f), io.DisplaySize, true);
            }
        }
    }

    void OnShutdown() override {
        Kyumist::ScriptEngine::OnRuntimeStop(&m_Scene);
        Kyumist::UISystem::Shutdown();
        Kyumist::ScriptEngine::Shutdown();
        Kyumist::AudioEngine::Shutdown();
        KM_INFO("Kyumist Runtime shutting down cleanly.");
    }

private:
    std::filesystem::path m_ScenePath;
    std::filesystem::path m_ProjectPath;
    Kyumist::Scene m_Scene;
    Kyumist::EditorCamera m_Camera;
    Kyumist::Timestep m_LastTimestep;
    uint32_t m_FrameCount = 0;
};

int main(int argc, char** argv) {
    Kyumist::ApplicationSpecification spec;
    spec.name = "Kyumist Game";
    spec.width = 1280;
    spec.height = 720;

#ifdef KM_ASSETS_DIR
    std::filesystem::path scenePath = std::filesystem::path(KM_ASSETS_DIR) / "scenes" / "default.kmscene";
#else
    std::filesystem::path scenePath = std::filesystem::path("assets") / "scenes" / "default.kmscene";
#endif
    std::filesystem::path projectPath;
    bool explicitScene = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            spec.headless = true;
        } else if (arg == "--max-frames" && i + 1 < argc) {
            try {
                spec.maxFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                std::cerr << "Invalid value for --max-frames\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--scene" && i + 1 < argc) {
            scenePath = argv[++i];
            explicitScene = true;
        } else if (arg == "--project" && i + 1 < argc) {
            projectPath = argv[++i];
            if (!explicitScene) {
                scenePath.clear();
            }
        } else if (arg == "--title" && i + 1 < argc) {
            spec.name = argv[++i];
        }
    }

    if (projectPath.empty() && !explicitScene) {
        std::error_code ec;
        const std::filesystem::path exeProj = Kyumist::PlatformPaths::GetExecutableDir() / "project.kmproj";
        const std::filesystem::path cwdProj = std::filesystem::current_path(ec) / "project.kmproj";
        if (std::filesystem::exists(exeProj, ec)) {
            projectPath = exeProj;
            scenePath.clear();
        } else if (std::filesystem::exists(cwdProj, ec)) {
            projectPath = cwdProj;
            scenePath.clear();
        }
    }

    try {
        RuntimeApplication app(spec, std::move(scenePath), std::move(projectPath));
        app.Run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
