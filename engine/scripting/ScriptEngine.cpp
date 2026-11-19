#include "scripting/ScriptEngine.hpp"

#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "core/Window.hpp"
#include "physics/PhysicsSystem.hpp"
#include "scene/Components.hpp"
#include "scene/Scene.hpp"

#include <GLFW/glfw3.h>
#include <jni.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kyumist {

namespace {

struct ScriptEngineState {
    JavaVM* jvm = nullptr;
    JNIEnv* env = nullptr;
    Window* window = nullptr;
    Scene* activeScene = nullptr;
    std::filesystem::path scriptsDir;
    std::string lastCompileOutput;
    bool lastCompileSuccess = true;

    jclass scriptHostClass = nullptr;
    jmethodID reloadClassLoaderMethod = nullptr;
    jmethodID createInstanceMethod = nullptr;
    jmethodID updateInstanceMethod = nullptr;
    jmethodID dispatchCollisionMethod = nullptr;
    jmethodID destroyInstanceMethod = nullptr;
    jmethodID destroyAllMethod = nullptr;
};

ScriptEngineState s_State;

std::filesystem::path ResolveJavaApiJar() {
    return PlatformPaths::ResolveJavaApiJar();
}

std::filesystem::path ResolveScriptsSourceDir() {
    if (!s_State.scriptsDir.empty()) {
        return s_State.scriptsDir;
    }
    return PlatformPaths::ResolveAssetsDir() / "scripts";
}

std::filesystem::path ResolveScriptsBuildDir() {
    std::error_code ec;
#ifdef KM_SCRIPTS_BUILD_DIR
    const std::filesystem::path devDir(KM_SCRIPTS_BUILD_DIR);
    if (std::filesystem::exists(devDir.parent_path(), ec) && PlatformPaths::IsDirectoryWritable(devDir.parent_path())) {
        return devDir;
    }
#endif
    if (std::filesystem::exists("build", ec) && PlatformPaths::IsDirectoryWritable("build")) {
        return std::filesystem::path("build/user-scripts-classes");
    }
    return std::filesystem::temp_directory_path(ec) / "KymEngine" / "user-scripts-classes";
}

std::string ResolveJavacExecutable() {
    std::error_code ec;
    const std::filesystem::path exeDir = PlatformPaths::GetExecutableDir();
#ifdef _WIN32
    const std::filesystem::path bundledJavac = exeDir / "jre" / "bin" / "javac.exe";
#else
    const std::filesystem::path bundledJavac = exeDir / "jre" / "bin" / "javac";
#endif
    if (std::filesystem::exists(bundledJavac, ec)) {
        return PlatformPaths::PathToUtf8(bundledJavac);
    }

    if (const char* javaHome = std::getenv("JAVA_HOME")) {
        if (*javaHome != '\0') {
#ifdef _WIN32
            const std::filesystem::path candidate = std::filesystem::path(javaHome) / "bin" / "javac.exe";
#else
            const std::filesystem::path candidate = std::filesystem::path(javaHome) / "bin" / "javac";
#endif
            if (std::filesystem::exists(candidate, ec)) {
                return candidate.string();
            }
        }
    }
#ifdef KM_JAVAC_EXECUTABLE
    if (std::filesystem::exists(KM_JAVAC_EXECUTABLE, ec)) {
        return KM_JAVAC_EXECUTABLE;
    }
#endif
    return "javac";
}

bool CheckAndClearException(JNIEnv* env, const char* context) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    KM_CORE_ERROR("Java exception thrown in {}:", context);
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

std::string JStringToStdString(JNIEnv* env, jstring jstr) {
    if (!jstr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) {
        return {};
    }
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

entt::entity ToEntity(jlong id) {
    return static_cast<entt::entity>(static_cast<uint32_t>(id));
}

// --- JNI Native Callbacks for dev.kyumist.NativeBridge ---

void JNICALL Native_LogInfo(JNIEnv* env, jclass /*cls*/, jstring message) {
    KM_INFO("[Java] {}", JStringToStdString(env, message));
}

void JNICALL Native_LogWarn(JNIEnv* env, jclass /*cls*/, jstring message) {
    KM_WARN("[Java] {}", JStringToStdString(env, message));
}

void JNICALL Native_LogError(JNIEnv* env, jclass /*cls*/, jstring message) {
    KM_ERROR("[Java] {}", JStringToStdString(env, message));
}

void JNICALL Native_GetPosition(JNIEnv* env, jclass /*cls*/, jlong entityId, jfloatArray outXYZ) {
    if (!s_State.activeScene || !outXYZ || env->GetArrayLength(outXYZ) < 3) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (const auto* tr = s_State.activeScene->GetRegistry().try_get<TransformComponent>(entity)) {
        const std::array<jfloat, 3> vals{tr->translation.x, tr->translation.y, tr->translation.z};
        env->SetFloatArrayRegion(outXYZ, 0, 3, vals.data());
    }
}

void JNICALL Native_SetPosition(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat x, jfloat y, jfloat z) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* tr = s_State.activeScene->GetRegistry().try_get<TransformComponent>(entity)) {
        tr->translation = {x, y, z};
    }
}

void JNICALL Native_GetRotation(JNIEnv* env, jclass /*cls*/, jlong entityId, jfloatArray outXYZ) {
    if (!s_State.activeScene || !outXYZ || env->GetArrayLength(outXYZ) < 3) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (const auto* tr = s_State.activeScene->GetRegistry().try_get<TransformComponent>(entity)) {
        const std::array<jfloat, 3> vals{tr->rotationDegrees.x, tr->rotationDegrees.y, tr->rotationDegrees.z};
        env->SetFloatArrayRegion(outXYZ, 0, 3, vals.data());
    }
}

void JNICALL Native_SetRotation(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat x, jfloat y, jfloat z) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* tr = s_State.activeScene->GetRegistry().try_get<TransformComponent>(entity)) {
        tr->rotationDegrees = {x, y, z};
    }
}

void JNICALL Native_GetScale(JNIEnv* env, jclass /*cls*/, jlong entityId, jfloatArray outXYZ) {
    if (!s_State.activeScene || !outXYZ || env->GetArrayLength(outXYZ) < 3) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (const auto* tr = s_State.activeScene->GetRegistry().try_get<TransformComponent>(entity)) {
        const std::array<jfloat, 3> vals{tr->scale.x, tr->scale.y, tr->scale.z};
        env->SetFloatArrayRegion(outXYZ, 0, 3, vals.data());
    }
}

void JNICALL Native_SetScale(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat x, jfloat y, jfloat z) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* tr = s_State.activeScene->GetRegistry().try_get<TransformComponent>(entity)) {
        tr->scale = {x, y, z};
    }
}

void JNICALL Native_SetColor(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    auto& reg = s_State.activeScene->GetRegistry();
    if (auto* mesh = reg.try_get<MeshRendererComponent>(entity)) {
        mesh->color = {r, g, b, a};
    }
    if (auto* spr = reg.try_get<SpriteRendererComponent>(entity)) {
        spr->color = {r, g, b, a};
    }
}

jboolean JNICALL Native_IsKeyDown(JNIEnv* /*env*/, jclass /*cls*/, jint keyCode) {
    if (!s_State.window || !s_State.window->GetNativeWindow()) {
        return JNI_FALSE;
    }
    return glfwGetKey(s_State.window->GetNativeWindow(), static_cast<int>(keyCode)) == GLFW_PRESS
        ? JNI_TRUE
        : JNI_FALSE;
}

void JNICALL Native_GetVelocity(JNIEnv* env, jclass /*cls*/, jlong entityId, jfloatArray outXYZ) {
    if (!s_State.activeScene || !outXYZ || env->GetArrayLength(outXYZ) < 3) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (const auto* rb = s_State.activeScene->GetRegistry().try_get<RigidBodyComponent>(entity)) {
        const std::array<jfloat, 3> vals{rb->velocity.x, rb->velocity.y, rb->velocity.z};
        env->SetFloatArrayRegion(outXYZ, 0, 3, vals.data());
    }
}

void JNICALL Native_SetVelocity(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat vx, jfloat vy, jfloat vz) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* rb = s_State.activeScene->GetRegistry().try_get<RigidBodyComponent>(entity)) {
        rb->velocity = {vx, vy, vz};
    }
}

void JNICALL Native_ApplyImpulse(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat ix, jfloat iy, jfloat iz) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* rb = s_State.activeScene->GetRegistry().try_get<RigidBodyComponent>(entity)) {
        if (rb->bodyType == RigidBodyType::Dynamic) {
            const float invMass = (rb->mass > 0.0001f) ? (1.0f / rb->mass) : 1.0f;
            rb->velocity += glm::vec3(ix, iy, iz) * invMass;
        }
    }
}

void JNICALL Native_PlaySound(JNIEnv* env, jclass /*cls*/, jstring relativePath, jfloat volume) {
    AudioEngine::PlaySound(JStringToStdString(env, relativePath), static_cast<float>(volume));
}

jstring JNICALL Native_GetName(JNIEnv* env, jclass /*cls*/, jlong entityId) {
    if (!s_State.activeScene) {
        return env->NewStringUTF("");
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return env->NewStringUTF("");
    }
    if (const auto* tag = s_State.activeScene->GetRegistry().try_get<TagComponent>(entity)) {
        return env->NewStringUTF(tag->tag.c_str());
    }
    return env->NewStringUTF("");
}

void JNICALL Native_SetName(JNIEnv* env, jclass /*cls*/, jlong entityId, jstring name) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* tag = s_State.activeScene->GetRegistry().try_get<TagComponent>(entity)) {
        tag->tag = JStringToStdString(env, name);
    }
}

jlong JNICALL Native_FindEntityByName(JNIEnv* env, jclass /*cls*/, jstring name) {
    if (!s_State.activeScene) {
        return -1;
    }
    const entt::entity found = s_State.activeScene->FindEntityByName(JStringToStdString(env, name));
    if (found == entt::null || !s_State.activeScene->IsValid(found)) {
        return -1;
    }
    return static_cast<jlong>(entt::to_integral(found));
}

jlong JNICALL Native_SpawnCube(JNIEnv* env, jclass /*cls*/, jstring name, jfloat x, jfloat y, jfloat z) {
    if (!s_State.activeScene) {
        return -1;
    }
    const std::string entName = JStringToStdString(env, name);
    entt::entity e = s_State.activeScene->CreateEntity(entName);
    auto& reg = s_State.activeScene->GetRegistry();
    reg.get<TransformComponent>(e).translation = {x, y, z};
    auto& mesh = reg.emplace<MeshRendererComponent>(e);
    mesh.type = MeshType::Cube;
    mesh.color = {0.92f, 0.45f, 0.22f, 1.0f};
    auto& rb = reg.emplace<RigidBodyComponent>(e);
    rb.bodyType = RigidBodyType::Dynamic;
    rb.shape = ColliderShape::Box3D;
    rb.halfExtents = {0.5f, 0.5f, 0.5f};
    return static_cast<jlong>(entt::to_integral(e));
}

void JNICALL Native_DestroyEntity(JNIEnv* env, jclass /*cls*/, jlong entityId) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (s_State.scriptHostClass && s_State.destroyInstanceMethod) {
        env->CallStaticVoidMethod(s_State.scriptHostClass, s_State.destroyInstanceMethod, entityId);
        CheckAndClearException(env, "ScriptHost.destroyInstance");
    }
    s_State.activeScene->DestroyEntity(entity);
}

void JNICALL Native_SetUIText(JNIEnv* env, jclass /*cls*/, jlong entityId, jstring text) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* ui = s_State.activeScene->GetRegistry().try_get<UITextComponent>(entity)) {
        ui->text = JStringToStdString(env, text);
    }
}

jboolean JNICALL Native_IsMouseButtonDown(JNIEnv* /*env*/, jclass /*cls*/, jint button) {
    if (!s_State.window || !s_State.window->GetNativeWindow()) {
        return JNI_FALSE;
    }
    return glfwGetMouseButton(s_State.window->GetNativeWindow(), static_cast<int>(button)) == GLFW_PRESS
        ? JNI_TRUE
        : JNI_FALSE;
}

void JNICALL Native_GetMousePosition(JNIEnv* env, jclass /*cls*/, jfloatArray outXY) {
    if (!s_State.window || !s_State.window->GetNativeWindow() || !outXY || env->GetArrayLength(outXY) < 2) {
        return;
    }
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(s_State.window->GetNativeWindow(), &mx, &my);
    const std::array<jfloat, 2> vals{static_cast<jfloat>(mx), static_cast<jfloat>(my)};
    env->SetFloatArrayRegion(outXY, 0, 2, vals.data());
}

void JNICALL Native_GetAngularVelocity(JNIEnv* env, jclass /*cls*/, jlong entityId, jfloatArray outXYZ) {
    if (!s_State.activeScene || !outXYZ || env->GetArrayLength(outXYZ) < 3) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (const auto* rb = s_State.activeScene->GetRegistry().try_get<RigidBodyComponent>(entity)) {
        const std::array<jfloat, 3> vals{rb->angularVelocity.x, rb->angularVelocity.y, rb->angularVelocity.z};
        env->SetFloatArrayRegion(outXYZ, 0, 3, vals.data());
    }
}

void JNICALL Native_SetAngularVelocity(JNIEnv* /*env*/, jclass /*cls*/, jlong entityId, jfloat wx, jfloat wy, jfloat wz) {
    if (!s_State.activeScene) {
        return;
    }
    const entt::entity entity = ToEntity(entityId);
    if (!s_State.activeScene->IsValid(entity)) {
        return;
    }
    if (auto* rb = s_State.activeScene->GetRegistry().try_get<RigidBodyComponent>(entity)) {
        rb->angularVelocity = {wx, wy, wz};
    }
}

jlong JNICALL Native_Raycast(
    JNIEnv* /*env*/,
    jclass /*cls*/,
    jfloat ox, jfloat oy, jfloat oz,
    jfloat dx, jfloat dy, jfloat dz,
    jfloat maxDist
) {
    if (!s_State.activeScene) {
        return -1;
    }
    const RaycastHit hit = PhysicsSystem::Raycast(s_State.activeScene, {ox, oy, oz}, {dx, dy, dz}, maxDist);
    if (!hit.hit || !s_State.activeScene->IsValid(hit.entity)) {
        return -1;
    }
    return static_cast<jlong>(entt::to_integral(hit.entity));
}

bool RegisterNativeBridge(JNIEnv* env) {
    jclass bridgeClass = env->FindClass("dev/kyumist/NativeBridge");
    if (!bridgeClass || CheckAndClearException(env, "FindClass(NativeBridge)")) {
        return false;
    }

    auto makeMethod = [](const char* name, const char* sig, void* fn) {
        return JNINativeMethod{const_cast<char*>(name), const_cast<char*>(sig), fn};
    };

    const std::array<JNINativeMethod, 26> methods{{
        makeMethod("logInfo",            "(Ljava/lang/String;)V",       reinterpret_cast<void*>(Native_LogInfo)),
        makeMethod("logWarn",            "(Ljava/lang/String;)V",       reinterpret_cast<void*>(Native_LogWarn)),
        makeMethod("logError",           "(Ljava/lang/String;)V",       reinterpret_cast<void*>(Native_LogError)),
        makeMethod("getName",            "(J)Ljava/lang/String;",       reinterpret_cast<void*>(Native_GetName)),
        makeMethod("setName",            "(JLjava/lang/String;)V",      reinterpret_cast<void*>(Native_SetName)),
        makeMethod("findEntityByName",   "(Ljava/lang/String;)J",       reinterpret_cast<void*>(Native_FindEntityByName)),
        makeMethod("spawnCube",          "(Ljava/lang/String;FFF)J",    reinterpret_cast<void*>(Native_SpawnCube)),
        makeMethod("destroyEntity",      "(J)V",                        reinterpret_cast<void*>(Native_DestroyEntity)),
        makeMethod("getPosition",        "(J[F)V",                      reinterpret_cast<void*>(Native_GetPosition)),
        makeMethod("setPosition",        "(JFFF)V",                     reinterpret_cast<void*>(Native_SetPosition)),
        makeMethod("getRotation",        "(J[F)V",                      reinterpret_cast<void*>(Native_GetRotation)),
        makeMethod("setRotation",        "(JFFF)V",                     reinterpret_cast<void*>(Native_SetRotation)),
        makeMethod("getScale",           "(J[F)V",                      reinterpret_cast<void*>(Native_GetScale)),
        makeMethod("setScale",           "(JFFF)V",                     reinterpret_cast<void*>(Native_SetScale)),
        makeMethod("setColor",           "(JFFFF)V",                    reinterpret_cast<void*>(Native_SetColor)),
        makeMethod("setUIText",          "(JLjava/lang/String;)V",      reinterpret_cast<void*>(Native_SetUIText)),
        makeMethod("isKeyDown",          "(I)Z",                        reinterpret_cast<void*>(Native_IsKeyDown)),
        makeMethod("isMouseButtonDown",  "(I)Z",                        reinterpret_cast<void*>(Native_IsMouseButtonDown)),
        makeMethod("getMousePosition",   "([F)V",                       reinterpret_cast<void*>(Native_GetMousePosition)),
        makeMethod("getVelocity",        "(J[F)V",                      reinterpret_cast<void*>(Native_GetVelocity)),
        makeMethod("setVelocity",        "(JFFF)V",                     reinterpret_cast<void*>(Native_SetVelocity)),
        makeMethod("getAngularVelocity", "(J[F)V",                      reinterpret_cast<void*>(Native_GetAngularVelocity)),
        makeMethod("setAngularVelocity", "(JFFF)V",                     reinterpret_cast<void*>(Native_SetAngularVelocity)),
        makeMethod("applyImpulse",       "(JFFF)V",                     reinterpret_cast<void*>(Native_ApplyImpulse)),
        makeMethod("raycast",            "(FFFFFFF)J",                  reinterpret_cast<void*>(Native_Raycast)),
        makeMethod("playSound",          "(Ljava/lang/String;F)V",      reinterpret_cast<void*>(Native_PlaySound))
    }};

    const jint res = env->RegisterNatives(bridgeClass, methods.data(), static_cast<jint>(methods.size()));
    env->DeleteLocalRef(bridgeClass);
    if (res != JNI_OK || CheckAndClearException(env, "RegisterNatives(NativeBridge)")) {
        return false;
    }
    return true;
}

} // namespace

void ScriptEngine::Init(Window* window) {
    if (s_State.jvm) {
        return;
    }
    s_State.window = window;

    const std::filesystem::path jarPath = ResolveJavaApiJar();
    if (!std::filesystem::exists(jarPath)) {
        KM_CORE_ERROR("kyumist-api.jar not found at '{}'. Java scripting disabled.", jarPath.string());
        return;
    }

    std::string classPathOpt = "-Djava.class.path=" + jarPath.string();
    std::string zgcOpt = "-XX:+UseZGC";

    std::array<JavaVMOption, 2> optionsWithZgc{};
    optionsWithZgc[0].optionString = classPathOpt.data();
    optionsWithZgc[1].optionString = zgcOpt.data();

    JavaVMInitArgs vmArgs{};
    vmArgs.version = JNI_VERSION_21;
    vmArgs.nOptions = 2;
    vmArgs.options = optionsWithZgc.data();
    vmArgs.ignoreUnrecognized = JNI_FALSE;

    jint res = JNI_CreateJavaVM(&s_State.jvm, reinterpret_cast<void**>(&s_State.env), &vmArgs);
    if (res == JNI_OK) {
        KM_CORE_INFO("Embedded JVM initialized (ZGC active, classpath: {}).", jarPath.string());
    } else {
        KM_CORE_WARN("JNI_CreateJavaVM with ZGC returned {} — retrying with default GC.", res);
        JavaVMOption fallbackOption{};
        fallbackOption.optionString = classPathOpt.data();
        vmArgs.nOptions = 1;
        vmArgs.options = &fallbackOption;
        res = JNI_CreateJavaVM(&s_State.jvm, reinterpret_cast<void**>(&s_State.env), &vmArgs);
        if (res != JNI_OK) {
            KM_CORE_ERROR("Failed to initialize embedded JVM (error {}).", res);
            s_State.jvm = nullptr;
            s_State.env = nullptr;
            return;
        }
        KM_CORE_INFO("Embedded JVM initialized (default GC, classpath: {}).", jarPath.string());
    }

    if (!RegisterNativeBridge(s_State.env)) {
        KM_CORE_ERROR("Failed to register JNI native methods for dev.kyumist.NativeBridge.");
        Shutdown();
        return;
    }

    jclass localHost = s_State.env->FindClass("dev/kyumist/ScriptHost");
    if (!localHost || CheckAndClearException(s_State.env, "FindClass(ScriptHost)")) {
        KM_CORE_ERROR("Failed to locate dev.kyumist.ScriptHost.");
        Shutdown();
        return;
    }

    s_State.scriptHostClass = static_cast<jclass>(s_State.env->NewGlobalRef(localHost));
    s_State.env->DeleteLocalRef(localHost);

    s_State.reloadClassLoaderMethod = s_State.env->GetStaticMethodID(
        s_State.scriptHostClass, "reloadClassLoader", "(Ljava/lang/String;)Z");
    s_State.createInstanceMethod = s_State.env->GetStaticMethodID(
        s_State.scriptHostClass, "createInstance", "(JLjava/lang/String;)Z");
    s_State.updateInstanceMethod = s_State.env->GetStaticMethodID(
        s_State.scriptHostClass, "updateInstance", "(JF)V");
    s_State.dispatchCollisionMethod = s_State.env->GetStaticMethodID(
        s_State.scriptHostClass, "dispatchCollision", "(JJ)V");
    s_State.destroyInstanceMethod = s_State.env->GetStaticMethodID(
        s_State.scriptHostClass, "destroyInstance", "(J)V");
    s_State.destroyAllMethod = s_State.env->GetStaticMethodID(
        s_State.scriptHostClass, "destroyAll", "()V");

    if (CheckAndClearException(s_State.env, "ScriptHost method lookup") ||
        !s_State.reloadClassLoaderMethod || !s_State.createInstanceMethod ||
        !s_State.updateInstanceMethod || !s_State.dispatchCollisionMethod ||
        !s_State.destroyInstanceMethod || !s_State.destroyAllMethod) {
        KM_CORE_ERROR("Failed to resolve ScriptHost JNI methods.");
        Shutdown();
        return;
    }

    CompileAndReloadScripts(nullptr, false);
}

void ScriptEngine::Shutdown() {
    if (!s_State.jvm) {
        return;
    }

    if (s_State.env) {
        if (s_State.scriptHostClass && s_State.destroyAllMethod) {
            s_State.env->CallStaticVoidMethod(s_State.scriptHostClass, s_State.destroyAllMethod);
            CheckAndClearException(s_State.env, "ScriptHost.destroyAll");
        }
        if (s_State.scriptHostClass) {
            s_State.env->DeleteGlobalRef(s_State.scriptHostClass);
            s_State.scriptHostClass = nullptr;
        }
    }

    s_State.jvm->DestroyJavaVM();
    s_State.jvm = nullptr;
    s_State.env = nullptr;
    s_State.activeScene = nullptr;
    s_State.window = nullptr;
    KM_CORE_INFO("Embedded JVM shut down cleanly.");
}

bool ScriptEngine::IsInitialized() {
    return s_State.jvm != nullptr && s_State.env != nullptr;
}

void ScriptEngine::SetScriptsDirectory(const std::filesystem::path& scriptsDir) {
    s_State.scriptsDir = scriptsDir;
}

const std::filesystem::path& ScriptEngine::GetScriptsDirectory() {
    if (s_State.scriptsDir.empty()) {
        s_State.scriptsDir = ResolveScriptsSourceDir();
    }
    return s_State.scriptsDir;
}

const std::string& ScriptEngine::GetLastCompileOutput() {
    return s_State.lastCompileOutput;
}

bool ScriptEngine::DidLastCompileSucceed() {
    return s_State.lastCompileSuccess;
}

bool ScriptEngine::CompileAndReloadScripts(Scene* scene, bool isRuntimeActive) {
    if (!IsInitialized()) {
        KM_CORE_WARN("Cannot compile Java scripts: JVM is not initialized.");
        return false;
    }

    if (isRuntimeActive && scene) {
        OnRuntimeStop(scene);
    }

    const std::filesystem::path scriptsDir = ResolveScriptsSourceDir();
    const std::filesystem::path buildDir = ResolveScriptsBuildDir();
    const std::filesystem::path jarPath = ResolveJavaApiJar();

    std::error_code ec;
    if (std::filesystem::exists(buildDir)) {
        std::filesystem::remove_all(buildDir, ec);
    }
    std::filesystem::create_directories(buildDir, ec);

    std::vector<std::filesystem::path> javaFiles;
    if (std::filesystem::exists(scriptsDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".java") {
                javaFiles.push_back(entry.path());
            }
        }
    }

    if (javaFiles.empty()) {
        s_State.lastCompileOutput = "No se encontraron archivos .java en " + scriptsDir.string();
        s_State.lastCompileSuccess = true;
        KM_CORE_INFO("No Java script files found in '{}'.", scriptsDir.string());
        return true;
    }

    std::string cmd = "\"" + ResolveJavacExecutable() + "\" -cp \"" + jarPath.string() +
                      "\" -d \"" + buildDir.string() + "\"";
    for (const auto& file : javaFiles) {
        cmd += " \"" + file.string() + "\"";
    }
    cmd += " 2>&1";

    std::string output;
#ifdef _WIN32
    // Wrap entire command in quotes for cmd.exe /c when paths contain spaces
    const std::string shellCmd = "\"" + cmd + "\"";
    FILE* pipe = _popen(shellCmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        s_State.lastCompileOutput = "No se pudo ejecutar javac.";
        s_State.lastCompileSuccess = false;
        KM_CORE_ERROR("Failed to invoke javac compiler.");
        return false;
    }
    std::array<char, 512> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        output += buf.data();
    }
#ifdef _WIN32
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif

    if (status != 0) {
        s_State.lastCompileOutput = output;
        s_State.lastCompileSuccess = false;
        KM_CORE_ERROR("Java script compilation failed:\n{}", output);
        return false;
    }

    s_State.lastCompileOutput = "Compilacion exitosa (" + std::to_string(javaFiles.size()) + " scripts).";
    s_State.lastCompileSuccess = true;
    KM_CORE_INFO("Compiled {} Java script(s) into '{}'.", javaFiles.size(), buildDir.string());

    jstring jBuildDir = s_State.env->NewStringUTF(buildDir.string().c_str());
    const jboolean reloaded = s_State.env->CallStaticBooleanMethod(
        s_State.scriptHostClass,
        s_State.reloadClassLoaderMethod,
        jBuildDir
    );
    s_State.env->DeleteLocalRef(jBuildDir);
    if (CheckAndClearException(s_State.env, "ScriptHost.reloadClassLoader") || reloaded == JNI_FALSE) {
        KM_CORE_ERROR("Failed to reload Java URLClassLoader.");
        return false;
    }

    KM_CORE_INFO("Java URLClassLoader Hot-Reload completed.");

    if (isRuntimeActive && scene) {
        OnRuntimeStart(scene);
    }

    return true;
}

void ScriptEngine::OnRuntimeStart(Scene* scene) {
    if (!IsInitialized() || !scene) {
        return;
    }

    s_State.activeScene = scene;
    auto& registry = scene->GetRegistry();

    std::vector<entt::entity> scriptEntities;
    for (auto entity : registry.view<JavaScriptComponent>()) {
        scriptEntities.push_back(entity);
    }

    for (entt::entity entity : scriptEntities) {
        if (!scene->IsValid(entity)) {
            continue;
        }
        auto& js = registry.get<JavaScriptComponent>(entity);
        js.instanceCreated = false;
        if (!js.enabled || js.className.empty()) {
            continue;
        }

        const jlong entityId = static_cast<jlong>(entt::to_integral(entity));
        jstring jClassName = s_State.env->NewStringUTF(js.className.c_str());
        const jboolean ok = s_State.env->CallStaticBooleanMethod(
            s_State.scriptHostClass,
            s_State.createInstanceMethod,
            entityId,
            jClassName
        );
        s_State.env->DeleteLocalRef(jClassName);
        if (!CheckAndClearException(s_State.env, "ScriptHost.createInstance") && ok == JNI_TRUE) {
            js.instanceCreated = true;
        }
    }
}

void ScriptEngine::OnRuntimeUpdate(Scene* scene, Timestep ts) {
    if (!IsInitialized() || !scene) {
        return;
    }

    s_State.activeScene = scene;
    auto& registry = scene->GetRegistry();

    std::vector<entt::entity> scriptEntities;
    for (auto entity : registry.view<JavaScriptComponent>()) {
        scriptEntities.push_back(entity);
    }

    const jfloat dt = static_cast<jfloat>(ts.GetSeconds());
    for (entt::entity entity : scriptEntities) {
        if (!scene->IsValid(entity)) {
            continue;
        }
        const auto& js = registry.get<JavaScriptComponent>(entity);
        if (!js.enabled || !js.instanceCreated) {
            continue;
        }
        const jlong entityId = static_cast<jlong>(entt::to_integral(entity));
        s_State.env->CallStaticVoidMethod(s_State.scriptHostClass, s_State.updateInstanceMethod, entityId, dt);
        CheckAndClearException(s_State.env, "ScriptHost.updateInstance");
    }
}

void ScriptEngine::OnRuntimeStop(Scene* scene) {
    if (!IsInitialized()) {
        return;
    }

    s_State.activeScene = scene;
    s_State.env->CallStaticVoidMethod(s_State.scriptHostClass, s_State.destroyAllMethod);
    CheckAndClearException(s_State.env, "ScriptHost.destroyAll");

    if (scene) {
        auto& registry = scene->GetRegistry();
        for (auto entity : registry.view<JavaScriptComponent>()) {
            registry.get<JavaScriptComponent>(entity).instanceCreated = false;
        }
    }

    s_State.activeScene = nullptr;
}

void ScriptEngine::NotifyCollision(Scene* scene, entt::entity self, entt::entity other) {
    if (!IsInitialized() || !scene || !s_State.scriptHostClass || !s_State.dispatchCollisionMethod) {
        return;
    }
    if (!scene->IsValid(self) || !scene->IsValid(other)) {
        return;
    }
    const auto* js = scene->GetRegistry().try_get<JavaScriptComponent>(self);
    if (!js || !js->enabled || !js->instanceCreated) {
        return;
    }

    s_State.activeScene = scene;
    const jlong selfId = static_cast<jlong>(entt::to_integral(self));
    const jlong otherId = static_cast<jlong>(entt::to_integral(other));
    s_State.env->CallStaticVoidMethod(s_State.scriptHostClass, s_State.dispatchCollisionMethod, selfId, otherId);
    CheckAndClearException(s_State.env, "ScriptHost.dispatchCollision");
}

std::vector<std::string> ScriptEngine::GetAvailableScriptNames() {
    std::vector<std::string> names;
    const std::filesystem::path scriptsDir = ResolveScriptsSourceDir();
    std::error_code ec;
    if (!std::filesystem::exists(scriptsDir, ec)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".java") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace Kyumist

