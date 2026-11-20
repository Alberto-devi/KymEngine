#include "audio/AudioEngine.hpp"
#include "core/Application.hpp"
#include "core/InputMap.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "core/Window.hpp"
#include "physics/PhysicsSystem.hpp"
#include "project/ProjectManager.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "scene/Components.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptEngine.hpp"
#include "ui/TouchControlsOverlay.hpp"
#include "ui/UISystem.hpp"

#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <imgui.h>
#include <imgui_impl_android.h>
#include <jni.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "KymEngine", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "KymEngine", __VA_ARGS__))

namespace {

class AndroidRuntimeApplication final : public Kyumist::Application {
public:
    AndroidRuntimeApplication(
        const Kyumist::ApplicationSpecification& spec,
        std::filesystem::path gameDir
    )
        : Kyumist::Application(spec),
          m_GameDir(std::move(gameDir)) {}

protected:
    void OnInit() override {
        Kyumist::AudioEngine::Init(false);
        Kyumist::UISystem::Init(GetRenderer());
        Kyumist::ScriptEngine::Init(GetWindow());

        std::error_code ec;
        std::filesystem::path scenePath;
        const std::filesystem::path projFile = m_GameDir / "project.kmproj";

        if (!m_GameDir.empty() && std::filesystem::exists(projFile, ec)) {
            if (Kyumist::ProjectManager::OpenProject(projFile)) {
                scenePath = Kyumist::ProjectManager::GetStartScenePath();
            }
        } else if (!m_GameDir.empty() && std::filesystem::exists(m_GameDir, ec)) {
            Kyumist::AudioEngine::SetAssetsDirectory(m_GameDir);
            if (std::filesystem::exists(m_GameDir / "scripts.dex", ec) ||
                std::filesystem::exists(m_GameDir / "scripts", ec)) {
                Kyumist::ScriptEngine::SetScriptsDirectory(m_GameDir / "scripts");
                Kyumist::ScriptEngine::CompileAndReloadScripts(nullptr, false);
            }
            const std::filesystem::path fallbackScene = m_GameDir / "scenes" / "default.kmscene";
            if (std::filesystem::exists(fallbackScene, ec)) {
                scenePath = fallbackScene;
            }
        }

        if (!scenePath.empty() && std::filesystem::exists(scenePath, ec)) {
            if (!Kyumist::SceneSerializer::LoadFromFile(m_Scene, scenePath)) {
                m_Scene.CreateDefaultScene();
            }
        } else {
            m_Scene.CreateDefaultScene();
        }

        Kyumist::ScriptEngine::OnRuntimeStart(&m_Scene);
        Kyumist::PhysicsSystem::OnRuntimeStart(&m_Scene);
        KM_INFO("KymEngine Android Runtime initialized (gameDir: '{}').", m_GameDir.string());
    }

    void OnUpdate(Kyumist::Timestep ts) override {
        m_LastTimestep = ts;

        Kyumist::InputMap::Update(nullptr);
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
            if (!std::filesystem::exists(target, ec) && !m_GameDir.empty()) {
                target = m_GameDir / target;
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

        glm::mat4 viewProj(1.0f);
        if (!m_Scene.ComputePrimaryCameraViewProjection(aspect, viewProj)) {
            const glm::mat4 proj = glm::perspective(glm::radians(48.0f), aspect, 0.1f, 200.0f);
            const glm::mat4 view = glm::lookAt(
                glm::vec3(0.0f, 3.0f, 8.0f),
                glm::vec3(0.0f, 0.5f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            );
            viewProj = proj * view;
        }
        GetRenderer()->SetActiveScene(&m_Scene, viewProj);
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
            bg->AddImage(
                static_cast<ImTextureID>(Kyumist::VkDescriptorSetToU64(sceneDesc)),
                ImVec2(0.0f, 0.0f),
                io.DisplaySize
            );

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
            char hud[48]{};
            std::snprintf(hud, sizeof(hud), "%.0f FPS", fps);
            const ImVec2 txtSize = ImGui::CalcTextSize(hud);
            const float boxW = txtSize.x + 20.0f;
            const float boxX = std::max(12.0f, io.DisplaySize.x - boxW - 14.0f);
            bg->AddRectFilled(
                ImVec2(boxX, 12.0f),
                ImVec2(boxX + boxW, 26.0f + txtSize.y),
                IM_COL32(15, 17, 24, 175),
                6.0f
            );
            bg->AddText(ImVec2(boxX + 10.0f, 19.0f), IM_COL32(230, 236, 245, 255), hud);

            Kyumist::UISystem::UpdateAndRender(bg, ImVec2(0.0f, 0.0f), io.DisplaySize, true);

            if (Kyumist::TouchControlsOverlay::ShouldShow(true)) {
                Kyumist::TouchControlsOverlay::RenderAndProcess(bg, ImVec2(0.0f, 0.0f), io.DisplaySize, true);
            }
        }
    }

    void OnShutdown() override {
        Kyumist::TouchControlsOverlay::ClearTouchPoints();
        Kyumist::ScriptEngine::OnRuntimeStop(&m_Scene);
        Kyumist::UISystem::Shutdown();
        Kyumist::ScriptEngine::Shutdown();
        Kyumist::AudioEngine::Shutdown();
        Kyumist::ProjectManager::CloseActiveProject();
        KM_INFO("KymEngine Android Runtime shut down cleanly.");
    }

private:
    std::filesystem::path m_GameDir;
    Kyumist::Scene m_Scene;
    Kyumist::Timestep m_LastTimestep;
};

AndroidRuntimeApplication* g_AppInstance = nullptr;

void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window != nullptr) {
            const int32_t rawW = ANativeWindow_getWidth(app->window);
            const int32_t rawH = ANativeWindow_getHeight(app->window);
            const uint32_t w = rawW > 0 ? static_cast<uint32_t>(rawW) : 1280u;
            const uint32_t h = rawH > 0 ? static_cast<uint32_t>(rawH) : 720u;
            Kyumist::Window::SetAndroidWindow(app->window, w, h);
            if (g_AppInstance && g_AppInstance->GetRenderer()) {
                g_AppInstance->GetRenderer()->ResumeSurface(app->window);
            }
        }
        break;
    case APP_CMD_TERM_WINDOW:
        Kyumist::TouchControlsOverlay::ClearTouchPoints();
        if (g_AppInstance && g_AppInstance->GetRenderer()) {
            g_AppInstance->GetRenderer()->SuspendSurface();
        }
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        if (app->window != nullptr) {
            const int32_t rawW = ANativeWindow_getWidth(app->window);
            const int32_t rawH = ANativeWindow_getHeight(app->window);
            if (rawW > 0 && rawH > 0) {
                Kyumist::Window::MarkAndroidResized(static_cast<uint32_t>(rawW), static_cast<uint32_t>(rawH));
            }
        }
        break;
    case APP_CMD_LOST_FOCUS:
        Kyumist::TouchControlsOverlay::ClearTouchPoints();
        Kyumist::InputMap::ClearVirtualStates();
        break;
    case APP_CMD_DESTROY:
        Kyumist::Window::RequestAndroidClose();
        if (g_AppInstance) {
            g_AppInstance->RequestClose();
        }
        break;
    default:
        break;
    }
}

int32_t handle_input(struct android_app* /*app*/, AInputEvent* event) {
    const int32_t imguiHandled = ImGui_ImplAndroid_HandleInputEvent(event);

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t rawAction = AMotionEvent_getAction(event);
        const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
        const int32_t upIndex =
            (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        std::vector<Kyumist::TouchControlsOverlay::TouchPoint> points;
        if (action != AMOTION_EVENT_ACTION_CANCEL) {
            const size_t count = AMotionEvent_getPointerCount(event);
            points.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const bool isPointerGoingUp =
                    (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) &&
                    static_cast<int32_t>(i) == upIndex;
                if (isPointerGoingUp) {
                    continue;
                }
                Kyumist::TouchControlsOverlay::TouchPoint pt;
                pt.pointerId = AMotionEvent_getPointerId(event, i);
                pt.x = AMotionEvent_getX(event, i);
                pt.y = AMotionEvent_getY(event, i);
                pt.active = true;
                points.push_back(pt);
            }
        }
        Kyumist::TouchControlsOverlay::SetTouchPoints(points.data(), points.size());
    }

    return imguiHandled;
}

void PollAndroidEvents(struct android_app* app) {
    int events = 0;
    struct android_poll_source* source = nullptr;
    const bool suspended = (g_AppInstance && g_AppInstance->GetRenderer() &&
                            g_AppInstance->GetRenderer()->IsSurfaceSuspended());
    int timeoutMs = suspended ? 50 : 0;
    while (ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
        timeoutMs = 0;
        if (source) {
            source->process(app, source);
        }
        if (app->destroyRequested) {
            Kyumist::Window::RequestAndroidClose();
            if (g_AppInstance) {
                g_AppInstance->RequestClose();
            }
            return;
        }
    }
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_dev_kyumist_android_KymEngineActivity_nativeSetAndroidContext(
    JNIEnv* env,
    jclass /*cls*/,
    jstring jGameDir,
    jclass nativeBridgeClass,
    jclass scriptHostClass
) {
    if (jGameDir) {
        const char* utf = env->GetStringUTFChars(jGameDir, nullptr);
        if (utf) {
            Kyumist::PlatformPaths::SetAndroidDataDir(Kyumist::PlatformPaths::Utf8ToPath(utf));
            env->ReleaseStringUTFChars(jGameDir, utf);
        }
    }

    JavaVM* jvm = nullptr;
    if (env->GetJavaVM(&jvm) == JNI_OK && jvm && nativeBridgeClass && scriptHostClass) {
        Kyumist::ScriptEngine::SetAndroidJvm(
            jvm,
            static_cast<jclass>(env->NewGlobalRef(nativeBridgeClass)),
            static_cast<jclass>(env->NewGlobalRef(scriptHostClass))
        );
    }
}

void android_main(struct android_app* app) {
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    // Wait until Android grants a valid ANativeWindow before creating Vulkan surface
    int events = 0;
    struct android_poll_source* source = nullptr;
    while (!app->destroyRequested && app->window == nullptr) {
        if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
        }
    }

    if (app->destroyRequested || app->window == nullptr) {
        return;
    }

    const int32_t initW = ANativeWindow_getWidth(app->window);
    const int32_t initH = ANativeWindow_getHeight(app->window);
    Kyumist::Window::SetAndroidWindow(
        app->window,
        initW > 0 ? static_cast<uint32_t>(initW) : 1280u,
        initH > 0 ? static_cast<uint32_t>(initH) : 720u
    );
    Kyumist::Window::SetAndroidEventPump([app]() {
        PollAndroidEvents(app);
    });

    Kyumist::ApplicationSpecification spec;
    spec.name = "KymEngine Game";
    spec.width = initW > 0 ? static_cast<uint32_t>(initW) : 1280u;
    spec.height = initH > 0 ? static_cast<uint32_t>(initH) : 720u;
    spec.headless = false;

    const std::filesystem::path gameDir = Kyumist::PlatformPaths::GetAndroidDataDir();
    LOGI("Starting KymEngine Android Runtime (gameDir: %s)", gameDir.string().c_str());

    try {
        AndroidRuntimeApplication runtimeApp(spec, gameDir);
        g_AppInstance = &runtimeApp;
        runtimeApp.Run();
        g_AppInstance = nullptr;
    } catch (const std::exception& ex) {
        g_AppInstance = nullptr;
        LOGE("Fatal KymEngine Android Runtime exception: %s", ex.what());
    } catch (...) {
        g_AppInstance = nullptr;
        LOGE("Fatal unknown exception in KymEngine Android Runtime.");
    }
}
