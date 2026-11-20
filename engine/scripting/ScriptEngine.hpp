#pragma once

#include "core/Timestep.hpp"

#include <entt/entt.hpp>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include <filesystem>
#include <string>
#include <vector>

namespace Kyumist {

class Scene;
class Window;

class ScriptEngine {
public:
#ifdef __ANDROID__
    static void SetAndroidJvm(JavaVM* jvm, jclass nativeBridgeClassGlobalRef, jclass scriptHostClassGlobalRef);
#endif
    static void Init(Window* window);
    static void Shutdown();

    static void SetScriptsDirectory(const std::filesystem::path& scriptsDir);
    static const std::filesystem::path& GetScriptsDirectory();

    static bool CompileAndReloadScripts(Scene* scene, bool isRuntimeActive);
    static const std::string& GetLastCompileOutput();
    static bool DidLastCompileSucceed();

    static void OnRuntimeStart(Scene* scene);
    static void OnRuntimeUpdate(Scene* scene, Timestep ts);
    static void OnRuntimeStop(Scene* scene);
    static void NotifyCollision(Scene* scene, entt::entity self, entt::entity other);
    static void NotifyUIEvent(const std::string& elementId, const std::string& eventType);

    static bool HasPendingSceneLoad();
    static std::string ConsumePendingSceneLoad();

    static std::vector<std::string> GetAvailableScriptNames();
    static bool IsInitialized();
};

} // namespace Kyumist
