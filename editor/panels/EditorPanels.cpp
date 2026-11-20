#include "panels/EditorPanels.hpp"

#include "audio/AudioEngine.hpp"
#include "core/InputMap.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "physics/PhysicsSystem.hpp"
#include "project/ProjectManager.hpp"
#include "renderer/ModelLoader.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "scene/Components.hpp"
#include "scene/EditorCamera.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptEngine.hpp"
#include "ui/TouchControlsOverlay.hpp"
#include "ui/UISystem.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Kyumist {

namespace {

bool IntersectRayAABB(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    const glm::vec3& boxMin,
    const glm::vec3& boxMax,
    float& outT
) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = rayOrigin[axis];
        const float dir = rayDir[axis];
        if (std::abs(dir) < 1e-6f) {
            if (origin < boxMin[axis] || origin > boxMax[axis]) {
                return false;
            }
        } else {
            const float invD = 1.0f / dir;
            float t1 = (boxMin[axis] - origin) * invD;
            float t2 = (boxMax[axis] - origin) * invD;
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                return false;
            }
        }
    }
    outT = tMin;
    return true;
}

bool IntersectRaySphere(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    float radius,
    float& outT
) {
    const float a = glm::dot(rayDir, rayDir);
    if (a < 1e-8f) {
        return false;
    }
    const float b = 2.0f * glm::dot(rayOrigin, rayDir);
    const float c = glm::dot(rayOrigin, rayOrigin) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return false;
    }
    const float sqrtDisc = std::sqrt(disc);
    float t = (-b - sqrtDisc) / (2.0f * a);
    if (t < 0.0f) {
        t = (-b + sqrtDisc) / (2.0f * a);
    }
    if (t < 0.0f) {
        return false;
    }
    outT = t;
    return true;
}

std::pair<glm::vec3, glm::vec3> GetCustomModelLocalBounds(
    const std::filesystem::path& assetsRoot,
    const std::string& objPath
) {
    static std::unordered_map<std::string, std::pair<glm::vec3, glm::vec3>> s_BoundsCache;
    const std::filesystem::path p(objPath);
    std::filesystem::path resolved = p;
    if (p.is_relative()) {
        std::error_code ec;
        const std::filesystem::path candidate = assetsRoot / p;
        if (std::filesystem::exists(candidate, ec)) {
            resolved = candidate;
        }
    }
    const std::string key = resolved.string();
    auto it = s_BoundsCache.find(key);
    if (it != s_BoundsCache.end()) {
        return it->second;
    }

    std::pair<glm::vec3, glm::vec3> bounds{glm::vec3(-0.65f), glm::vec3(0.65f)};
    LoadedModelData loaded;
    if (ModelLoader::LoadModel(resolved, loaded) && !loaded.vertices.empty()) {
        glm::vec3 bMin(std::numeric_limits<float>::max());
        glm::vec3 bMax(std::numeric_limits<float>::lowest());
        for (const auto& v : loaded.vertices) {
            bMin = glm::min(bMin, v.position);
            bMax = glm::max(bMax, v.position);
        }
        // Asegurar grosor minimo por si un plano/malla es plano en un eje
        for (int axis = 0; axis < 3; ++axis) {
            if ((bMax[axis] - bMin[axis]) < 0.04f) {
                bMin[axis] -= 0.02f;
                bMax[axis] += 0.02f;
            }
        }
        bounds = {bMin, bMax};
    }
    s_BoundsCache.emplace(key, bounds);
    return bounds;
}

bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    auto it = std::search(
        haystack.begin(),
        haystack.end(),
        needle.begin(),
        needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        }
    );
    return it != haystack.end();
}

std::string ToLowerExt(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

bool MatchesImportFilter(const std::string& ext, int filterIndex) {
    const bool isModel = (ext == ".gltf" || ext == ".glb" || ext == ".obj");
    const bool isMat = (ext == ".kmmat" || ext == ".mtl");
    const bool isTex = (ext == ".png" || ext == ".ppm");
    const bool isAudio = (ext == ".wav");
    const bool isScript = (ext == ".java");
    const bool isPrefab = (ext == ".kmprefab");
    switch (filterIndex) {
        case 1: return isModel;
        case 2: return isMat;
        case 3: return isTex;
        case 4: return isAudio;
        case 5: return isScript;
        default: return isModel || isMat || isTex || isAudio || isScript || isPrefab;
    }
}

std::vector<std::string> CollectProjectFilesByExt(
    const std::filesystem::path& rootDir,
    std::initializer_list<std::string_view> exts
) {
    std::vector<std::string> result;
    std::error_code ec;
    if (rootDir.empty() || !std::filesystem::exists(rootDir, ec)) {
        return result;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string ext = ToLowerExt(entry.path());
        bool match = false;
        for (std::string_view e : exts) {
            if (ext == e) {
                match = true;
                break;
            }
        }
        if (!match) {
            continue;
        }
        const std::filesystem::path rel = std::filesystem::relative(entry.path(), rootDir, ec);
        result.push_back(ec ? entry.path().string() : rel.string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

ImVec4 ColorForLogLevel(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace:
            return ImVec4(0.58f, 0.63f, 0.72f, 1.0f);
        case spdlog::level::debug:
            return ImVec4(0.45f, 0.78f, 0.96f, 1.0f);
        case spdlog::level::info:
            return ImVec4(0.32f, 0.85f, 0.56f, 1.0f);
        case spdlog::level::warn:
            return ImVec4(0.98f, 0.76f, 0.24f, 1.0f);
        case spdlog::level::err:
        case spdlog::level::critical:
            return ImVec4(0.96f, 0.36f, 0.38f, 1.0f);
        default:
            return ImVec4(0.90f, 0.92f, 0.95f, 1.0f);
    }
}

const char* LabelForLogLevel(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace:    return "TRACE";
        case spdlog::level::debug:    return "DEBUG";
        case spdlog::level::info:     return "INFO";
        case spdlog::level::warn:     return "WARN";
        case spdlog::level::err:      return "ERROR";
        case spdlog::level::critical: return "CRIT";
        default:                      return "LOG";
    }
}

std::filesystem::path GetUserHomeDir() {
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') {
            return std::filesystem::path(home);
        }
    }
#ifdef _WIN32
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        if (*userProfile != '\0') {
            return std::filesystem::path(userProfile);
        }
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path ResolvePlaySnapshotPath() {
    std::error_code ec;
    if (std::filesystem::exists("build", ec)) {
        return std::filesystem::absolute("build/temp_play_snapshot.kmscene", ec);
    }
    return std::filesystem::temp_directory_path() / "kyumist_temp_play_snapshot.kmscene";
}

#ifdef _WIN32
std::string QuoteWinCmdArg(const std::string& arg) {
    std::string quoted = "\"";
    for (char c : arg) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted += c;
        }
    }
    quoted += "\"";
    return quoted;
}
#endif

std::filesystem::path ResolveRuntimeExecutablePath() {
    std::error_code ec;
#ifdef _WIN32
    char exeBuf[4096]{};
    const DWORD len = ::GetModuleFileNameA(nullptr, exeBuf, static_cast<DWORD>(sizeof(exeBuf) - 1));
    if (len > 0) {
        exeBuf[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
        const std::filesystem::path candidates[] = {
            exeDir / "KyumistRuntime.exe",
            exeDir / "../runtime/KyumistRuntime.exe",
            exeDir / "../runtime/Release/KyumistRuntime.exe",
            exeDir / "../runtime/Debug/KyumistRuntime.exe",
            exeDir / "../../runtime/Release/KyumistRuntime.exe",
            exeDir / "../../runtime/Debug/KyumistRuntime.exe"
        };
        for (const auto& c : candidates) {
            const std::filesystem::path norm = std::filesystem::weakly_canonical(c, ec);
            if (!ec && std::filesystem::exists(norm, ec)) {
                return norm;
            }
        }
    }

    const std::filesystem::path fallbackCandidates[] = {
        std::filesystem::path("build/runtime/KyumistRuntime.exe"),
        std::filesystem::path("build/runtime/Release/KyumistRuntime.exe"),
        std::filesystem::path("./KyumistRuntime.exe")
    };
    for (const auto& c : fallbackCandidates) {
        if (std::filesystem::exists(c, ec)) {
            return std::filesystem::absolute(c, ec);
        }
    }
    return {};
#else
    char exeBuf[4096]{};
    const ssize_t len = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (len > 0) {
        exeBuf[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
        const std::filesystem::path candidates[] = {
            exeDir / "../runtime/KyumistRuntime",
            exeDir / "KyumistRuntime"
        };
        for (const auto& c : candidates) {
            const std::filesystem::path norm = std::filesystem::weakly_canonical(c, ec);
            if (!ec && std::filesystem::exists(norm, ec)) {
                return norm;
            }
        }
    }

    const std::filesystem::path fallbackCandidates[] = {
        std::filesystem::path("build/runtime/KyumistRuntime"),
        std::filesystem::path("./KyumistRuntime")
    };
    for (const auto& c : fallbackCandidates) {
        if (std::filesystem::exists(c, ec)) {
            return std::filesystem::absolute(c, ec);
        }
    }
    return {};
#endif
}

void DrawVec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float speed = 0.05f) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);

    const float lineHeight = ImGui::GetFrameHeight();
    const ImVec2 buttonSize(lineHeight, lineHeight);
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float dragWidth = std::max(34.0f, (availWidth - buttonSize.x * 3.0f - 18.0f) / 3.0f);

    struct AxisDef {
        const char* buttonText;
        const char* dragId;
        ImVec4 normal;
        ImVec4 hovered;
        ImVec4 active;
        int index;
    };

    const AxisDef axes[3] = {
        {"X", "##X", ImVec4(0.82f, 0.18f, 0.24f, 1.0f), ImVec4(0.92f, 0.28f, 0.34f, 1.0f), ImVec4(0.70f, 0.12f, 0.18f, 1.0f), 0},
        {"Y", "##Y", ImVec4(0.16f, 0.65f, 0.28f, 1.0f), ImVec4(0.24f, 0.76f, 0.36f, 1.0f), ImVec4(0.12f, 0.52f, 0.20f, 1.0f), 1},
        {"Z", "##Z", ImVec4(0.18f, 0.44f, 0.86f, 1.0f), ImVec4(0.28f, 0.54f, 0.96f, 1.0f), ImVec4(0.14f, 0.34f, 0.72f, 1.0f), 2}
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 4.0f));
    for (int i = 0; i < 3; ++i) {
        const auto& axis = axes[i];
        ImGui::PushStyleColor(ImGuiCol_Button, axis.normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, axis.hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, axis.active);
        if (ImGui::Button(axis.buttonText, buttonSize)) {
            values[axis.index] = resetValue;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(dragWidth);
        ImGui::DragFloat(axis.dragId, &values[axis.index], speed, 0.0f, 0.0f, "%.2f");
        if (i < 2) {
            ImGui::SameLine(0.0f, 5.0f);
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}

bool DrawComponentHeader(const char* title, const char* id, bool* outRemove) {
    const bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (outRemove) {
        ImGui::SameLine(std::max(10.0f, ImGui::GetWindowContentRegionMax().x - 24.0f));
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.16f, 0.18f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.24f, 0.26f, 1.00f));
        if (ImGui::Button("X", ImVec2(22.0f, 0.0f))) {
            *outRemove = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    return open;
}

bool BeginInspectorTable(const char* tableId) {
    if (!ImGui::BeginTable(tableId, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        return false;
    }
    ImGui::TableSetupColumn("Propiedad", ImGuiTableColumnFlags_WidthFixed, 105.0f);
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void BeginInspectorRow(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
}

bool DrawFilterToggle(const char* label, bool& active, const ImVec4& accent) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.45f, accent.y * 0.45f, accent.z * 0.45f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 0.65f, accent.y * 0.65f, accent.z * 0.65f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.11f, 0.12f, 0.16f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.18f, 0.24f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.52f, 0.60f, 1.00f));
    }
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    if (clicked) {
        active = !active;
    }
    return clicked;
}

} // namespace

EditorPanels::EditorPanels() {
    m_AssetsRoot = ProjectManager::GetActiveProjectDir();
    m_CurrentAssetDir = m_AssetsRoot;
    m_ActiveSceneFile = ProjectManager::GetStartScenePath();

    const std::string initialParent = (GetUserHomeDir() / "Proyectos_Kyumist").string();
    std::strncpy(m_NewProjectParentDir, initialParent.c_str(), sizeof(m_NewProjectParentDir) - 1);
    m_BrowserCurrentDir = std::filesystem::path(m_NewProjectParentDir).parent_path();
    if (m_BrowserCurrentDir.empty() || !std::filesystem::exists(m_BrowserCurrentDir)) {
        m_BrowserCurrentDir = std::filesystem::current_path();
    }
}

void EditorPanels::SyncProjectState(Scene& scene) {
    if (m_IsPlaying) {
        StopPlayMode(scene);
    }
    m_SelectedEntity = entt::null;
    m_AssetsRoot = ProjectManager::GetActiveProjectDir();
    m_CurrentAssetDir = m_AssetsRoot;
    m_ActiveSceneFile = ProjectManager::GetStartScenePath();

    if (std::filesystem::exists(m_ActiveSceneFile)) {
        if (!SceneSerializer::LoadFromFile(scene, m_ActiveSceneFile)) {
            scene.CreateDefaultScene();
        }
    } else {
        scene.CreateDefaultScene();
        SceneSerializer::SaveToFile(scene, m_ActiveSceneFile);
    }

    m_ScriptEditor.CloseAll();
    m_ScriptEditor.OpenFirstAvailableScript();
    ResetSceneHistory(scene);
    m_FocusViewportNext = true;
}

void EditorPanels::SpawnModelEntity(Scene& scene, const std::filesystem::path& modelFile) {
    std::error_code ec;
    const std::filesystem::path relModel = std::filesystem::relative(modelFile, m_AssetsRoot, ec);
    const std::string relStr = ec ? modelFile.string() : relModel.string();

    entt::entity e = scene.CreateEntity(modelFile.stem().string());
    auto& mesh = scene.GetRegistry().emplace<MeshRendererComponent>(e);
    mesh.type = MeshType::CustomOBJ;
    mesh.objPath = relStr;

    LoadedModelData loaded;
    if (ModelLoader::LoadModel(modelFile, loaded) && loaded.hasMaterial) {
        mesh.color = loaded.primaryMaterial.baseColor;
        mesh.metallic = loaded.primaryMaterial.metallic;
        mesh.roughness = loaded.primaryMaterial.roughness;
        mesh.emissive = loaded.primaryMaterial.emissive;
    } else {
        mesh.color = {0.92f, 0.78f, 0.34f, 1.0f};
    }

    m_SelectedEntity = e;
    KM_INFO("Modelo 3D instanciado desde 'res://{}'.", mesh.objPath);
}

void EditorPanels::ApplyMaterialToEntity(Scene& scene, entt::entity entity, const std::filesystem::path& materialFile) {
    if (!scene.IsValid(entity)) {
        KM_WARN("Selecciona primero una entidad con MeshRendererComponent para aplicar el material.");
        return;
    }
    auto& reg = scene.GetRegistry();
    auto* mesh = reg.try_get<MeshRendererComponent>(entity);
    if (!mesh) {
        KM_WARN("La entidad seleccionada no tiene MeshRendererComponent.");
        return;
    }

    PBRMaterialData mat;
    if (!ModelLoader::LoadMaterialFile(materialFile, mat)) {
        KM_ERROR("No se pudo cargar el material '{}'.", materialFile.string());
        return;
    }

    std::error_code ec;
    const std::filesystem::path relMat = std::filesystem::relative(materialFile, m_AssetsRoot, ec);
    mesh->materialPath = ec ? materialFile.string() : relMat.string();
    mesh->color = mat.baseColor;
    mesh->metallic = mat.metallic;
    mesh->roughness = mat.roughness;
    mesh->emissive = mat.emissive;

    KM_INFO("Material '{}' ('res://{}') aplicado.", mat.name, mesh->materialPath);
}

void EditorPanels::StartPlayMode(Scene& scene) {
    if (m_IsPlaying) {
        return;
    }

    const std::filesystem::path snapPath = ResolvePlaySnapshotPath();
    SceneSerializer::SaveToFile(scene, snapPath);

    const std::filesystem::path runtimeExe = m_HeadlessMode ? std::filesystem::path{} : ResolveRuntimeExecutablePath();
    std::error_code ec;
    if (!m_HeadlessMode && !runtimeExe.empty() && std::filesystem::exists(runtimeExe, ec)) {
        const std::string projDirStr = ProjectManager::GetActiveProjectDir().string();
        const std::string snapStr = snapPath.string();
        const std::string titleStr = (ProjectManager::HasActiveProject()
            ? ProjectManager::GetActiveProject().name
            : std::string("Kyumist Game")) + " — Ventana de Juego (Sin Edicion)";

#ifdef _WIN32
        std::string cmdLine = QuoteWinCmdArg(runtimeExe.string()) +
                              " --project " + QuoteWinCmdArg(projDirStr) +
                              " --scene " + QuoteWinCmdArg(snapStr) +
                              " --title " + QuoteWinCmdArg(titleStr);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (::CreateProcessA(
                runtimeExe.string().c_str(),
                cmdLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                nullptr,
                &si,
                &pi)) {
            ::CloseHandle(pi.hThread);
            m_RuntimeProcessHandle = reinterpret_cast<std::uintptr_t>(pi.hProcess);
            m_RuntimePid = static_cast<int>(pi.dwProcessId);
            m_IsPlaying = true;
            KM_INFO("Ventana de juego independiente abierta (PID {}): '{}' [sin edicion].", m_RuntimePid, titleStr);
            return;
        }
#else
        const pid_t pid = ::fork();
        if (pid == 0) {
            ::execl(
                runtimeExe.c_str(),
                runtimeExe.c_str(),
                "--project", projDirStr.c_str(),
                "--scene", snapStr.c_str(),
                "--title", titleStr.c_str(),
                static_cast<char*>(nullptr)
            );
            ::_exit(127);
        } else if (pid > 0) {
            m_RuntimePid = static_cast<int>(pid);
            m_IsPlaying = true;
            KM_INFO("Ventana de juego independiente abierta (PID {}): '{}' [sin edicion].", m_RuntimePid, titleStr);
            return;
        }
#endif
        KM_WARN("No se pudo lanzar la ventana externa de KyumistRuntime; usando modo en editor.");
    }

    m_RuntimePid = -1;
    m_RuntimeProcessHandle = 0;
    ScriptEngine::OnRuntimeStart(&scene);
    PhysicsSystem::OnRuntimeStart(&scene);
    m_IsPlaying = true;
    KM_INFO("Modo Play iniciado (scripts Java + fisicas + audio activos).");
}

void EditorPanels::StopPlayMode(Scene& scene) {
    if (!m_IsPlaying) {
        return;
    }

#ifdef _WIN32
    if (m_RuntimeProcessHandle != 0) {
        HANDLE proc = reinterpret_cast<HANDLE>(m_RuntimeProcessHandle);
        if (::WaitForSingleObject(proc, 0) == WAIT_TIMEOUT) {
            ::TerminateProcess(proc, 0);
            ::WaitForSingleObject(proc, 500);
        }
        ::CloseHandle(proc);
        m_RuntimeProcessHandle = 0;
        m_RuntimePid = -1;
        m_IsPlaying = false;
        KM_INFO("Ventana de juego externa cerrada (modo edicion restaurado).");
        return;
    }
#else
    if (m_RuntimePid > 0) {
        const pid_t pid = static_cast<pid_t>(m_RuntimePid);
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) == 0) {
            ::kill(pid, SIGTERM);
            for (int attempt = 0; attempt < 10; ++attempt) {
                if (::waitpid(pid, &status, WNOHANG) != 0) {
                    break;
                }
                ::usleep(20000);
            }
            if (::waitpid(pid, &status, WNOHANG) == 0) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &status, 0);
            }
        }
        m_RuntimePid = -1;
        m_IsPlaying = false;
        KM_INFO("Ventana de juego externa cerrada (modo edicion restaurado).");
        return;
    }
#endif

    ScriptEngine::OnRuntimeStop(&scene);
    UISystem::UnloadAllDocuments();
    InputMap::ClearVirtualStates();
    m_IsPlaying = false;
    SceneSerializer::LoadFromFile(scene, ResolvePlaySnapshotPath());
    m_SelectedEntity = entt::null;
    ResetSceneHistory(scene);
    KM_INFO("Modo Play detenido (escena restaurada).");
}

std::string EditorPanels::GetSelectedEntityTag(const Scene& scene) const {
    if (scene.IsValid(m_SelectedEntity) && scene.GetRegistry().all_of<TagComponent>(m_SelectedEntity)) {
        return scene.GetRegistry().get<TagComponent>(m_SelectedEntity).tag;
    }
    return {};
}

void EditorPanels::ResetSceneHistory(const Scene& scene) {
    m_SceneUndoStack.clear();
    m_SceneRedoStack.clear();
    m_LastCommittedSceneState = SceneSerializer::SerializeToString(scene);
    m_LastCommittedSelectedTag = GetSelectedEntityTag(scene);
}

void EditorPanels::CaptureSceneUndoStateIfChanged(const Scene& scene) {
    if (m_IsPlaying || ImGui::IsAnyItemActive() || ImGuizmo::IsUsing()) {
        return;
    }

    const std::string currentJson = SceneSerializer::SerializeToString(scene);
    if (m_LastCommittedSceneState.empty()) {
        m_LastCommittedSceneState = currentJson;
        m_LastCommittedSelectedTag = GetSelectedEntityTag(scene);
        return;
    }

    if (currentJson != m_LastCommittedSceneState) {
        m_SceneUndoStack.push_back(SceneHistoryEntry{m_LastCommittedSceneState, m_LastCommittedSelectedTag});
        if (m_SceneUndoStack.size() > 64) {
            m_SceneUndoStack.erase(m_SceneUndoStack.begin());
        }
        m_SceneRedoStack.clear();
        m_LastCommittedSceneState = currentJson;
    }

    m_LastCommittedSelectedTag = GetSelectedEntityTag(scene);
}

bool EditorPanels::UndoScene(Scene& scene) {
    if (m_IsPlaying || m_SceneUndoStack.empty()) {
        return false;
    }

    SceneHistoryEntry redoEntry;
    redoEntry.sceneJson = SceneSerializer::SerializeToString(scene);
    redoEntry.selectedTag = GetSelectedEntityTag(scene);

    SceneHistoryEntry target = std::move(m_SceneUndoStack.back());
    m_SceneUndoStack.pop_back();

    if (!SceneSerializer::DeserializeFromString(scene, target.sceneJson)) {
        m_SceneUndoStack.push_back(std::move(target));
        return false;
    }

    m_SceneRedoStack.push_back(std::move(redoEntry));
    m_SelectedEntity = target.selectedTag.empty()
        ? entt::null
        : scene.FindEntityByName(target.selectedTag);
    m_LastCommittedSceneState = SceneSerializer::SerializeToString(scene);
    m_LastCommittedSelectedTag = GetSelectedEntityTag(scene);
    KM_INFO("Escena: Deshacer aplicado.");
    return true;
}

bool EditorPanels::RedoScene(Scene& scene) {
    if (m_IsPlaying || m_SceneRedoStack.empty()) {
        return false;
    }

    SceneHistoryEntry undoEntry;
    undoEntry.sceneJson = SceneSerializer::SerializeToString(scene);
    undoEntry.selectedTag = GetSelectedEntityTag(scene);

    SceneHistoryEntry target = std::move(m_SceneRedoStack.back());
    m_SceneRedoStack.pop_back();

    if (!SceneSerializer::DeserializeFromString(scene, target.sceneJson)) {
        m_SceneRedoStack.push_back(std::move(target));
        return false;
    }

    m_SceneUndoStack.push_back(std::move(undoEntry));
    m_SelectedEntity = target.selectedTag.empty()
        ? entt::null
        : scene.FindEntityByName(target.selectedTag);
    m_LastCommittedSceneState = SceneSerializer::SerializeToString(scene);
    m_LastCommittedSelectedTag = GetSelectedEntityTag(scene);
    KM_INFO("Escena: Rehacer aplicado.");
    return true;
}

entt::entity EditorPanels::PickEntityInViewport(
    const Scene& scene,
    const EditorCamera& camera,
    const ImVec2& mousePos,
    const ImVec2& imageMin,
    const ImVec2& imageSize
) const {
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f) {
        return entt::null;
    }

    const float nx = ((mousePos.x - imageMin.x) / imageSize.x) * 2.0f - 1.0f;
    const float ny = 1.0f - ((mousePos.y - imageMin.y) / imageSize.y) * 2.0f;
    const float aspect = imageSize.x / imageSize.y;

    glm::mat4 viewProj = camera.GetViewProjection(aspect);
    if (m_IsPlaying) {
        glm::mat4 gameCamVP(1.0f);
        if (scene.ComputePrimaryCameraViewProjection(aspect, gameCamVP)) {
            viewProj = gameCamVP;
        }
    }

    const glm::mat4 invVP = glm::inverse(viewProj);
    glm::vec4 nearWorld4 = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 farWorld4 = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
    if (std::abs(nearWorld4.w) < 1e-6f || std::abs(farWorld4.w) < 1e-6f) {
        return entt::null;
    }
    nearWorld4 /= nearWorld4.w;
    farWorld4 /= farWorld4.w;

    const glm::vec3 rayOrigin = glm::vec3(nearWorld4);
    const glm::vec3 rayDelta = glm::vec3(farWorld4 - nearWorld4);
    if (glm::dot(rayDelta, rayDelta) < 1e-10f) {
        return entt::null;
    }
    const glm::vec3 rayDir = glm::normalize(rayDelta);

    entt::entity bestEntity = entt::null;
    float bestT = std::numeric_limits<float>::max();
    entt::entity bestPlaneEntity = entt::null;
    float bestPlaneT = std::numeric_limits<float>::max();

    const auto& reg = scene.GetRegistry();
    for (auto entity : reg.view<TransformComponent>()) {
        const auto* mesh = reg.try_get<MeshRendererComponent>(entity);
        const auto* sprite = reg.try_get<SpriteRendererComponent>(entity);
        if ((!mesh || !mesh->visible) && !sprite) {
            continue;
        }

        const glm::mat4 worldM = scene.GetWorldTransform(entity);
        if (std::abs(glm::determinant(worldM)) < 1e-8f) {
            continue;
        }
        const glm::mat4 invM = glm::inverse(worldM);
        const glm::vec3 localOrigin = glm::vec3(invM * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 localDir = glm::vec3(invM * glm::vec4(rayDir, 0.0f));

        float t = 0.0f;
        bool hit = false;
        bool isPlane = false;

        if (mesh && mesh->visible) {
            if (mesh->type == MeshType::Sphere) {
                hit = IntersectRaySphere(localOrigin, localDir, 0.5f, t);
            } else if (mesh->type == MeshType::Plane) {
                isPlane = true;
                hit = IntersectRayAABB(
                    localOrigin,
                    localDir,
                    glm::vec3(-0.5f, -0.02f, -0.5f),
                    glm::vec3(0.5f, 0.02f, 0.5f),
                    t
                );
            } else if (mesh->type == MeshType::CustomOBJ) {
                const auto bounds = GetCustomModelLocalBounds(m_AssetsRoot, mesh->objPath);
                hit = IntersectRayAABB(localOrigin, localDir, bounds.first, bounds.second, t);
            } else {
                hit = IntersectRayAABB(localOrigin, localDir, glm::vec3(-0.5f), glm::vec3(0.5f), t);
            }
        } else if (sprite) {
            hit = IntersectRayAABB(
                localOrigin,
                localDir,
                glm::vec3(-0.5f, -0.5f, -0.05f),
                glm::vec3(0.5f, 0.5f, 0.05f),
                t
            );
        }

        if (!hit) {
            continue;
        }

        if (isPlane) {
            if (t < bestPlaneT) {
                bestPlaneT = t;
                bestPlaneEntity = entity;
            }
        } else if (t < bestT) {
            bestT = t;
            bestEntity = entity;
        }
    }

    if (bestEntity != entt::null) {
        if (bestPlaneEntity != entt::null && (bestPlaneT + 0.25f) < bestT) {
            return bestPlaneEntity;
        }
        return bestEntity;
    }
    return bestPlaneEntity;
}

entt::entity EditorPanels::DuplicateEntity(Scene& scene, entt::entity src) {
    if (!scene.IsValid(src)) {
        return entt::null;
    }
    auto& reg = scene.GetRegistry();
    const std::string baseName = reg.all_of<TagComponent>(src) ? reg.get<TagComponent>(src).tag : "Entity";
    entt::entity dst = scene.CreateEntity(baseName + " (Copia)");

    if (const auto* tr = reg.try_get<TransformComponent>(src)) {
        auto& dstTr = reg.get<TransformComponent>(dst);
        dstTr = *tr;
        dstTr.translation.x += 0.5f;
    }
    if (const auto* mesh = reg.try_get<MeshRendererComponent>(src)) {
        reg.emplace<MeshRendererComponent>(dst, *mesh);
    }
    if (const auto* spr = reg.try_get<SpriteRendererComponent>(src)) {
        reg.emplace<SpriteRendererComponent>(dst, *spr);
    }
    if (const auto* light = reg.try_get<DirectionalLightComponent>(src)) {
        reg.emplace<DirectionalLightComponent>(dst, *light);
    }
    if (const auto* cam = reg.try_get<CameraComponent>(src)) {
        reg.emplace<CameraComponent>(dst, *cam);
    }
    if (const auto* js = reg.try_get<JavaScriptComponent>(src)) {
        auto copyJs = *js;
        copyJs.instanceCreated = false;
        reg.emplace<JavaScriptComponent>(dst, copyJs);
    }
    if (const auto* rb = reg.try_get<RigidBodyComponent>(src)) {
        reg.emplace<RigidBodyComponent>(dst, *rb);
    }
    if (const auto* audio = reg.try_get<AudioSourceComponent>(src)) {
        reg.emplace<AudioSourceComponent>(dst, *audio);
    }
    return dst;
}

void EditorPanels::Render(
    VulkanRenderer* renderer,
    Scene& scene,
    EditorCamera& camera,
    Timestep ts,
    uint32_t frameCount,
    bool& shouldClose
) {
    if (!ProjectManager::HasActiveProject()) {
        m_ViewportHovered = false;
        RenderProjectManager(scene, shouldClose);
        return;
    }

#ifdef _WIN32
    if (m_IsPlaying && m_RuntimeProcessHandle != 0) {
        HANDLE proc = reinterpret_cast<HANDLE>(m_RuntimeProcessHandle);
        if (::WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
            ::CloseHandle(proc);
            m_RuntimeProcessHandle = 0;
            m_RuntimePid = -1;
            m_IsPlaying = false;
            KM_INFO("Ventana de juego cerrada por el usuario.");
        }
    }
#else
    if (m_IsPlaying && m_RuntimePid > 0) {
        int status = 0;
        const pid_t res = ::waitpid(static_cast<pid_t>(m_RuntimePid), &status, WNOHANG);
        if (res == static_cast<pid_t>(m_RuntimePid) || (res == -1 && errno == ECHILD)) {
            m_RuntimePid = -1;
            m_IsPlaying = false;
            KM_INFO("Ventana de juego cerrada por el usuario.");
        }
    }
#endif

    RenderMenuBar(scene, shouldClose);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::GetID("KyumistDockspace");

    if (!m_LayoutInitialized) {
        m_LayoutInitialized = true;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        const ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);
        const ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.21f, nullptr, &dockMain);
        const ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.27f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Script Editor", dockMain);
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Stats & Renderer", dockRight);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("FileSystem (res://)", dockBottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpaceOverViewport(dockspaceId, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    if (m_ShowHierarchy) {
        RenderHierarchy(scene);
    }
    if (m_ShowViewport) {
        if (m_FocusViewportNext) {
            ImGui::SetNextWindowFocus();
            m_FocusViewportNext = false;
        }
        RenderViewport(renderer, scene, camera, ts);
    }
    if (m_ShowScriptEditor) {
        if (m_FocusScriptEditorNext) {
            ImGui::SetNextWindowFocus();
            m_FocusScriptEditorNext = false;
        }
        m_ScriptEditor.Render(scene, m_IsPlaying, &m_ShowScriptEditor);
    }
    if (m_ShowInspector) {
        RenderInspector(scene);
    }
    if (m_ShowStats) {
        RenderStats(renderer, ts, frameCount);
    }
    if (m_ShowConsole) {
        RenderConsole();
    }
    if (m_ShowContentBrowser) {
        RenderContentBrowser(scene);
    }

    RenderAssetImportModal(renderer, scene);
    RenderExportProjectModal();
    RenderInputMapModal();

    const ImGuiIO& io = ImGui::GetIO();
    if (!m_IsPlaying &&
        !m_ScriptEditor.IsFocused() &&
        !io.WantTextInput &&
        !ImGui::IsAnyItemActive() &&
        !ImGuizmo::IsUsing() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        const bool ctrlOnly = io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
        if (ctrlOnly && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            UndoScene(scene);
        } else if (ctrlOnly && ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
                                (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false)))) {
            RedoScene(scene);
        }
    }

    CaptureSceneUndoStateIfChanged(scene);
}

void EditorPanels::RenderProjectManager(Scene& scene, bool& shouldClose) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("KyumistProjectManager", nullptr, flags)) {
        ImGui::End();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImFont* headerFont = (io.Fonts->Fonts.Size > 2) ? io.Fonts->Fonts[2] : nullptr;

    // Header bar
    if (headerFont) ImGui::PushFont(headerFont);
    ImGui::TextColored(ImVec4(0.36f, 0.62f, 0.99f, 1.0f), "KYMENGINE");
    if (headerFont) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("— Gestor de Proyectos (C++20 + Vulkan 1.3 + Java)");

    const float rightBtnsW = 420.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f, ImGui::GetWindowWidth() - rightBtnsW));
    ImGui::SetNextItemWidth(175.0f);
    ImGui::InputTextWithHint("##ProjectSearch", "Buscar proyecto...", m_ProjectSearch, sizeof(m_ProjectSearch));
    ImGui::SameLine();
    if (ImGui::Button("Importar / Abrir...")) {
        m_FolderPickerMode = FolderPickerMode::OpenExistingProject;
        if (!std::filesystem::exists(m_BrowserCurrentDir)) {
            m_BrowserCurrentDir = std::filesystem::current_path();
        }
        m_OpenFolderPickerPopup = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Salir")) {
        shouldClose = true;
    }

    ImGui::Separator();
    ImGui::Spacing();

    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float leftW = std::max(360.0f, availW * 0.58f);
    const std::string_view search(m_ProjectSearch);

    // Left column: Recent projects (top) + Sample Projects section (bottom)
    if (ImGui::BeginChild("LeftProjectColumn", ImVec2(leftW, availH), false)) {
        const float recentsH = std::max(175.0f, availH * 0.40f);
        if (ImGui::BeginChild("RecentProjectsPane", ImVec2(0.0f, recentsH), true)) {
            if (headerFont) ImGui::PushFont(headerFont);
            ImGui::Text("Proyectos Recientes");
            if (headerFont) ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            const std::vector<RecentProjectEntry> recents = ProjectManager::GetRecentProjects();
            bool anyShown = false;

            for (size_t i = 0; i < recents.size(); ++i) {
                const auto& entry = recents[i];
                if (!ContainsCaseInsensitive(entry.name, search) &&
                    !ContainsCaseInsensitive(entry.projectFilePath, search)) {
                    continue;
                }
                anyShown = true;

                std::error_code ec;
                const bool existsOnDisk = std::filesystem::exists(entry.projectFilePath, ec);

                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.114f, 0.129f, 0.173f, 1.0f));
                if (ImGui::BeginChild("ProjectCard", ImVec2(0.0f, 70.0f), true)) {
                    ImGui::TextColored(ImVec4(0.92f, 0.95f, 0.99f, 1.0f), "%s", entry.name.c_str());
                    ImGui::SameLine();
                    if (existsOnDisk) {
                        ImGui::TextColored(ImVec4(0.22f, 0.88f, 0.48f, 1.0f), "[LISTO]");
                    } else {
                        ImGui::TextColored(ImVec4(0.96f, 0.38f, 0.38f, 1.0f), "[NO ENCONTRADO]");
                    }

                    ImGui::TextDisabled("%s", entry.projectFilePath.c_str());

                    ImGui::SetCursorPos(ImVec2(std::max(10.0f, ImGui::GetWindowWidth() - 190.0f), 19.0f));
                    ImGui::BeginDisabled(!existsOnDisk);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.46f, 0.88f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.56f, 0.98f, 1.0f));
                    if (ImGui::Button("Abrir", ImVec2(90.0f, 30.0f))) {
                        if (ProjectManager::OpenProject(entry.projectFilePath)) {
                            SyncProjectState(scene);
                        }
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Quitar", ImVec2(75.0f, 30.0f))) {
                        ProjectManager::RemoveRecentProject(entry.projectFilePath);
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopID();
                ImGui::Spacing();
            }

            if (!anyShown) {
                ImGui::TextDisabled(
                    recents.empty()
                        ? "Todavia no tienes proyectos recientes propios. Crea uno a la derecha o abre un ejemplo abajo."
                        : "Ningun proyecto reciente coincide con la busqueda."
                );
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        if (ImGui::BeginChild("SampleProjectsPane", ImVec2(0.0f, 0.0f), true)) {
            if (headerFont) ImGui::PushFont(headerFont);
            ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.99f, 1.0f), "Ejemplos de Proyectos");
            if (headerFont) ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("— Listos para explorar y ejecutar");
            ImGui::Separator();
            ImGui::Spacing();

            const std::vector<SampleProjectEntry> samples = ProjectManager::GetSampleProjects();
            bool anySample = false;

            for (size_t i = 0; i < samples.size(); ++i) {
                const auto& sample = samples[i];
                if (!ContainsCaseInsensitive(sample.name, search) &&
                    !ContainsCaseInsensitive(sample.categoryBadge, search) &&
                    !ContainsCaseInsensitive(sample.description, search)) {
                    continue;
                }
                anySample = true;

                ImVec4 badgeCol(0.38f, 0.68f, 0.99f, 1.0f);
                if (sample.templateIndex == 2) {
                    badgeCol = ImVec4(0.20f, 0.88f, 0.56f, 1.0f);
                } else if (sample.templateIndex == 1) {
                    badgeCol = ImVec4(0.96f, 0.54f, 0.24f, 1.0f);
                }

                ImGui::PushID(static_cast<int>(1000 + i));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f, 0.125f, 0.168f, 1.0f));
                if (ImGui::BeginChild("SampleCard", ImVec2(0.0f, 84.0f), true)) {
                    const float btnAreaW = 215.0f;
                    const float textMaxW = std::max(180.0f, ImGui::GetWindowWidth() - btnAreaW - 22.0f);

                    ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "%s", sample.name.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(badgeCol, "[%s]", sample.categoryBadge.c_str());

                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textMaxW);
                    ImGui::TextDisabled("%s", sample.description.c_str());
                    ImGui::PopTextWrapPos();

                    ImGui::SetCursorPos(ImVec2(std::max(10.0f, ImGui::GetWindowWidth() - btnAreaW), 26.0f));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.56f, 0.34f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.42f, 1.0f));
                    if (ImGui::Button("Abrir Ejemplo", ImVec2(115.0f, 32.0f))) {
                        if (ProjectManager::OpenProject(sample.projectFilePath)) {
                            SyncProjectState(scene);
                        }
                    }
                    ImGui::PopStyleColor(2);

                    ImGui::SameLine();
                    if (ImGui::Button("Restaurar", ImVec2(82.0f, 32.0f))) {
                        m_ProjectError.clear();
                        ProjectManager::ResetSampleProject(static_cast<int>(i), &m_ProjectError);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Regenera la escena y scripts originales de este ejemplo");
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopID();
                ImGui::Spacing();
            }

            if (!anySample) {
                ImGui::TextDisabled("Ningun ejemplo coincide con la busqueda.");
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right column: Create new project
    if (ImGui::BeginChild("CreateProjectPane", ImVec2(0.0f, availH), true)) {
        if (headerFont) ImGui::PushFont(headerFont);
        ImGui::Text("Crear Nuevo Proyecto");
        if (headerFont) ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Nombre del Proyecto:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##NewProjName", m_NewProjectName, sizeof(m_NewProjectName));
        ImGui::Spacing();

        ImGui::Text("Directorio Padre:");
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 135.0f));
        ImGui::InputText("##NewProjParent", m_NewProjectParentDir, sizeof(m_NewProjectParentDir));
        ImGui::SameLine();
        if (ImGui::Button("Explorar...", ImVec2(125.0f, 0.0f))) {
            m_FolderPickerMode = FolderPickerMode::CreateParentDir;
            std::filesystem::path startDir(m_NewProjectParentDir);
            if (!std::filesystem::exists(startDir)) {
                startDir = startDir.parent_path();
            }
            if (startDir.empty() || !std::filesystem::exists(startDir)) {
                startDir = std::filesystem::current_path();
            }
            m_BrowserCurrentDir = startDir;
            m_OpenFolderPickerPopup = true;
        }

        const std::filesystem::path previewPath =
            std::filesystem::path(m_NewProjectParentDir) / m_NewProjectName / "project.kmproj";
        ImGui::TextWrapped("Se creara en: %s", previewPath.string().c_str());
        ImGui::Spacing();

        ImGui::Text("Plantilla Inicial:");
        const char* templates[] = {
            "Hibrido 3D + 2D (Aether Guardian — Santuario HD-2D)",
            "Shooter 3D (Vortex Arena — Raycast y Drones)",
            "Plataformas 2D (SkyHop — Fisicas Box2D y Monedas)",
            "En blanco 3D (Camara + Luz)",
            "En blanco 2D (Camara Ortografica)",
            "En blanco Mixto 3D + 2D (Camara + Luz)"
        };
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##ProjTemplate", &m_NewProjectTemplate, templates, 6);
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::TextDisabled("Archivos generados automaticamente:");
        ImGui::BulletText("project.kmproj (Configuracion del proyecto)");
        ImGui::BulletText("scenes/main.kmscene (Escena inicial jugable)");
        ImGui::BulletText("scripts/*.java (Scripts Java listos para Hot-Reload)");
        ImGui::BulletText("models/, materials/, textures/ y audio/bounce.wav");
        ImGui::BulletText(".vscode/settings.json (Autocompletado Java)");
        ImGui::Spacing();

        if (!m_ProjectError.empty()) {
            ImGui::TextColored(ImVec4(0.96f, 0.36f, 0.38f, 1.0f), "%s", m_ProjectError.c_str());
            ImGui::Spacing();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.58f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.72f, 0.40f, 1.0f));
        if (ImGui::Button("Crear y Abrir Proyecto", ImVec2(-1.0f, 38.0f))) {
            m_ProjectError.clear();
            if (ProjectManager::CreateNewProject(
                    m_NewProjectParentDir,
                    m_NewProjectName,
                    m_NewProjectTemplate,
                    &m_ProjectError)) {
                SyncProjectState(scene);
            }
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::EndChild();

    RenderDirectoryBrowserModal(scene);

    ImGui::End();
}

void EditorPanels::RenderDirectoryBrowserModal(Scene& scene) {
    if (m_OpenFolderPickerPopup) {
        ImGui::OpenPopup("Explorador de Carpetas");
        m_OpenFolderPickerPopup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Explorador de Carpetas", nullptr, ImGuiWindowFlags_NoCollapse)) {
        return;
    }

    if (ImGui::Button("Home")) {
        m_BrowserCurrentDir = GetUserHomeDir();
    }
    ImGui::SameLine();
    if (ImGui::Button("Workspace")) {
        m_BrowserCurrentDir = std::filesystem::current_path();
    }
    ImGui::SameLine();
    if (m_BrowserCurrentDir.has_parent_path() && m_BrowserCurrentDir != m_BrowserCurrentDir.parent_path()) {
        if (ImGui::Button("<- Subir")) {
            m_BrowserCurrentDir = m_BrowserCurrentDir.parent_path();
        }
        ImGui::SameLine();
    }
    ImGui::TextWrapped("%s", m_BrowserCurrentDir.string().c_str());
    ImGui::Separator();

    std::error_code ec;
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(m_BrowserCurrentDir, ec)) {
        const std::string fname = entry.path().filename().string();
        if (!fname.empty() && fname.front() == '.') {
            continue;
        }
        if (entry.is_directory() || fname == "project.kmproj") {
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory()) {
            return a.is_directory() > b.is_directory();
        }
        return a.path().filename().string() < b.path().filename().string();
    });

    if (ImGui::BeginChild("FolderBrowserList", ImVec2(0.0f, 275.0f), true)) {
        for (const auto& entry : entries) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory()) {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.28f, 1.0f), "[DIR]");
                ImGui::SameLine();
                if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_BrowserCurrentDir = entry.path();
                }
            } else {
                ImGui::TextColored(ImVec4(0.25f, 0.68f, 0.98f, 1.0f), "[PROYECTO]");
                ImGui::SameLine();
                if (ImGui::Selectable(name.c_str(), false)) {
                    if (ProjectManager::OpenProject(entry.path())) {
                        SyncProjectState(scene);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##NewSubfolder", m_NewSubfolderName, sizeof(m_NewSubfolderName));
    ImGui::SameLine();
    if (ImGui::Button("+ Nueva Carpeta")) {
        const std::filesystem::path sub = m_BrowserCurrentDir / m_NewSubfolderName;
        std::filesystem::create_directories(sub, ec);
        if (!ec) {
            m_BrowserCurrentDir = sub;
        }
    }

    ImGui::Separator();

    if (m_FolderPickerMode == FolderPickerMode::CreateParentDir) {
        if (ImGui::Button("Seleccionar Esta Carpeta", ImVec2(200.0f, 0.0f))) {
            std::strncpy(
                m_NewProjectParentDir,
                m_BrowserCurrentDir.string().c_str(),
                sizeof(m_NewProjectParentDir) - 1
            );
            m_NewProjectParentDir[sizeof(m_NewProjectParentDir) - 1] = '\0';
            ImGui::CloseCurrentPopup();
        }
    } else if (m_FolderPickerMode == FolderPickerMode::OpenExistingProject) {
        const std::filesystem::path candidate = m_BrowserCurrentDir / "project.kmproj";
        const bool hasProjectFile = std::filesystem::exists(candidate, ec);
        ImGui::BeginDisabled(!hasProjectFile);
        if (ImGui::Button("Abrir project.kmproj", ImVec2(180.0f, 0.0f)) && hasProjectFile) {
            if (ProjectManager::OpenProject(candidate)) {
                SyncProjectState(scene);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(100.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorPanels::RenderAssetImportModal(VulkanRenderer* renderer, Scene& scene) {
    if (m_OpenAssetImportPopup) {
        if (m_AssetImportDir.empty() || !std::filesystem::exists(m_AssetImportDir)) {
            m_AssetImportDir = GetUserHomeDir();
        }
        m_AssetImportError.clear();
        ImGui::OpenPopup("Importar Asset al Proyecto");
        m_OpenAssetImportPopup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Importar Asset al Proyecto", nullptr, ImGuiWindowFlags_NoCollapse)) {
        return;
    }

    if (ImGui::Button("Home")) {
        m_AssetImportDir = GetUserHomeDir();
    }
    ImGui::SameLine();
    if (ImGui::Button("Workspace")) {
        m_AssetImportDir = std::filesystem::current_path();
    }
    ImGui::SameLine();
    if (ImGui::Button("res://") && !m_AssetsRoot.empty()) {
        m_AssetImportDir = m_AssetsRoot;
    }
    ImGui::SameLine();
    if (m_AssetImportDir.has_parent_path() && m_AssetImportDir != m_AssetImportDir.parent_path()) {
        if (ImGui::Button("<- Subir")) {
            m_AssetImportDir = m_AssetImportDir.parent_path();
        }
        ImGui::SameLine();
    }

    const char* filterNames[] = {
        "Todos (.gltf/.glb/.obj/.png/.kmmat/.mtl/.wav/.java/.kmprefab)",
        "Modelos 3D (.gltf / .glb / .obj)",
        "Materiales (.kmmat / .mtl)",
        "Texturas (.png / .ppm)",
        "Audio (.wav)",
        "Scripts (.java)"
    };
    ImGui::SetNextItemWidth(320.0f);
    ImGui::Combo("##ImportFilter", &m_AssetImportFilter, filterNames, 6);

    ImGui::TextWrapped("Carpeta: %s", m_AssetImportDir.string().c_str());
    ImGui::Separator();

    std::error_code ec;
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(m_AssetImportDir, ec)) {
        const std::string fname = entry.path().filename().string();
        if (!fname.empty() && fname.front() == '.') {
            continue;
        }
        if (entry.is_directory()) {
            entries.push_back(entry);
        } else if (entry.is_regular_file() && MatchesImportFilter(ToLowerExt(entry.path()), m_AssetImportFilter)) {
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory()) {
            return a.is_directory() > b.is_directory();
        }
        return a.path().filename().string() < b.path().filename().string();
    });

    if (ImGui::BeginChild("AssetImportBrowserList", ImVec2(0.0f, 285.0f), true)) {
        for (const auto& entry : entries) {
            const std::string fname = entry.path().filename().string();
            if (entry.is_directory()) {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "[DIR]");
                ImGui::SameLine();
                if (ImGui::Selectable(fname.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        m_AssetImportDir = entry.path();
                        m_SelectedImportFile.clear();
                    }
                }
            } else {
                const std::string ext = ToLowerExt(entry.path());
                ImVec4 tagCol(0.75f, 0.80f, 0.88f, 1.0f);
                const char* tagStr = "[FILE]";
                if (ext == ".gltf" || ext == ".glb") {
                    tagCol = ImVec4(0.24f, 0.90f, 0.70f, 1.0f);
                    tagStr = (ext == ".glb") ? "[GLB]" : "[glTF]";
                } else if (ext == ".obj") {
                    tagCol = ImVec4(0.32f, 0.76f, 0.96f, 1.0f);
                    tagStr = "[OBJ]";
                } else if (ext == ".kmmat" || ext == ".mtl") {
                    tagCol = ImVec4(0.96f, 0.45f, 0.78f, 1.0f);
                    tagStr = (ext == ".kmmat") ? "[MAT]" : "[MTL]";
                } else if (ext == ".png" || ext == ".ppm") {
                    tagCol = ImVec4(0.96f, 0.80f, 0.24f, 1.0f);
                    tagStr = "[IMG]";
                } else if (ext == ".kmprefab") {
                    tagCol = ImVec4(0.74f, 0.56f, 0.96f, 1.0f);
                    tagStr = "[PREFAB]";
                } else if (ext == ".wav") {
                    tagCol = ImVec4(0.25f, 0.85f, 0.75f, 1.0f);
                    tagStr = "[WAV]";
                } else if (ext == ".java") {
                    tagCol = ImVec4(0.98f, 0.50f, 0.14f, 1.0f);
                    tagStr = "[JAVA]";
                }

                ImGui::TextColored(tagCol, "%s", tagStr);
                ImGui::SameLine();
                const bool isSel = (m_SelectedImportFile == entry.path());
                if (ImGui::Selectable(fname.c_str(), isSel)) {
                    m_SelectedImportFile = entry.path();
                    m_AssetImportError.clear();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (m_SelectedImportFile.empty()) {
        ImGui::TextDisabled("Selecciona un archivo compatible (.gltf, .glb, .obj, .kmmat, .mtl, .wav, .java).");
    } else {
        ImGui::TextColored(ImVec4(0.45f, 0.86f, 0.98f, 1.0f), "Archivo: %s", m_SelectedImportFile.filename().string().c_str());
    }
    ImGui::Checkbox("Organizar en subcarpeta automatica (models/ materials/ audio/ scripts/)", &m_ImportAutoSubfolder);
    ImGui::Checkbox("Instanciar en escena automaticamente (modelos 3D)", &m_ImportSpawnInScene);
    ImGui::TextDisabled("Nota: Al importar .gltf o .obj se copian automaticamente sus .bin o .mtl adjuntos.");

    if (!m_AssetImportError.empty()) {
        ImGui::TextColored(ImVec4(0.96f, 0.36f, 0.38f, 1.0f), "%s", m_AssetImportError.c_str());
    }

    ImGui::Spacing();
    const bool hasSelection = !m_SelectedImportFile.empty();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Importar al Proyecto", ImVec2(190.0f, 0.0f))) {
        const std::string ext = ToLowerExt(m_SelectedImportFile);
        std::filesystem::path destDir = m_CurrentAssetDir.empty() ? m_AssetsRoot : m_CurrentAssetDir;
        if (m_ImportAutoSubfolder) {
            if (ext == ".gltf" || ext == ".glb" || ext == ".obj") {
                destDir = m_AssetsRoot / "models";
            } else if (ext == ".kmmat" || ext == ".mtl") {
                destDir = m_AssetsRoot / "materials";
            } else if (ext == ".png" || ext == ".ppm") {
                destDir = m_AssetsRoot / "textures";
            } else if (ext == ".kmprefab") {
                destDir = m_AssetsRoot / "prefabs";
            } else if (ext == ".wav") {
                destDir = m_AssetsRoot / "audio";
            } else if (ext == ".java") {
                destDir = m_AssetsRoot / "scripts";
            }
        }

        std::filesystem::path importedPath;
        if (ModelLoader::ImportExternalAssetToDir(m_SelectedImportFile, destDir, &importedPath, &m_AssetImportError)) {
            if (renderer) {
                renderer->ClearModelCache();
            }
            if (ext == ".gltf" || ext == ".glb" || ext == ".obj") {
                if (m_ImportSpawnInScene) {
                    SpawnModelEntity(scene, importedPath);
                }
            } else if (ext == ".java") {
                ScriptEngine::CompileAndReloadScripts(&scene, m_IsPlaying);
                m_ScriptEditor.OpenScript(importedPath);
                m_ShowScriptEditor = true;
                m_FocusScriptEditorNext = true;
            } else if ((ext == ".kmmat" || ext == ".mtl") && scene.IsValid(m_SelectedEntity)) {
                ApplyMaterialToEntity(scene, m_SelectedEntity, importedPath);
            } else if ((ext == ".png" || ext == ".ppm") && scene.IsValid(m_SelectedEntity)) {
                const std::filesystem::path relTex = std::filesystem::relative(importedPath, m_AssetsRoot, ec);
                const std::string texStr = ec ? importedPath.string() : relTex.string();
                auto& reg = scene.GetRegistry();
                if (auto* m = reg.try_get<MeshRendererComponent>(m_SelectedEntity)) {
                    m->texturePath = texStr;
                }
                if (auto* s = reg.try_get<SpriteRendererComponent>(m_SelectedEntity)) {
                    s->texturePath = texStr;
                }
            } else if (ext == ".kmprefab") {
                entt::entity spawned = SceneSerializer::LoadPrefab(scene, importedPath);
                if (scene.IsValid(spawned)) {
                    m_SelectedEntity = spawned;
                }
            }
            m_CurrentAssetDir = destDir;
            m_SelectedImportFile.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(100.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorPanels::RenderMenuBar(Scene& scene, bool& shouldClose) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("Archivo")) {
        if (ImGui::MenuItem("Nueva Escena Default")) {
            if (m_IsPlaying) {
                StopPlayMode(scene);
            }
            scene.CreateDefaultScene();
            m_SelectedEntity = entt::null;
            ResetSceneHistory(scene);
            KM_INFO("Escena por defecto restaurada.");
        }
        if (ImGui::MenuItem("Guardar Escena")) {
            SceneSerializer::SaveToFile(scene, m_ActiveSceneFile);
        }
        if (ImGui::MenuItem("Recargar Escena")) {
            if (m_IsPlaying) {
                StopPlayMode(scene);
            }
            if (SceneSerializer::LoadFromFile(scene, m_ActiveSceneFile)) {
                m_SelectedEntity = entt::null;
                ResetSceneHistory(scene);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Importar Asset Externo...")) {
            m_OpenAssetImportPopup = true;
        }
        if (ImGui::MenuItem("Configuracion de Controles (Input Map)...")) {
            m_ListeningActionName.clear();
            m_OpenInputMapModal = true;
        }
        if (ImGui::MenuItem("Exportar Juego (Standalone)...")) {
            std::string projFolder = "KymEngineGame";
            if (ProjectManager::HasActiveProject()) {
                projFolder = ProjectManager::GetActiveProject().name;
                for (char& ch : projFolder) {
                    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') {
                        ch = '_';
                    }
                }
            }
            std::strncpy(m_ExportExeName, projFolder.c_str(), sizeof(m_ExportExeName) - 1);
            m_ExportExeName[sizeof(m_ExportExeName) - 1] = '\0';

            const char* platDir = (m_ExportPlatformIdx == 0) ? "windows-x64" : "linux-x64";
            const std::filesystem::path defaultOut = ProjectManager::GetActiveProjectDir() / "exports" / platDir;
            const std::string outUtf8 = PlatformPaths::PathToUtf8(defaultOut);
            std::strncpy(m_ExportOutputDir, outUtf8.c_str(), sizeof(m_ExportOutputDir) - 1);
            m_ExportOutputDir[sizeof(m_ExportOutputDir) - 1] = '\0';
            m_ExportStatus.clear();
            m_ExportError.clear();
            m_OpenExportModal = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cerrar Proyecto (Project Manager)")) {
            if (m_IsPlaying) {
                StopPlayMode(scene);
            }
            m_ScriptEditor.CloseAll();
            m_SelectedEntity = entt::null;
            ProjectManager::CloseActiveProject();
        }
        if (ImGui::MenuItem("Salir", "ESC")) {
            shouldClose = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Editar")) {
        if (ImGui::MenuItem("Deshacer Escena", "Ctrl+Z", false, !m_IsPlaying && !m_SceneUndoStack.empty())) {
            UndoScene(scene);
        }
        if (ImGui::MenuItem("Rehacer Escena", "Ctrl+Shift+Z", false, !m_IsPlaying && !m_SceneRedoStack.empty())) {
            RedoScene(scene);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Ventanas")) {
        ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
        ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
        ImGui::MenuItem("Script Editor", nullptr, &m_ShowScriptEditor);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
        ImGui::MenuItem("Stats & Renderer", nullptr, &m_ShowStats);
        ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
        ImGui::MenuItem("FileSystem (res://)", nullptr, &m_ShowContentBrowser);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scripting")) {
        if (ImGui::MenuItem(m_IsPlaying ? "Detener Play (Stop)" : "Ejecutar Escena (Play)")) {
            if (m_IsPlaying) {
                StopPlayMode(scene);
            } else {
                StartPlayMode(scene);
            }
        }
        if (ImGui::MenuItem("Hot-Reload Scripts Java")) {
            ScriptEngine::CompileAndReloadScripts(&scene, m_IsPlaying);
        }
        if (ImGui::MenuItem("Abrir Script Editor")) {
            m_ShowScriptEditor = true;
            m_FocusScriptEditorNext = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Diagnostico")) {
        if (ImGui::MenuItem("Emitir Log Info")) {
            KM_INFO("Evento de diagnostico informativo.");
        }
        if (ImGui::MenuItem("Emitir Log Warning")) {
            KM_WARN("Advertencia de prueba desde el menu Diagnostico.");
        }
        if (ImGui::MenuItem("Limpiar Consola")) {
            Log::ClearLogEntries();
        }
        ImGui::EndMenu();
    }

    if (ProjectManager::HasActiveProject()) {
        const std::string& projName = ProjectManager::GetActiveProject().name;
        const float badgeW = ImGui::CalcTextSize(projName.c_str()).x + 30.0f;
        const float rightX = ImGui::GetWindowWidth() - badgeW;
        if (rightX > ImGui::GetCursorPosX() + 20.0f) {
            ImGui::SameLine(rightX);
        }
        ImGui::TextColored(ImVec4(0.38f, 0.66f, 0.99f, 1.0f), "[%s]", projName.c_str());
    }

    ImGui::EndMainMenuBar();
}

void EditorPanels::RenderHierarchy(Scene& scene) {
    if (!ImGui::Begin("Hierarchy", &m_ShowHierarchy)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Nueva Entidad")) {
        ImGui::OpenPopup("CreateEntityPopup");
    }
    if (ImGui::BeginPopup("CreateEntityPopup")) {
        if (ImGui::MenuItem("Cubo 3D")) {
            entt::entity e = scene.CreateEntity("Cubo");
            scene.GetRegistry().emplace<MeshRendererComponent>(e).type = MeshType::Cube;
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Esfera 3D")) {
            entt::entity e = scene.CreateEntity("Esfera");
            scene.GetRegistry().emplace<MeshRendererComponent>(e).type = MeshType::Sphere;
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Plano 3D")) {
            entt::entity e = scene.CreateEntity("Plano");
            scene.GetRegistry().emplace<MeshRendererComponent>(e).type = MeshType::Plane;
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Sprite 2D")) {
            entt::entity e = scene.CreateEntity("Sprite 2D");
            scene.GetRegistry().emplace<SpriteRendererComponent>(e);
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Luz Direccional")) {
            entt::entity e = scene.CreateEntity("Luz Direccional");
            scene.GetRegistry().emplace<DirectionalLightComponent>(e);
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Luz Puntual (Point Light)")) {
            entt::entity e = scene.CreateEntity("Luz Puntual");
            scene.GetRegistry().get<TransformComponent>(e).translation = {0.0f, 2.2f, 0.0f};
            scene.GetRegistry().emplace<PointLightComponent>(e);
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Camara 3D")) {
            entt::entity e = scene.CreateEntity("Camara Juego");
            scene.GetRegistry().get<TransformComponent>(e).translation = {0.0f, 2.2f, 6.0f};
            auto& cam = scene.GetRegistry().emplace<CameraComponent>(e);
            cam.primary = true;
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Texto UI (HUD)")) {
            entt::entity e = scene.CreateEntity("Texto HUD");
            scene.GetRegistry().emplace<UITextComponent>(e);
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Skybox")) {
            entt::entity e = scene.CreateEntity("Skybox");
            scene.GetRegistry().emplace<SkyboxComponent>(e);
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Entidad Vacia")) {
            m_SelectedEntity = scene.CreateEntity("Entidad Vacia");
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    const bool hasSelection = scene.IsValid(m_SelectedEntity);
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Eliminar") && hasSelection) {
        scene.DestroyEntity(m_SelectedEntity);
        m_SelectedEntity = entt::null;
    }
    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##HierarchySearch", "Buscar entidad...", m_HierarchyFilter, sizeof(m_HierarchyFilter));
    ImGui::Separator();

    auto& registry = scene.GetRegistry();
    std::vector<entt::entity> entities;
    for (auto entity : registry.view<TagComponent>()) {
        entities.push_back(entity);
    }
    std::reverse(entities.begin(), entities.end());

    const std::string_view searchFilter(m_HierarchyFilter);
    entt::entity entityToDelete = entt::null;
    entt::entity entityToSelect = entt::null;

    for (entt::entity entity : entities) {
        const auto& tag = registry.get<TagComponent>(entity).tag;
        if (!ContainsCaseInsensitive(tag, searchFilter)) {
            continue;
        }

        const char* typeTag = "[ENT]";
        ImVec4 typeColor(0.65f, 0.70f, 0.78f, 1.0f);
        if (registry.all_of<DirectionalLightComponent>(entity)) {
            typeTag = "[LUZ]";
            typeColor = ImVec4(0.96f, 0.62f, 0.04f, 1.0f);
        } else if (registry.all_of<CameraComponent>(entity)) {
            typeTag = "[CAM]";
            typeColor = ImVec4(0.66f, 0.33f, 0.97f, 1.0f);
        } else if (registry.all_of<MeshRendererComponent>(entity)) {
            typeTag = "[3D]";
            typeColor = ImVec4(0.23f, 0.51f, 0.96f, 1.0f);
        } else if (registry.all_of<SpriteRendererComponent>(entity)) {
            typeTag = "[2D]";
            typeColor = ImVec4(0.06f, 0.73f, 0.51f, 1.0f);
        }

        const bool hasJavaScript = registry.all_of<JavaScriptComponent>(entity);
        const uint32_t rawId = static_cast<uint32_t>(entt::to_integral(entity));

        ImGui::PushID(static_cast<int>(rawId));
        const bool isSelected = (m_SelectedEntity == entity);
        if (ImGui::Selectable("##EntityRow", isSelected, ImGuiSelectableFlags_AllowOverlap)) {
            m_SelectedEntity = entity;
        }
        if (ImGui::BeginPopupContextItem("EntityContextMenu")) {
            m_SelectedEntity = entity;
            if (ImGui::MenuItem("Duplicar Entidad")) {
                entityToSelect = DuplicateEntity(scene, entity);
            }
            if (ImGui::MenuItem("Resetear Transform")) {
                if (auto* tr = registry.try_get<TransformComponent>(entity)) {
                    tr->translation = {0.0f, 0.0f, 0.0f};
                    tr->rotationDegrees = {0.0f, 0.0f, 0.0f};
                    tr->scale = {1.0f, 1.0f, 1.0f};
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar Entidad")) {
                entityToDelete = entity;
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(6.0f);
        ImGui::TextColored(typeColor, "%s", typeTag);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(tag.c_str());

        if (hasJavaScript) {
            ImGui::SameLine(std::max(10.0f, ImGui::GetWindowContentRegionMax().x - 32.0f));
            ImGui::TextColored(ImVec4(0.98f, 0.45f, 0.09f, 1.0f), "[JV]");
        }

        ImGui::PopID();
    }

    if (entityToSelect != entt::null) {
        m_SelectedEntity = entityToSelect;
    }
    if (entityToDelete != entt::null) {
        if (m_SelectedEntity == entityToDelete) {
            m_SelectedEntity = entt::null;
        }
        scene.DestroyEntity(entityToDelete);
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        m_SelectedEntity = entt::null;
    }

    ImGui::End();
}

void EditorPanels::RenderViewport(VulkanRenderer* renderer, Scene& scene, EditorCamera& camera, Timestep ts) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("Viewport", &m_ShowViewport);
    ImGui::PopStyleVar();

    m_ViewportHovered = false;

    if (open && renderer) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.067f, 0.075f, 0.102f, 1.0f));
        ImGui::BeginChild("ViewportToolbar", ImVec2(0.0f, 38.0f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));

        auto drawGizmoBtn = [&](const char* label, ImGuizmo::OPERATION op) {
            const bool active = (m_GizmoOperation == op);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.231f, 0.510f, 0.965f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.320f, 0.580f, 0.990f, 1.00f));
            }
            if (ImGui::Button(label)) {
                m_GizmoOperation = op;
            }
            if (active) {
                ImGui::PopStyleColor(2);
            }
        };

        drawGizmoBtn("Mover", ImGuizmo::TRANSLATE);
        ImGui::SameLine(0.0f, 4.0f);
        drawGizmoBtn("Rotar", ImGuizmo::ROTATE);
        ImGui::SameLine(0.0f, 4.0f);
        drawGizmoBtn("Escalar", ImGuizmo::SCALE);
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button(m_GizmoMode == ImGuizmo::LOCAL ? "Local" : "Global")) {
            m_GizmoMode = (m_GizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::BeginDisabled(m_IsPlaying || m_SceneUndoStack.empty());
        if (ImGui::Button("Deshacer##SceneUndo")) {
            UndoScene(scene);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Deshacer cambio en escena (Ctrl+Z)");
        }
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::BeginDisabled(m_IsPlaying || m_SceneRedoStack.empty());
        if (ImGui::Button("Rehacer##SceneRedo")) {
            RedoScene(scene);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Rehacer cambio en escena (Ctrl+Shift+Z)");
        }

        const float toolbarWidth = ImGui::GetWindowWidth();
        const float centerX = std::max(ImGui::GetCursorPosX() + 14.0f, toolbarWidth * 0.5f - 140.0f);
        ImGui::SameLine(centerX);

        if (m_IsPlaying) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.76f, 0.19f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.26f, 0.28f, 1.0f));
            if (ImGui::Button("Stop")) {
                StopPlayMode(scene);
            }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.58f, 0.31f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.72f, 0.39f, 1.0f));
            if (ImGui::Button("Play")) {
                StartPlayMode(scene);
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine(0.0f, 5.0f);
        if (ImGui::Button("Hot-Reload")) {
            ScriptEngine::CompileAndReloadScripts(&scene, m_IsPlaying);
        }
        ImGui::SameLine(0.0f, 5.0f);
        if (ImGui::Button("</>" " Script")) {
            m_ShowScriptEditor = true;
            m_FocusScriptEditorNext = true;
        }

        const float rightTarget = toolbarWidth - 285.0f;
        if (rightTarget > ImGui::GetCursorPosX() + 12.0f) {
            ImGui::SameLine(rightTarget);
        } else {
            ImGui::SameLine(0.0f, 10.0f);
        }

        const bool is2D = camera.Is2D();
        if (ImGui::Button(is2D ? "2D Orto" : "3D Persp")) {
            camera.Set2D(!is2D);
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("Reset")) {
            camera.Reset();
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("Guardar")) {
            SceneSerializer::SaveToFile(scene, m_ActiveSceneFile);
        }
        ImGui::SameLine(0.0f, 8.0f);
        const float fps = (ts.GetSeconds() > 0.00001f) ? (1.0f / ts.GetSeconds()) : 0.0f;
        ImGui::TextColored(ImVec4(0.42f, 0.78f, 0.98f, 1.0f), "%.0f FPS", fps);

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 64.0f && avail.y >= 64.0f) {
            const uint32_t targetW = static_cast<uint32_t>(avail.x);
            const uint32_t targetH = static_cast<uint32_t>(avail.y);
            const VkExtent2D currentExtent = renderer->GetSceneExtent();
            if (targetW != currentExtent.width || targetH != currentExtent.height) {
                renderer->RequestSceneResize(targetW, targetH);
            }
        }

        VkDescriptorSet sceneDesc = renderer->GetSceneTextureDescriptor();
        if (sceneDesc != VK_NULL_HANDLE && avail.x > 0.0f && avail.y > 0.0f) {
            ImGui::Image(reinterpret_cast<ImTextureID>(sceneDesc), avail);
            m_ViewportHovered = ImGui::IsItemHovered();

            if (IsPlayingInProcess()) {
                const ImVec2 imgMin = ImGui::GetItemRectMin();
                const ImVec2 imgSize = ImGui::GetItemRectSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                UISystem::UpdateAndRender(dl, imgMin, imgSize, m_ViewportHovered);
                if (TouchControlsOverlay::ShouldShow(false)) {
                    TouchControlsOverlay::RenderAndProcess(dl, imgMin, imgSize, m_ViewportHovered);
                }
            }

            if (m_ViewportHovered) {
                m_ScrollDelta += ImGui::GetIO().MouseWheel;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGuizmo::IsUsing()) {
                    if (ImGui::IsKeyPressed(ImGuiKey_W)) m_GizmoOperation = ImGuizmo::TRANSLATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_E)) m_GizmoOperation = ImGuizmo::ROTATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_R)) m_GizmoOperation = ImGuizmo::SCALE;
                }
            }

            const ImVec2 imageMin = ImGui::GetItemRectMin();
            const ImVec2 imageMax = ImGui::GetItemRectMax();
            const ImVec2 imageSize = ImGui::GetItemRectSize();
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            if (m_IsPlaying) {
                drawList->AddRect(
                    ImVec2(imageMin.x + 1.5f, imageMin.y + 1.5f),
                    ImVec2(imageMax.x - 1.5f, imageMax.y - 1.5f),
                    IM_COL32(34, 197, 94, 225),
                    0.0f,
                    0,
                    3.0f
                );
            }

            if (imageSize.x > 260.0f && imageSize.y > 110.0f) {
                const glm::vec3 camPos = camera.GetPosition();
                const VkExtent2D scExtent = renderer->GetSceneExtent();
                std::string selectedName = "Ninguna";
                if (scene.IsValid(m_SelectedEntity) && scene.GetRegistry().all_of<TagComponent>(m_SelectedEntity)) {
                    selectedName = scene.GetRegistry().get<TagComponent>(m_SelectedEntity).tag;
                }

                char line1[128]{};
                char line2[128]{};
                char line3[128]{};
                std::snprintf(line1, sizeof(line1), "Vista: %s  |  %ux%u",
                              camera.Is2D() ? "2D Ortografica" : "3D Perspectiva",
                              scExtent.width, scExtent.height);
                std::snprintf(line2, sizeof(line2), "Camara: (%.1f, %.1f, %.1f)", camPos.x, camPos.y, camPos.z);
                std::snprintf(line3, sizeof(line3), "Seleccion: %s", selectedName.c_str());

                const ImVec2 s1 = ImGui::CalcTextSize(line1);
                const ImVec2 s2 = ImGui::CalcTextSize(line2);
                const ImVec2 s3 = ImGui::CalcTextSize(line3);
                const float maxW = std::max({s1.x, s2.x, s3.x});
                const float lineH = ImGui::GetTextLineHeight();

                const ImVec2 hudMin(imageMin.x + 12.0f, imageMin.y + 12.0f);
                const ImVec2 hudMax(hudMin.x + maxW + 20.0f, hudMin.y + lineH * 3.0f + 20.0f);

                drawList->AddRectFilled(hudMin, hudMax, IM_COL32(15, 17, 24, 195), 6.0f);
                drawList->AddRect(hudMin, hudMax, IM_COL32(59, 130, 246, 115), 6.0f, 0, 1.0f);
                drawList->AddText(ImVec2(hudMin.x + 10.0f, hudMin.y + 7.0f), IM_COL32(230, 236, 245, 255), line1);
                drawList->AddText(ImVec2(hudMin.x + 10.0f, hudMin.y + 10.0f + lineH), IM_COL32(165, 178, 198, 255), line2);
                drawList->AddText(ImVec2(hudMin.x + 10.0f, hudMin.y + 13.0f + lineH * 2.0f), IM_COL32(96, 165, 250, 255), line3);
            }

            // Render in-game UITextComponent overlays inside Viewport bounds
            {
                const auto& uiReg = scene.GetRegistry();
                ImFont* uiFont = ImGui::GetFont();
                const float baseFontSize = ImGui::GetFontSize();
                for (auto uiEnt : uiReg.view<UITextComponent>()) {
                    const auto& ui = uiReg.get<UITextComponent>(uiEnt);
                    if (!ui.visible || ui.text.empty()) {
                        continue;
                    }
                    const ImU32 textCol = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(ui.color.r, ui.color.g, ui.color.b, ui.color.a)
                    );
                    drawList->AddText(
                        uiFont,
                        baseFontSize * std::clamp(ui.fontScale, 0.5f, 4.0f),
                        ImVec2(imageMin.x + ui.screenPos.x, imageMin.y + ui.screenPos.y),
                        textCol,
                        ui.text.c_str()
                    );
                }
            }

            if (!m_IsPlaying &&
                imageSize.x > 1.0f &&
                imageSize.y > 1.0f &&
                scene.IsValid(m_SelectedEntity) &&
                scene.GetRegistry().all_of<TransformComponent>(m_SelectedEntity)) {
                auto& tr = scene.GetRegistry().get<TransformComponent>(m_SelectedEntity);

                ImGuizmo::SetOrthographic(camera.Is2D());
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

                const float aspect = imageSize.x / imageSize.y;
                const glm::mat4 view = camera.GetViewMatrix();
                const glm::mat4 proj = camera.GetProjectionMatrix(aspect);
                glm::mat4 model = tr.GetTransform();

                if (ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(proj),
                        m_GizmoOperation,
                        m_GizmoMode,
                        glm::value_ptr(model))) {
                    ImGuizmo::DecomposeMatrixToComponents(
                        glm::value_ptr(model),
                        glm::value_ptr(tr.translation),
                        glm::value_ptr(tr.rotationDegrees),
                        glm::value_ptr(tr.scale)
                    );
                }
            }

            if (m_IsPlaying && (m_RuntimePid > 0 || m_RuntimeProcessHandle != 0) && imageSize.x > 260.0f) {
                const char* playBanner = "JUGANDO EN VENTANA DE JUEGO EXTERNA (Sin Edicion) — Pulsa Stop o ESC en la ventana";
                const ImVec2 bSize = ImGui::CalcTextSize(playBanner);
                const float bx = imageMin.x + (imageSize.x - bSize.x - 24.0f) * 0.5f;
                const float by = imageMin.y + 12.0f;
                drawList->AddRectFilled(
                    ImVec2(bx, by),
                    ImVec2(bx + bSize.x + 24.0f, by + bSize.y + 14.0f),
                    IM_COL32(16, 108, 62, 230),
                    6.0f
                );
                drawList->AddText(ImVec2(bx + 12.0f, by + 7.0f), IM_COL32(240, 252, 245, 255), playBanner);
            }

            if (!m_IsPlaying &&
                m_ViewportHovered &&
                imageSize.x > 1.0f &&
                imageSize.y > 1.0f &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing() &&
                !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
                const ImVec2 mousePos = ImGui::GetIO().MousePos;
                if (mousePos.x >= imageMin.x && mousePos.x <= imageMax.x &&
                    mousePos.y >= imageMin.y && mousePos.y <= imageMax.y) {
                    m_SelectedEntity = PickEntityInViewport(scene, camera, mousePos, imageMin, imageSize);
                }
            }
        } else {
            ImGui::TextDisabled("Inicializando Viewport Vulkan...");
        }
    }

    ImGui::End();
}

void EditorPanels::RenderInspector(Scene& scene) {
    if (!ImGui::Begin("Inspector", &m_ShowInspector)) {
        ImGui::End();
        return;
    }

    if (!scene.IsValid(m_SelectedEntity)) {
        ImGui::Spacing();
        ImGui::TextDisabled("Selecciona una entidad en Hierarchy para editar sus propiedades.");
        ImGui::End();
        return;
    }

    auto& registry = scene.GetRegistry();

    if (auto* tag = registry.try_get<TagComponent>(m_SelectedEntity)) {
        char buf[128]{};
        std::strncpy(buf, tag->tag.c_str(), sizeof(buf) - 1);
        ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 145.0f));
        if (ImGui::InputText("##EntityTag", buf, sizeof(buf))) {
            tag->tag = buf;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Componente")) {
            ImGui::OpenPopup("AddComponentPopup");
        }
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!registry.all_of<HierarchyComponent>(m_SelectedEntity) && ImGui::MenuItem("Hierarchy (Padre-Hijo)")) {
            registry.emplace<HierarchyComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<MeshRendererComponent>(m_SelectedEntity) && ImGui::MenuItem("Mesh Renderer 3D")) {
            registry.emplace<MeshRendererComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<SpriteRendererComponent>(m_SelectedEntity) && ImGui::MenuItem("Sprite Renderer 2D")) {
            registry.emplace<SpriteRendererComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<DirectionalLightComponent>(m_SelectedEntity) && ImGui::MenuItem("Directional Light")) {
            registry.emplace<DirectionalLightComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<PointLightComponent>(m_SelectedEntity) && ImGui::MenuItem("Point Light")) {
            registry.emplace<PointLightComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<SkyboxComponent>(m_SelectedEntity) && ImGui::MenuItem("Skybox")) {
            registry.emplace<SkyboxComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<UITextComponent>(m_SelectedEntity) && ImGui::MenuItem("UI Text (HUD)")) {
            registry.emplace<UITextComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<CameraComponent>(m_SelectedEntity) && ImGui::MenuItem("Camera")) {
            registry.emplace<CameraComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<JavaScriptComponent>(m_SelectedEntity) && ImGui::MenuItem("Java Script")) {
            registry.emplace<JavaScriptComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<RigidBodyComponent>(m_SelectedEntity) && ImGui::MenuItem("Rigid Body (Fisicas)")) {
            registry.emplace<RigidBodyComponent>(m_SelectedEntity);
        }
        if (!registry.all_of<AudioSourceComponent>(m_SelectedEntity) && ImGui::MenuItem("Audio Source")) {
            registry.emplace<AudioSourceComponent>(m_SelectedEntity);
        }
        ImGui::EndPopup();
    }

    ImGui::TextDisabled("ID Entidad: %u", static_cast<uint32_t>(entt::to_integral(m_SelectedEntity)));
    ImGui::SameLine();
    if (ImGui::Button("Guardar Prefab (.kmprefab)")) {
        std::string prefabName = "Entity";
        if (const auto* t = registry.try_get<TagComponent>(m_SelectedEntity)) {
            prefabName = t->tag;
        }
        for (char& ch : prefabName) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                ch = '_';
            }
        }
        if (prefabName.empty()) {
            prefabName = "Entity";
        }
        SceneSerializer::SavePrefab(scene, m_SelectedEntity, m_AssetsRoot / "prefabs" / (prefabName + ".kmprefab"));
    }
    ImGui::Separator();

    if (auto* hier = registry.try_get<HierarchyComponent>(m_SelectedEntity)) {
        bool removeHier = false;
        if (DrawComponentHeader("Hierarchy (Padre)", "HdrHierarchy", &removeHier)) {
            if (BeginInspectorTable("HierTable")) {
                BeginInspectorRow("Entidad Padre");
                const char* previewParent = hier->parentTag.empty() ? "(Ninguno / Raiz)" : hier->parentTag.c_str();
                if (ImGui::BeginCombo("##ParentPicker", previewParent)) {
                    if (ImGui::Selectable("(Ninguno / Raiz)", hier->parentTag.empty())) {
                        hier->parentTag.clear();
                    }
                    std::string selfTag;
                    if (const auto* st = registry.try_get<TagComponent>(m_SelectedEntity)) {
                        selfTag = st->tag;
                    }
                    for (auto candidate : registry.view<TagComponent>()) {
                        if (candidate == m_SelectedEntity) {
                            continue;
                        }
                        const std::string& cTag = registry.get<TagComponent>(candidate).tag;
                        if (cTag == selfTag) {
                            continue;
                        }
                        if (ImGui::Selectable(cTag.c_str(), hier->parentTag == cTag)) {
                            hier->parentTag = cTag;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndTable();
            }
        }
        if (removeHier) {
            registry.remove<HierarchyComponent>(m_SelectedEntity);
        }
    }

    if (auto* tr = registry.try_get<TransformComponent>(m_SelectedEntity)) {
        if (DrawComponentHeader("Transform", "HdrTransform", nullptr)) {
            if (BeginInspectorTable("TransformTable")) {
                DrawVec3Control("Posicion", tr->translation, 0.0f, 0.05f);
                DrawVec3Control("Rotacion", tr->rotationDegrees, 0.0f, 0.5f);
                DrawVec3Control("Escala", tr->scale, 1.0f, 0.05f);
                ImGui::EndTable();
            }
        }
    }

    if (auto* mesh = registry.try_get<MeshRendererComponent>(m_SelectedEntity)) {
        bool removeMesh = false;
        if (DrawComponentHeader("Mesh Renderer 3D", "HdrMesh", &removeMesh)) {
            if (BeginInspectorTable("MeshTable")) {
                BeginInspectorRow("Malla");
                const char* meshTypes[] = {"Cube", "Sphere", "Plane", "Custom 3D (.obj / .gltf / .glb)"};
                int currentType = static_cast<int>(mesh->type);
                if (ImGui::Combo("##MeshType", &currentType, meshTypes, 4)) {
                    mesh->type = static_cast<MeshType>(currentType);
                }

                if (mesh->type == MeshType::CustomOBJ) {
                    BeginInspectorRow("Ruta Modelo");
                    char pathBuf[256]{};
                    std::strncpy(pathBuf, mesh->objPath.c_str(), sizeof(pathBuf) - 1);
                    if (ImGui::InputText("##ObjPath", pathBuf, sizeof(pathBuf))) {
                        mesh->objPath = pathBuf;
                    }

                    BeginInspectorRow("En Proyecto");
                    const auto modelFiles = CollectProjectFilesByExt(m_AssetsRoot, {".gltf", ".glb", ".obj"});
                    const char* previewModel = mesh->objPath.empty() ? "(Seleccionar modelo)" : mesh->objPath.c_str();
                    if (ImGui::BeginCombo("##ModelPicker", previewModel)) {
                        for (const auto& mf : modelFiles) {
                            const bool isSel = (mf == mesh->objPath);
                            if (ImGui::Selectable(mf.c_str(), isSel)) {
                                mesh->objPath = mf;
                                LoadedModelData loaded;
                                if (ModelLoader::LoadModel(m_AssetsRoot / mf, loaded) && loaded.hasMaterial) {
                                    mesh->color = loaded.primaryMaterial.baseColor;
                                    mesh->metallic = loaded.primaryMaterial.metallic;
                                    mesh->roughness = loaded.primaryMaterial.roughness;
                                    mesh->emissive = loaded.primaryMaterial.emissive;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }

                    BeginInspectorRow("Material Modelo");
                    if (ImGui::Button("Leer Material Embebido")) {
                        LoadedModelData loaded;
                        std::filesystem::path resolved = mesh->objPath;
                        if (!std::filesystem::exists(resolved)) {
                            resolved = m_AssetsRoot / mesh->objPath;
                        }
                        if (ModelLoader::LoadModel(resolved, loaded) && loaded.hasMaterial) {
                            mesh->color = loaded.primaryMaterial.baseColor;
                            mesh->metallic = loaded.primaryMaterial.metallic;
                            mesh->roughness = loaded.primaryMaterial.roughness;
                            mesh->emissive = loaded.primaryMaterial.emissive;
                            KM_INFO("Material '{}' cargado desde modelo.", loaded.primaryMaterial.name);
                        } else {
                            KM_WARN("El modelo no contiene un material glTF/MTL embebido.");
                        }
                    }
                }

                BeginInspectorRow("Material Asset");
                const auto matFiles = CollectProjectFilesByExt(m_AssetsRoot, {".kmmat", ".mtl"});
                const char* previewMat = mesh->materialPath.empty() ? "(Personalizado en Entidad)" : mesh->materialPath.c_str();
                if (ImGui::BeginCombo("##MatPicker", previewMat)) {
                    for (const auto& matRel : matFiles) {
                        const bool isSel = (matRel == mesh->materialPath);
                        if (ImGui::Selectable(matRel.c_str(), isSel)) {
                            ApplyMaterialToEntity(scene, m_SelectedEntity, m_AssetsRoot / matRel);
                        }
                    }
                    ImGui::EndCombo();
                }

                BeginInspectorRow("Textura (.png)");
                const auto texFiles = CollectProjectFilesByExt(m_AssetsRoot, {".png", ".ppm"});
                const char* previewMeshTex = mesh->texturePath.empty() ? "(Sin Textura)" : mesh->texturePath.c_str();
                if (ImGui::BeginCombo("##MeshTexPicker", previewMeshTex)) {
                    if (ImGui::Selectable("(Sin Textura)", mesh->texturePath.empty())) {
                        mesh->texturePath.clear();
                    }
                    for (const auto& tf : texFiles) {
                        if (ImGui::Selectable(tf.c_str(), tf == mesh->texturePath)) {
                            mesh->texturePath = tf;
                        }
                    }
                    ImGui::EndCombo();
                }

                BeginInspectorRow("Albedo");
                ImGui::ColorEdit4("##MeshColor", &mesh->color.x);

                BeginInspectorRow("Metallic");
                ImGui::SliderFloat("##MeshMetallic", &mesh->metallic, 0.0f, 1.0f);

                BeginInspectorRow("Roughness");
                ImGui::SliderFloat("##MeshRoughness", &mesh->roughness, 0.05f, 1.0f);

                BeginInspectorRow("Emissive");
                ImGui::ColorEdit3("##MeshEmissive", &mesh->emissive.x);

                BeginInspectorRow("Sombras");
                ImGui::Checkbox("##MeshCastShadows", &mesh->castShadows);

                BeginInspectorRow("Exportar Mat.");
                if (ImGui::Button("Guardar como .kmmat")) {
                    std::string baseName = "Material";
                    if (const auto* t = registry.try_get<TagComponent>(m_SelectedEntity)) {
                        baseName = t->tag;
                    }
                    for (char& ch : baseName) {
                        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                            ch = '_';
                        }
                    }
                    if (baseName.empty()) {
                        baseName = "Material";
                    }
                    PBRMaterialData matData;
                    matData.name = baseName;
                    matData.baseColor = mesh->color;
                    matData.metallic = mesh->metallic;
                    matData.roughness = mesh->roughness;
                    matData.emissive = mesh->emissive;

                    const std::filesystem::path savePath = m_AssetsRoot / "materials" / (baseName + ".kmmat");
                    if (ModelLoader::SaveMaterialFile(savePath, matData)) {
                        mesh->materialPath = "materials/" + baseName + ".kmmat";
                        KM_INFO("Material guardado en 'res://{}'.", mesh->materialPath);
                    }
                }

                BeginInspectorRow("Visible");
                ImGui::Checkbox("##MeshVisible", &mesh->visible);

                ImGui::EndTable();
            }
        }
        if (removeMesh) {
            registry.remove<MeshRendererComponent>(m_SelectedEntity);
        }
    }

    if (auto* sprite = registry.try_get<SpriteRendererComponent>(m_SelectedEntity)) {
        bool removeSprite = false;
        if (DrawComponentHeader("Sprite Renderer 2D", "HdrSprite", &removeSprite)) {
            if (BeginInspectorTable("SpriteTable")) {
                BeginInspectorRow("Textura (.png)");
                const auto sprTexFiles = CollectProjectFilesByExt(m_AssetsRoot, {".png", ".ppm"});
                const char* previewSprTex = sprite->texturePath.empty() ? "(Sin Textura)" : sprite->texturePath.c_str();
                if (ImGui::BeginCombo("##SpriteTexPicker", previewSprTex)) {
                    if (ImGui::Selectable("(Sin Textura)", sprite->texturePath.empty())) {
                        sprite->texturePath.clear();
                    }
                    for (const auto& tf : sprTexFiles) {
                        if (ImGui::Selectable(tf.c_str(), tf == sprite->texturePath)) {
                            sprite->texturePath = tf;
                        }
                    }
                    ImGui::EndCombo();
                }

                BeginInspectorRow("Tint");
                ImGui::ColorEdit4("##SpriteColor", &sprite->color.x);

                BeginInspectorRow("Orden");
                ImGui::DragInt("##SpriteOrder", &sprite->sortingOrder);

                BeginInspectorRow("Checker");
                ImGui::Checkbox("##SpriteChecker", &sprite->checkerPattern);

                BeginInspectorRow("H Frames");
                if (ImGui::DragInt("##SpriteHFrames", &sprite->hFrames, 1.0f, 1, 64)) {
                    sprite->hFrames = std::max(1, sprite->hFrames);
                }

                BeginInspectorRow("V Frames");
                if (ImGui::DragInt("##SpriteVFrames", &sprite->vFrames, 1.0f, 1, 64)) {
                    sprite->vFrames = std::max(1, sprite->vFrames);
                }

                BeginInspectorRow("Frame");
                if (ImGui::DragInt("##SpriteFrame", &sprite->frameIndex, 1.0f, 0, 256)) {
                    sprite->frameIndex = std::max(0, sprite->frameIndex);
                }

                BeginInspectorRow("Anim FPS");
                ImGui::DragFloat("##SpriteAnimFps", &sprite->animFps, 0.25f, 0.0f, 60.0f);

                BeginInspectorRow("Visible");
                ImGui::Checkbox("##SpriteVisible", &sprite->visible);

                ImGui::EndTable();
            }
        }
        if (removeSprite) {
            registry.remove<SpriteRendererComponent>(m_SelectedEntity);
        }
    }

    if (auto* light = registry.try_get<DirectionalLightComponent>(m_SelectedEntity)) {
        bool removeLight = false;
        if (DrawComponentHeader("Directional Light", "HdrLight", &removeLight)) {
            if (BeginInspectorTable("LightTable")) {
                BeginInspectorRow("Color Luz");
                ImGui::ColorEdit3("##LightColor", &light->color.x);

                BeginInspectorRow("Intensidad");
                ImGui::DragFloat("##LightIntensity", &light->intensity, 0.05f, 0.0f, 10.0f);

                ImGui::EndTable();
            }
        }
        if (removeLight) {
            registry.remove<DirectionalLightComponent>(m_SelectedEntity);
        }
    }

    if (auto* pl = registry.try_get<PointLightComponent>(m_SelectedEntity)) {
        bool removePl = false;
        if (DrawComponentHeader("Point Light", "HdrPointLight", &removePl)) {
            if (BeginInspectorTable("PointLightTable")) {
                BeginInspectorRow("Color");
                ImGui::ColorEdit3("##PlColor", &pl->color.x);

                BeginInspectorRow("Intensidad");
                ImGui::DragFloat("##PlIntensity", &pl->intensity, 0.1f, 0.0f, 25.0f);

                BeginInspectorRow("Rango");
                ImGui::DragFloat("##PlRange", &pl->range, 0.2f, 0.5f, 100.0f);

                ImGui::EndTable();
            }
        }
        if (removePl) {
            registry.remove<PointLightComponent>(m_SelectedEntity);
        }
    }

    if (auto* sky = registry.try_get<SkyboxComponent>(m_SelectedEntity)) {
        bool removeSky = false;
        if (DrawComponentHeader("Skybox", "HdrSkybox", &removeSky)) {
            if (BeginInspectorTable("SkyboxTable")) {
                BeginInspectorRow("Activo");
                ImGui::Checkbox("##SkyEnabled", &sky->enabled);

                BeginInspectorRow("Cenit");
                ImGui::ColorEdit3("##SkyZenith", &sky->zenithColor.x);

                BeginInspectorRow("Horizonte");
                ImGui::ColorEdit3("##SkyHorizon", &sky->horizonColor.x);

                BeginInspectorRow("Suelo");
                ImGui::ColorEdit3("##SkyGround", &sky->groundColor.x);

                ImGui::EndTable();
            }
        }
        if (removeSky) {
            registry.remove<SkyboxComponent>(m_SelectedEntity);
        }
    }

    if (auto* ui = registry.try_get<UITextComponent>(m_SelectedEntity)) {
        bool removeUi = false;
        if (DrawComponentHeader("UI Text (HUD)", "HdrUiText", &removeUi)) {
            if (BeginInspectorTable("UiTextTable")) {
                BeginInspectorRow("Texto");
                char txtBuf[256]{};
                std::strncpy(txtBuf, ui->text.c_str(), sizeof(txtBuf) - 1);
                if (ImGui::InputText("##UiTextStr", txtBuf, sizeof(txtBuf))) {
                    ui->text = txtBuf;
                }

                BeginInspectorRow("Pos X/Y");
                ImGui::DragFloat2("##UiScreenPos", &ui->screenPos.x, 1.0f, 0.0f, 3840.0f);

                BeginInspectorRow("Escala");
                ImGui::DragFloat("##UiScale", &ui->fontScale, 0.05f, 0.5f, 4.0f);

                BeginInspectorRow("Color");
                ImGui::ColorEdit4("##UiColor", &ui->color.x);

                BeginInspectorRow("Visible");
                ImGui::Checkbox("##UiVisible", &ui->visible);

                ImGui::EndTable();
            }
        }
        if (removeUi) {
            registry.remove<UITextComponent>(m_SelectedEntity);
        }
    }

    if (auto* cam = registry.try_get<CameraComponent>(m_SelectedEntity)) {
        bool removeCam = false;
        if (DrawComponentHeader("Camera", "HdrCam", &removeCam)) {
            if (BeginInspectorTable("CameraTable")) {
                BeginInspectorRow("Principal");
                ImGui::Checkbox("##CamPrimary", &cam->primary);

                BeginInspectorRow("Ortografica");
                ImGui::Checkbox("##CamOrtho", &cam->isOrthographic);

                if (cam->isOrthographic) {
                    BeginInspectorRow("Ortho Size");
                    ImGui::DragFloat("##CamOrthoSize", &cam->orthoSize, 0.1f, 0.5f, 50.0f);
                } else {
                    BeginInspectorRow("FOV");
                    ImGui::DragFloat("##CamFov", &cam->fov, 0.5f, 15.0f, 120.0f);
                }

                BeginInspectorRow("Near Clip");
                ImGui::DragFloat("##CamNear", &cam->nearClip, 0.05f);

                BeginInspectorRow("Far Clip");
                ImGui::DragFloat("##CamFar", &cam->farClip, 1.0f);

                ImGui::EndTable();
            }
        }
        if (removeCam) {
            registry.remove<CameraComponent>(m_SelectedEntity);
        }
    }

    if (auto* js = registry.try_get<JavaScriptComponent>(m_SelectedEntity)) {
        bool removeJs = false;
        if (DrawComponentHeader("Java Script", "HdrJavaScript", &removeJs)) {
            if (BeginInspectorTable("JsTable")) {
                const std::vector<std::string> available = ScriptEngine::GetAvailableScriptNames();
                if (!available.empty()) {
                    BeginInspectorRow("Script");
                    if (ImGui::BeginCombo("##JsCombo", js->className.c_str())) {
                        for (const auto& name : available) {
                            const bool isSelected = (js->className == name);
                            if (ImGui::Selectable(name.c_str(), isSelected)) {
                                js->className = name;
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                BeginInspectorRow("Clase");
                char classBuf[128]{};
                std::strncpy(classBuf, js->className.c_str(), sizeof(classBuf) - 1);
                if (ImGui::InputText("##JsClass", classBuf, sizeof(classBuf))) {
                    js->className = classBuf;
                }

                BeginInspectorRow("Habilitado");
                ImGui::Checkbox("##JsEnabled", &js->enabled);

                BeginInspectorRow("Estado JVM");
                if (js->instanceCreated) {
                    ImGui::TextColored(ImVec4(0.22f, 0.88f, 0.48f, 1.0f), "Instancia activa");
                } else {
                    ImGui::TextDisabled("Inactivo");
                }

                BeginInspectorRow("Codigo");
                if (ImGui::Button("Editar en Script Editor")) {
                    const std::filesystem::path scriptFile =
                        ScriptEngine::GetScriptsDirectory() / (js->className + ".java");
                    if (std::filesystem::exists(scriptFile)) {
                        m_ScriptEditor.OpenScript(scriptFile);
                    } else {
                        m_ScriptEditor.CreateNewScriptFile(ScriptEngine::GetScriptsDirectory(), js->className);
                    }
                    m_ShowScriptEditor = true;
                    m_FocusScriptEditorNext = true;
                }

                ImGui::EndTable();
            }
        }
        if (removeJs) {
            registry.remove<JavaScriptComponent>(m_SelectedEntity);
        }
    }

    if (auto* rb = registry.try_get<RigidBodyComponent>(m_SelectedEntity)) {
        bool removeRb = false;
        if (DrawComponentHeader("Rigid Body (Fisicas)", "HdrRigidBody", &removeRb)) {
            if (BeginInspectorTable("RigidBodyTable")) {
                BeginInspectorRow("Tipo");
                const char* bodyTypes[] = {"Static", "Dynamic", "Kinematic"};
                int currentBody = static_cast<int>(rb->bodyType);
                if (ImGui::Combo("##RbType", &currentBody, bodyTypes, 3)) {
                    rb->bodyType = static_cast<RigidBodyType>(currentBody);
                }

                BeginInspectorRow("Colisionador");
                const char* shapes[] = {"Box3D", "Sphere3D", "Box2D"};
                int currentShape = static_cast<int>(rb->shape);
                if (ImGui::Combo("##RbShape", &currentShape, shapes, 3)) {
                    rb->shape = static_cast<ColliderShape>(currentShape);
                }

                if (rb->shape == ColliderShape::Sphere3D) {
                    BeginInspectorRow("Radio");
                    ImGui::DragFloat("##RbRadius", &rb->radius, 0.02f, 0.05f, 50.0f);
                } else {
                    DrawVec3Control("Half Extents", rb->halfExtents, 0.5f, 0.02f);
                }

                DrawVec3Control("Velocidad", rb->velocity, 0.0f, 0.05f);
                DrawVec3Control("Vel. Angular", rb->angularVelocity, 0.0f, 0.5f);

                BeginInspectorRow("Masa");
                ImGui::DragFloat("##RbMass", &rb->mass, 0.05f, 0.05f, 1000.0f);

                BeginInspectorRow("Restitucion");
                ImGui::SliderFloat("##RbRestitution", &rb->restitution, 0.0f, 1.0f);

                BeginInspectorRow("Friccion");
                ImGui::SliderFloat("##RbFriction", &rb->friction, 0.0f, 1.0f);

                BeginInspectorRow("Gravedad");
                ImGui::Checkbox("##RbGravity", &rb->useGravity);

                BeginInspectorRow("Es Trigger");
                ImGui::Checkbox("##RbTrigger", &rb->isTrigger);

                ImGui::EndTable();
            }
        }
        if (removeRb) {
            registry.remove<RigidBodyComponent>(m_SelectedEntity);
        }
    }

    if (auto* audio = registry.try_get<AudioSourceComponent>(m_SelectedEntity)) {
        bool removeAudio = false;
        if (DrawComponentHeader("Audio Source", "HdrAudioSource", &removeAudio)) {
            if (BeginInspectorTable("AudioTable")) {
                BeginInspectorRow("Ruta WAV");
                char pathBuf[256]{};
                std::strncpy(pathBuf, audio->soundPath.c_str(), sizeof(pathBuf) - 1);
                if (ImGui::InputText("##AudioPath", pathBuf, sizeof(pathBuf))) {
                    audio->soundPath = pathBuf;
                }

                BeginInspectorRow("Volumen");
                ImGui::SliderFloat("##AudioVolume", &audio->volume, 0.0f, 1.0f);

                BeginInspectorRow("Al Iniciar");
                ImGui::Checkbox("##AudioStart", &audio->playOnStart);

                BeginInspectorRow("Al Rebotar");
                ImGui::Checkbox("##AudioBounce", &audio->playOnBounce);

                BeginInspectorRow("Prueba");
                if (ImGui::Button("Reproducir Sonido")) {
                    AudioEngine::PlaySound(audio->soundPath, audio->volume);
                }

                ImGui::EndTable();
            }
        }
        if (removeAudio) {
            registry.remove<AudioSourceComponent>(m_SelectedEntity);
        }
    }

    ImGui::End();
}

void EditorPanels::RenderStats(VulkanRenderer* renderer, Timestep ts, uint32_t frameCount) {
    if (!ImGui::Begin("Stats & Renderer", &m_ShowStats)) {
        ImGui::End();
        return;
    }

    const float ms = ts.GetMilliseconds();
    const float fps = (ts.GetSeconds() > 0.00001f) ? (1.0f / ts.GetSeconds()) : 0.0f;

    ImGui::Text("KymEngine — Obsidian UI");
    ImGui::Separator();

    if (BeginInspectorTable("StatsTable")) {
        if (ProjectManager::HasActiveProject()) {
            BeginInspectorRow("Proyecto");
            ImGui::TextWrapped("%s", ProjectManager::GetActiveProject().name.c_str());
        }
        if (renderer) {
            BeginInspectorRow("GPU");
            ImGui::TextWrapped("%s", renderer->GetDeviceName().c_str());

            const VkExtent2D extent = renderer->GetSceneExtent();
            BeginInspectorRow("Viewport");
            ImGui::Text("%ux%u", extent.width, extent.height);
        }

        BeginInspectorRow("Frame");
        ImGui::Text("%u", frameCount);

        BeginInspectorRow("Frame Time");
        ImGui::Text("%.3f ms", ms);

        BeginInspectorRow("FPS");
        ImGui::Text("%.1f", fps);

        if (renderer) {
            BeginInspectorRow("Fondo");
            glm::vec4 clearColor = renderer->GetClearColor();
            if (ImGui::ColorEdit4("##ClearColor", &clearColor.x)) {
                renderer->SetClearColor(clearColor);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Atajos y Controles");
    ImGui::BulletText("Click Der. + WASD/QE: Camara 3D");
    ImGui::BulletText("Click Der. / Rueda: Pan y Zoom 2D");
    ImGui::BulletText("W / E / R: Mover / Rotar / Escalar");
    ImGui::BulletText("Ctrl + S en Script Editor: Guardar y Hot-Reload");

    ImGui::Spacing();
    if (ImGui::Button("Emitir Log de Prueba")) {
        KM_INFO("Log de prueba emitido desde el panel Stats & Renderer.");
    }

    ImGui::End();
}

void EditorPanels::RenderConsole() {
    if (!ImGui::Begin("Console", &m_ShowConsole)) {
        ImGui::End();
        return;
    }

    const std::vector<LogEntry> entries = Log::GetLogEntries();
    int traceCount = 0;
    int infoCount = 0;
    int warnCount = 0;
    int errorCount = 0;
    for (const auto& entry : entries) {
        switch (entry.level) {
            case spdlog::level::trace:
            case spdlog::level::debug:    ++traceCount; break;
            case spdlog::level::info:     ++infoCount; break;
            case spdlog::level::warn:     ++warnCount; break;
            case spdlog::level::err:
            case spdlog::level::critical: ++errorCount; break;
            default: break;
        }
    }

    if (ImGui::Button("Limpiar")) {
        Log::ClearLogEntries();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

    char lblInfo[32]{};
    char lblWarn[32]{};
    char lblErr[32]{};
    char lblTrace[32]{};
    std::snprintf(lblInfo, sizeof(lblInfo), "Info (%d)", infoCount);
    std::snprintf(lblWarn, sizeof(lblWarn), "Warn (%d)", warnCount);
    std::snprintf(lblErr, sizeof(lblErr), "Error (%d)", errorCount);
    std::snprintf(lblTrace, sizeof(lblTrace), "Trace (%d)", traceCount);

    ImGui::SameLine();
    DrawFilterToggle(lblInfo, m_ShowInfo, ImVec4(0.32f, 0.85f, 0.56f, 1.0f));
    ImGui::SameLine(0.0f, 4.0f);
    DrawFilterToggle(lblWarn, m_ShowWarn, ImVec4(0.98f, 0.76f, 0.24f, 1.0f));
    ImGui::SameLine(0.0f, 4.0f);
    DrawFilterToggle(lblErr, m_ShowError, ImVec4(0.96f, 0.36f, 0.38f, 1.0f));
    ImGui::SameLine(0.0f, 4.0f);
    DrawFilterToggle(lblTrace, m_ShowTrace, ImVec4(0.58f, 0.63f, 0.72f, 1.0f));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ConsoleFilter", "Filtrar logs...", m_FilterBuffer, sizeof(m_FilterBuffer));

    ImGui::Separator();

    if (ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        const ImGuiIO& io = ImGui::GetIO();
        ImFont* monoFont = (io.Fonts->Fonts.Size > 1) ? io.Fonts->Fonts[1] : nullptr;
        if (monoFont) {
            ImGui::PushFont(monoFont);
        }

        const std::string_view filter(m_FilterBuffer);
        for (const auto& entry : entries) {
            if ((entry.level == spdlog::level::trace || entry.level == spdlog::level::debug) && !m_ShowTrace) continue;
            if (entry.level == spdlog::level::info && !m_ShowInfo) continue;
            if (entry.level == spdlog::level::warn && !m_ShowWarn) continue;
            if ((entry.level == spdlog::level::err || entry.level == spdlog::level::critical) && !m_ShowError) continue;

            if (!ContainsCaseInsensitive(entry.loggerName, filter) &&
                !ContainsCaseInsensitive(entry.message, filter)) {
                continue;
            }

            const ImVec4 color = ColorForLogLevel(entry.level);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const std::string line = "[" + entry.loggerName + "] [" + LabelForLogLevel(entry.level) + "] " + entry.message;
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        if (monoFont) {
            ImGui::PopFont();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void EditorPanels::DrawVectorAssetIcon(
    const std::filesystem::path& entryPath,
    bool isDir,
    ImVec2 minPos,
    ImVec2 maxPos
) const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = maxPos.x - minPos.x;
    const float h = maxPos.y - minPos.y;
    const float cx = (minPos.x + maxPos.x) * 0.5f;
    const float cy = (minPos.y + maxPos.y) * 0.5f;
    const float s = std::min(w, h) * 0.34f;

    if (isDir) {
        const ImU32 folderTop = IM_COL32(217, 159, 43, 255);
        const ImU32 folderBody = IM_COL32(245, 185, 66, 255);
        dl->AddRectFilled(
            ImVec2(cx - s, cy - s * 0.72f),
            ImVec2(cx - s * 0.1f, cy - s * 0.35f),
            folderTop,
            3.0f
        );
        dl->AddRectFilled(
            ImVec2(cx - s, cy - s * 0.45f),
            ImVec2(cx + s, cy + s * 0.72f),
            folderBody,
            4.0f
        );
        return;
    }

    const std::string ext = ToLowerExt(entryPath);
    if (ext == ".kmscene") {
        // Scene clapperboard / emerald frame
        const ImU32 emerald = IM_COL32(34, 197, 94, 255);
        const ImU32 emeraldDark = IM_COL32(20, 120, 62, 255);
        dl->AddRectFilled(ImVec2(cx - s, cy - s * 0.75f), ImVec2(cx + s, cy - s * 0.40f), emeraldDark, 3.0f);
        dl->AddRectFilled(ImVec2(cx - s, cy - s * 0.35f), ImVec2(cx + s, cy + s * 0.75f), emerald, 4.0f);
        dl->AddTriangleFilled(
            ImVec2(cx - s * 0.28f, cy - s * 0.10f),
            ImVec2(cx - s * 0.28f, cy + s * 0.48f),
            ImVec2(cx + s * 0.38f, cy + s * 0.19f),
            IM_COL32(240, 253, 244, 255)
        );
    } else if (ext == ".java") {
        // Java document + {J} emblem
        const ImU32 orange = IM_COL32(249, 115, 22, 255);
        const ImU32 darkOrange = IM_COL32(154, 52, 18, 255);
        dl->AddRectFilled(ImVec2(cx - s * 0.78f, cy - s * 0.90f), ImVec2(cx + s * 0.78f, cy + s * 0.90f), orange, 4.0f);
        dl->AddTriangleFilled(
            ImVec2(cx + s * 0.28f, cy - s * 0.90f),
            ImVec2(cx + s * 0.78f, cy - s * 0.90f),
            ImVec2(cx + s * 0.78f, cy - s * 0.40f),
            darkOrange
        );
        const ImVec2 txtSize = ImGui::CalcTextSize("{J}");
        dl->AddText(
            ImVec2(cx - txtSize.x * 0.5f, cy - txtSize.y * 0.5f + 2.0f),
            IM_COL32(255, 247, 237, 255),
            "{J}"
        );
    } else if (ext == ".gltf" || ext == ".glb") {
        // glTF 2.0 3D emerald/cyan prism + badge
        const ImU32 teal = IM_COL32(34, 211, 168, 255);
        const ImVec2 top(cx, cy - s * 0.90f);
        const ImVec2 topL(cx - s * 0.80f, cy - s * 0.42f);
        const ImVec2 topR(cx + s * 0.80f, cy - s * 0.42f);
        const ImVec2 center(cx, cy);
        const ImVec2 botL(cx - s * 0.80f, cy + s * 0.45f);
        const ImVec2 botR(cx + s * 0.80f, cy + s * 0.45f);
        const ImVec2 bot(cx, cy + s * 0.90f);
        dl->AddQuadFilled(top, topR, center, topL, IM_COL32(20, 140, 118, 170));
        dl->AddQuad(top, topR, center, topL, teal, 2.0f);
        dl->AddQuad(topL, center, bot, botL, teal, 2.0f);
        dl->AddQuad(center, topR, botR, bot, teal, 2.0f);
        const char* tag = (ext == ".glb") ? "GLB" : "glTF";
        const ImVec2 ts = ImGui::CalcTextSize(tag);
        dl->AddText(ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), IM_COL32(236, 253, 245, 255), tag);
    } else if (ext == ".kmmat" || ext == ".mtl") {
        // Shaded PBR material sphere
        dl->AddCircleFilled(ImVec2(cx, cy), s * 0.82f, IM_COL32(219, 39, 119, 255), 24);
        dl->AddCircleFilled(ImVec2(cx - s * 0.25f, cy - s * 0.28f), s * 0.34f, IM_COL32(249, 168, 212, 220), 16);
        dl->AddCircleFilled(ImVec2(cx - s * 0.32f, cy - s * 0.35f), s * 0.14f, IM_COL32(255, 255, 255, 245), 12);
        dl->AddCircle(ImVec2(cx, cy), s * 0.82f, IM_COL32(251, 207, 232, 210), 24, 1.5f);
    } else if (ext == ".obj") {
        // Wireframe isometric 3D cube
        const ImU32 cyan = IM_COL32(56, 189, 248, 255);
        const ImVec2 top(cx, cy - s * 0.90f);
        const ImVec2 topL(cx - s * 0.80f, cy - s * 0.42f);
        const ImVec2 topR(cx + s * 0.80f, cy - s * 0.42f);
        const ImVec2 center(cx, cy);
        const ImVec2 botL(cx - s * 0.80f, cy + s * 0.45f);
        const ImVec2 botR(cx + s * 0.80f, cy + s * 0.45f);
        const ImVec2 bot(cx, cy + s * 0.90f);
        dl->AddQuad(top, topR, center, topL, cyan, 2.0f);
        dl->AddQuad(topL, center, bot, botL, cyan, 2.0f);
        dl->AddQuad(center, topR, botR, bot, cyan, 2.0f);
    } else if (ext == ".png" || ext == ".ppm") {
        // Framed mountain image icon
        dl->AddRectFilled(ImVec2(cx - s * 0.85f, cy - s * 0.72f), ImVec2(cx + s * 0.85f, cy + s * 0.72f), IM_COL32(30, 58, 138, 255), 4.0f);
        dl->AddRect(ImVec2(cx - s * 0.85f, cy - s * 0.72f), ImVec2(cx + s * 0.85f, cy + s * 0.72f), IM_COL32(250, 204, 21, 235), 4.0f, 0, 1.5f);
        dl->AddTriangleFilled(
            ImVec2(cx - s * 0.72f, cy + s * 0.56f),
            ImVec2(cx - s * 0.15f, cy - s * 0.25f),
            ImVec2(cx + s * 0.30f, cy + s * 0.56f),
            IM_COL32(52, 211, 153, 255)
        );
        dl->AddTriangleFilled(
            ImVec2(cx - s * 0.05f, cy + s * 0.56f),
            ImVec2(cx + s * 0.42f, cy - s * 0.08f),
            ImVec2(cx + s * 0.72f, cy + s * 0.56f),
            IM_COL32(16, 185, 129, 255)
        );
        dl->AddCircleFilled(ImVec2(cx + s * 0.38f, cy - s * 0.34f), s * 0.16f, IM_COL32(253, 224, 71, 255), 12);
    } else if (ext == ".kmprefab") {
        // Violet prefab block icon
        dl->AddRectFilled(ImVec2(cx - s * 0.80f, cy - s * 0.80f), ImVec2(cx + s * 0.80f, cy + s * 0.80f), IM_COL32(124, 58, 237, 255), 5.0f);
        dl->AddRect(ImVec2(cx - s * 0.45f, cy - s * 0.45f), ImVec2(cx + s * 0.45f, cy + s * 0.45f), IM_COL32(237, 233, 254, 240), 2.0f, 0, 2.0f);
    } else if (ext == ".wav") {
        // Audio waveform bars
        const ImU32 teal = IM_COL32(45, 212, 191, 255);
        const float heights[5] = {0.35f, 0.75f, 1.00f, 0.65f, 0.40f};
        for (int b = 0; b < 5; ++b) {
            const float bx = cx + (static_cast<float>(b) - 2.0f) * (s * 0.36f);
            const float bh = s * 0.82f * heights[b];
            dl->AddRectFilled(
                ImVec2(bx - s * 0.11f, cy - bh),
                ImVec2(bx + s * 0.11f, cy + bh),
                teal,
                2.0f
            );
        }
    } else if (ext == ".vert" || ext == ".frag" || ext == ".spv") {
        // Shader diamond prism
        const ImU32 purple = IM_COL32(168, 85, 247, 255);
        dl->AddQuadFilled(
            ImVec2(cx, cy - s * 0.90f),
            ImVec2(cx + s * 0.85f, cy),
            ImVec2(cx, cy + s * 0.90f),
            ImVec2(cx - s * 0.85f, cy),
            purple
        );
        dl->AddLine(ImVec2(cx - s * 0.85f, cy), ImVec2(cx + s * 0.85f, cy), IM_COL32(250, 245, 255, 210), 1.5f);
    } else if (ext == ".kmproj") {
        // Project star/emblem
        const ImU32 blue = IM_COL32(59, 130, 246, 255);
        dl->AddRectFilled(ImVec2(cx - s * 0.85f, cy - s * 0.85f), ImVec2(cx + s * 0.85f, cy + s * 0.85f), blue, 6.0f);
        dl->AddCircleFilled(ImVec2(cx, cy), s * 0.38f, IM_COL32(239, 246, 255, 255));
    } else {
        // Generic file page
        const ImU32 slate = IM_COL32(148, 163, 184, 255);
        dl->AddRectFilled(ImVec2(cx - s * 0.72f, cy - s * 0.88f), ImVec2(cx + s * 0.72f, cy + s * 0.88f), slate, 3.0f);
        dl->AddLine(ImVec2(cx - s * 0.42f, cy - s * 0.25f), ImVec2(cx + s * 0.42f, cy - s * 0.25f), IM_COL32(30, 41, 59, 255), 2.0f);
        dl->AddLine(ImVec2(cx - s * 0.42f, cy + s * 0.15f), ImVec2(cx + s * 0.42f, cy + s * 0.15f), IM_COL32(30, 41, 59, 255), 2.0f);
    }
}

void EditorPanels::RenderDirectoryTree(const std::filesystem::path& dir) {
    std::error_code ec;
    std::vector<std::filesystem::path> subdirs;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_directory()) {
            const std::string name = entry.path().filename().string();
            if (!name.empty() && name.front() != '.') {
                subdirs.push_back(entry.path());
            }
        }
    }
    std::sort(subdirs.begin(), subdirs.end());

    for (const auto& sub : subdirs) {
        const std::string name = sub.filename().string();
        bool hasChildDirs = false;
        for (const auto& child : std::filesystem::directory_iterator(sub, ec)) {
            if (child.is_directory()) {
                const std::string cname = child.path().filename().string();
                if (!cname.empty() && cname.front() != '.') {
                    hasChildDirs = true;
                    break;
                }
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildDirs) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (m_CurrentAssetDir == sub) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        const bool opened = ImGui::TreeNodeEx(sub.string().c_str(), flags, "%s", name.c_str());
        if (ImGui::IsItemClicked()) {
            m_CurrentAssetDir = sub;
        }
        if (opened && hasChildDirs) {
            RenderDirectoryTree(sub);
            ImGui::TreePop();
        }
    }
}

void EditorPanels::RenderContentBrowser(Scene& scene) {
    if (!ImGui::Begin("FileSystem (res://)", &m_ShowContentBrowser)) {
        ImGui::End();
        return;
    }

    if (!std::filesystem::exists(m_CurrentAssetDir)) {
        m_CurrentAssetDir = m_AssetsRoot;
    }
    if (!std::filesystem::exists(m_AssetsRoot)) {
        ImGui::TextDisabled("Carpeta del proyecto no encontrada.");
        ImGui::End();
        return;
    }

    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float treeW = std::clamp(availW * 0.24f, 150.0f, 260.0f);

    // Left panel: res:// folder tree
    if (ImGui::BeginChild("ResTreePane", ImVec2(treeW, availH), true)) {
        const bool rootSelected = (m_CurrentAssetDir == m_AssetsRoot);
        if (ImGui::Selectable("res://", rootSelected)) {
            m_CurrentAssetDir = m_AssetsRoot;
        }
        ImGui::Separator();
        RenderDirectoryTree(m_AssetsRoot);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: breadcrumbs, create menu and vector-icon grid
    if (ImGui::BeginChild("ResGridPane", ImVec2(0.0f, availH), true)) {
        if (m_CurrentAssetDir != m_AssetsRoot) {
            if (ImGui::Button("<- Subir")) {
                m_CurrentAssetDir = m_CurrentAssetDir.parent_path();
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("res://")) {
            m_CurrentAssetDir = m_AssetsRoot;
        }
        std::error_code ec;
        const std::filesystem::path relDir = std::filesystem::relative(m_CurrentAssetDir, m_AssetsRoot, ec);
        if (!ec && relDir != ".") {
            std::filesystem::path accum = m_AssetsRoot;
            for (const auto& part : relDir) {
                accum /= part;
                ImGui::SameLine(0.0f, 3.0f);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0.0f, 3.0f);
                if (ImGui::Button(part.string().c_str())) {
                    m_CurrentAssetDir = accum;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("+ Crear")) {
            ImGui::OpenPopup("FsCreatePopup");
        }
        if (ImGui::BeginPopup("FsCreatePopup")) {
            ImGui::InputTextWithHint("##FsNewName", "Nombre...", m_FsNewName, sizeof(m_FsNewName));
            if (ImGui::MenuItem("Nuevo Script Java (.java)")) {
                m_ScriptEditor.CreateNewScriptFile(ScriptEngine::GetScriptsDirectory(), m_FsNewName);
                ScriptEngine::CompileAndReloadScripts(&scene, m_IsPlaying);
                m_ShowScriptEditor = true;
                m_FocusScriptEditorNext = true;
            }
            if (ImGui::MenuItem("Nueva Escena (.kmscene)")) {
                Scene newScene;
                newScene.CreateDefaultScene();
                std::string fname = std::string(m_FsNewName);
                if (fname.empty()) fname = "NewScene";
                SceneSerializer::SaveToFile(newScene, m_CurrentAssetDir / (fname + ".kmscene"));
            }
            if (ImGui::MenuItem("Nuevo Material PBR (.kmmat)")) {
                std::string fname = std::string(m_FsNewName);
                if (fname.empty()) fname = "NewMaterial";
                PBRMaterialData mat;
                mat.name = fname;
                mat.baseColor = {0.28f, 0.68f, 0.96f, 1.0f};
                mat.metallic = 0.45f;
                mat.roughness = 0.32f;
                ModelLoader::SaveMaterialFile(m_CurrentAssetDir / (fname + ".kmmat"), mat);
            }
            if (ImGui::MenuItem("Nueva Carpeta")) {
                std::filesystem::create_directories(m_CurrentAssetDir / m_FsNewName, ec);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.46f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.46f, 1.0f));
        if (ImGui::Button("+ Importar Asset...")) {
            m_OpenAssetImportPopup = true;
        }
        ImGui::PopStyleColor(2);

        const float sliderWidth = 110.0f;
        const float rightPos = ImGui::GetWindowWidth() - sliderWidth - 18.0f;
        if (rightPos > ImGui::GetCursorPosX() + 14.0f) {
            ImGui::SameLine(rightPos);
        } else {
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##CardSize", &m_ContentCardSize, 72.0f, 130.0f, "%.0f px");

        ImGui::Separator();

        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentAssetDir, ec)) {
            const std::string fname = entry.path().filename().string();
            if (!fname.empty() && fname.front() == '.') {
                continue;
            }
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) {
                return a.is_directory() > b.is_directory();
            }
            return a.path().filename().string() < b.path().filename().string();
        });

        const float gridAvailW = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const int columns = std::max(1, static_cast<int>(gridAvailW / (m_ContentCardSize + 14.0f)));
        std::filesystem::path pendingDelete;

        if (ImGui::BeginTable("FileSystemGrid", columns)) {
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                const std::string name = entry.path().filename().string();
                const std::string ext = ToLowerExt(entry.path());

                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));

                const float cardW = m_ContentCardSize;
                const float cardH = m_ContentCardSize * 0.78f;
                const bool clicked = ImGui::InvisibleButton("##AssetCard", ImVec2(cardW, cardH));
                const bool hovered = ImGui::IsItemHovered();

                const ImVec2 cMin = ImGui::GetItemRectMin();
                const ImVec2 cMax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(
                    cMin,
                    cMax,
                    hovered ? IM_COL32(38, 46, 64, 255) : IM_COL32(25, 29, 40, 255),
                    7.0f
                );
                dl->AddRect(
                    cMin,
                    cMax,
                    hovered ? IM_COL32(59, 130, 246, 220) : IM_COL32(46, 54, 72, 190),
                    7.0f,
                    0,
                    1.2f
                );

                DrawVectorAssetIcon(entry.path(), entry.is_directory(), cMin, cMax);

                if (hovered) {
                    const std::filesystem::path relFile = std::filesystem::relative(entry.path(), m_AssetsRoot, ec);
                    ImGui::SetTooltip("res://%s", ec ? name.c_str() : relFile.string().c_str());
                }

                if (ImGui::BeginPopupContextItem("AssetCardCtx")) {
                    if (ext == ".java") {
                        if (ImGui::MenuItem("Editar en Script Editor")) {
                            m_ScriptEditor.OpenScript(entry.path());
                            m_ShowScriptEditor = true;
                            m_FocusScriptEditorNext = true;
                        }
                        if (scene.IsValid(m_SelectedEntity) && ImGui::MenuItem("Asignar a Entidad Seleccionada")) {
                            const std::string className = entry.path().stem().string();
                            auto& reg = scene.GetRegistry();
                            if (auto* js = reg.try_get<JavaScriptComponent>(m_SelectedEntity)) {
                                js->className = className;
                                js->enabled = true;
                            } else {
                                reg.emplace<JavaScriptComponent>(m_SelectedEntity, JavaScriptComponent{className, true, false});
                            }
                        }
                    } else if (ext == ".gltf" || ext == ".glb" || ext == ".obj") {
                        if (ImGui::MenuItem("Instanciar Modelo 3D en Escena")) {
                            SpawnModelEntity(scene, entry.path());
                        }
                    } else if (ext == ".kmmat" || ext == ".mtl") {
                        if (scene.IsValid(m_SelectedEntity) && ImGui::MenuItem("Aplicar Material a Entidad Seleccionada")) {
                            ApplyMaterialToEntity(scene, m_SelectedEntity, entry.path());
                        }
                    } else if (ext == ".png" || ext == ".ppm") {
                        if (scene.IsValid(m_SelectedEntity) && ImGui::MenuItem("Asignar Textura a Entidad Seleccionada")) {
                            const std::filesystem::path relTex = std::filesystem::relative(entry.path(), m_AssetsRoot, ec);
                            const std::string texStr = ec ? entry.path().string() : relTex.string();
                            auto& reg = scene.GetRegistry();
                            if (auto* m = reg.try_get<MeshRendererComponent>(m_SelectedEntity)) {
                                m->texturePath = texStr;
                            }
                            if (auto* s = reg.try_get<SpriteRendererComponent>(m_SelectedEntity)) {
                                s->texturePath = texStr;
                            }
                            KM_INFO("Textura 'res://{}' asignada.", texStr);
                        }
                    } else if (ext == ".kmprefab") {
                        if (ImGui::MenuItem("Instanciar Prefab en Escena")) {
                            entt::entity spawned = SceneSerializer::LoadPrefab(scene, entry.path());
                            if (scene.IsValid(spawned)) {
                                m_SelectedEntity = spawned;
                            }
                        }
                    } else if (ext == ".kmscene") {
                        if (ImGui::MenuItem("Cargar Escena")) {
                            if (m_IsPlaying) {
                                StopPlayMode(scene);
                            }
                            if (SceneSerializer::LoadFromFile(scene, entry.path())) {
                                m_ActiveSceneFile = entry.path();
                                m_SelectedEntity = entt::null;
                                ResetSceneHistory(scene);
                            }
                        }
                    }
                    if (name != "project.kmproj") {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Eliminar")) {
                            pendingDelete = entry.path();
                        }
                    }
                    ImGui::EndPopup();
                }

                if (clicked) {
                    if (entry.is_directory()) {
                        m_CurrentAssetDir = entry.path();
                    } else if (ext == ".kmscene") {
                        if (m_IsPlaying) {
                            StopPlayMode(scene);
                        }
                        if (SceneSerializer::LoadFromFile(scene, entry.path())) {
                            m_ActiveSceneFile = entry.path();
                            m_SelectedEntity = entt::null;
                            ResetSceneHistory(scene);
                        }
                    } else if (ext == ".gltf" || ext == ".glb" || ext == ".obj") {
                        SpawnModelEntity(scene, entry.path());
                    } else if (ext == ".kmmat" || ext == ".mtl") {
                        ApplyMaterialToEntity(scene, m_SelectedEntity, entry.path());
                    } else if (ext == ".png" || ext == ".ppm") {
                        if (scene.IsValid(m_SelectedEntity)) {
                            const std::filesystem::path relTex = std::filesystem::relative(entry.path(), m_AssetsRoot, ec);
                            const std::string texStr = ec ? entry.path().string() : relTex.string();
                            auto& reg = scene.GetRegistry();
                            if (auto* m = reg.try_get<MeshRendererComponent>(m_SelectedEntity)) {
                                m->texturePath = texStr;
                            }
                            if (auto* s = reg.try_get<SpriteRendererComponent>(m_SelectedEntity)) {
                                s->texturePath = texStr;
                            }
                            KM_INFO("Textura 'res://{}' asignada.", texStr);
                        }
                    } else if (ext == ".kmprefab") {
                        entt::entity spawned = SceneSerializer::LoadPrefab(scene, entry.path());
                        if (scene.IsValid(spawned)) {
                            m_SelectedEntity = spawned;
                        }
                    } else if (ext == ".java") {
                        m_ScriptEditor.OpenScript(entry.path());
                        m_ShowScriptEditor = true;
                        m_FocusScriptEditorNext = true;
                    } else if (ext == ".wav") {
                        const std::filesystem::path relWav = std::filesystem::relative(entry.path(), m_AssetsRoot, ec);
                        const std::string audioRel = ec ? entry.path().string() : relWav.string();
                        AudioEngine::PlaySound(audioRel, 0.8f);
                        if (scene.IsValid(m_SelectedEntity)) {
                            auto& reg = scene.GetRegistry();
                            if (auto* audio = reg.try_get<AudioSourceComponent>(m_SelectedEntity)) {
                                audio->soundPath = audioRel;
                            }
                        }
                        KM_INFO("Reproduciendo audio 'res://{}'.", audioRel);
                    }
                }

                ImGui::TextWrapped("%s", name.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!pendingDelete.empty()) {
            std::filesystem::remove_all(pendingDelete, ec);
            KM_INFO("Eliminado '{}'.", pendingDelete.string());
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void EditorPanels::RenderExportProjectModal() {
    if (m_IsExporting && m_ExportFuture.valid() &&
        m_ExportFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const bool ok = m_ExportFuture.get();
        m_IsExporting = false;
        if (ok) {
            m_ExportStatus = std::string("Exportado correctamente en: ") + m_ExportOutputDir;
            m_ExportError.clear();
        } else if (m_ExportError.empty()) {
            m_ExportError = "No se pudo completar la exportacion.";
        }
    }

    if (m_OpenExportModal) {
        ImGui::OpenPopup("Exportar Juego");
        m_OpenExportModal = false;
    }
    if (!ImGui::BeginPopupModal("Exportar Juego", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const char* platforms[] = {"Windows (x64)", "Linux (x64)", "Android (.apk)"};
    if (ImGui::Combo("Plataforma destino", &m_ExportPlatformIdx, platforms, 3)) {
        const char* platDir = "linux-x64";
        if (m_ExportPlatformIdx == 0) platDir = "windows-x64";
        else if (m_ExportPlatformIdx == 2) platDir = "android-apk";
        const std::filesystem::path defaultOut = ProjectManager::GetActiveProjectDir() / "exports" / platDir;
        const std::string outUtf8 = PlatformPaths::PathToUtf8(defaultOut);
        std::strncpy(m_ExportOutputDir, outUtf8.c_str(), sizeof(m_ExportOutputDir) - 1);
        m_ExportOutputDir[sizeof(m_ExportOutputDir) - 1] = '\0';
    }

    ExportPlatform targetPlat = ExportPlatform::LinuxX64;
    if (m_ExportPlatformIdx == 0) targetPlat = ExportPlatform::WindowsX64;
    else if (m_ExportPlatformIdx == 2) targetPlat = ExportPlatform::AndroidAPK;
    if (ProjectManager::HasExportTemplate(targetPlat)) {
        ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.45f, 1.0f), "Plantilla de exportacion lista localmente.");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                           "Se descargara automaticamente la plantilla desde GitHub Releases al exportar.");
    }

    ImGui::InputText("Nombre del ejecutable", m_ExportExeName, sizeof(m_ExportExeName));
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText("Carpeta de salida", m_ExportOutputDir, sizeof(m_ExportOutputDir));
    ImGui::Checkbox("Comprimir paquete (.zip / .tar.gz) listo para compartir", &m_ExportCreateArchive);

    if (!m_ExportStatus.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.45f, 1.0f), "%s", m_ExportStatus.c_str());
    }
    if (!m_ExportError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", m_ExportError.c_str());
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(m_IsExporting);
    if (ImGui::Button(m_IsExporting ? "Exportando..." : "Exportar", ImVec2(140.0f, 0.0f))) {
        m_ExportStatus.clear();
        m_ExportError.clear();
        ExportOptions opts;
        opts.platform = targetPlat;
        opts.outputDir = PlatformPaths::Utf8ToPath(m_ExportOutputDir);
        opts.executableName = m_ExportExeName;
        opts.downloadTemplateIfMissing = true;
        opts.createArchive = m_ExportCreateArchive;
        m_IsExporting = true;
        m_ExportStatus = "Preparando plantilla y exportando proyecto...";
        m_ExportFuture = std::async(std::launch::async, [this, opts]() {
            std::string err;
            const bool ok = ProjectManager::ExportProjectForPlatform(opts, &err);
            if (!ok) {
                m_ExportError = err;
            }
            return ok;
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_IsExporting);
    if (ImGui::Button("Cerrar", ImVec2(100.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
}

void EditorPanels::RenderInputMapModal() {
    if (m_OpenInputMapModal) {
        ImGui::OpenPopup("Configuracion de Controles (Input Map)");
        m_OpenInputMapModal = false;
    }
    if (!ImGui::BeginPopupModal("Configuracion de Controles (Input Map)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // Listen for key/mouse press when assigning a new binding
    if (!m_ListeningActionName.empty()) {
        GLFWwindow* win = glfwGetCurrentContext();
        if (win) {
            if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                m_ListeningActionName.clear();
            } else {
                bool assigned = false;
                for (int key : InputMap::GetListenKeyCandidates()) {
                    if (glfwGetKey(win, key) == GLFW_PRESS) {
                        InputMap::AddBinding(m_ListeningActionName, {InputDeviceType::Key, key});
                        m_ListeningActionName.clear();
                        assigned = true;
                        break;
                    }
                }
                if (!assigned) {
                    const int mouseBtns[] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE};
                    for (int mb : mouseBtns) {
                        if (glfwGetMouseButton(win, mb) == GLFW_PRESS) {
                            InputMap::AddBinding(m_ListeningActionName, {InputDeviceType::MouseButton, mb});
                            m_ListeningActionName.clear();
                            break;
                        }
                    }
                }
            }
        }
    }

    ImGui::TextUnformatted("Acciones del Input Map (llamalas en Java con Input.isActionPressed(\"nombre\"))");
    ImGui::Separator();

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##NewActionName", m_NewActionName, sizeof(m_NewActionName));
    ImGui::SameLine();
    if (ImGui::Button("+ Nueva Accion") && m_NewActionName[0] != '\0') {
        InputMap::SetActionBindings(m_NewActionName, {});
        m_NewActionName[0] = '\0';
    }
    ImGui::Spacing();

    const auto& actions = InputMap::GetAllActions();
    std::vector<std::string> sortedNames;
    sortedNames.reserve(actions.size());
    for (const auto& [name, _] : actions) {
        sortedNames.push_back(name);
    }
    std::sort(sortedNames.begin(), sortedNames.end());

    std::string actionToRemove;
    if (ImGui::BeginTable("##InputMapTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(640.0f, 240.0f))) {
        ImGui::TableSetupColumn("Accion", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Teclas / Botones", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Opciones", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableHeadersRow();

        for (const std::string& name : sortedNames) {
            const auto& bindings = actions.at(name);
            ImGui::PushID(name.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name.c_str());

            ImGui::TableSetColumnIndex(1);
            if (bindings.empty()) {
                ImGui::TextDisabled("(sin asignar)");
            } else {
                std::string joined;
                for (size_t i = 0; i < bindings.size(); ++i) {
                    if (i > 0) joined += ", ";
                    joined += InputMap::BindingToString(bindings[i]);
                }
                ImGui::TextWrapped("%s", joined.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            const bool isListening = (m_ListeningActionName == name);
            if (ImGui::Button(isListening ? "Pulsa tecla..." : "+ Asignar")) {
                m_ListeningActionName = isListening ? "" : name;
            }
            ImGui::SameLine();
            if (ImGui::Button("Limpiar")) {
                InputMap::SetActionBindings(name, {});
            }
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                actionToRemove = name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!actionToRemove.empty()) {
        InputMap::RemoveAction(actionToRemove);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Controles Tactiles en Pantalla (Movil / Simulacion PC)");
    TouchControlsConfig& tc = TouchControlsOverlay::GetConfig();
    ImGui::Checkbox("Activar controles tactiles en Android/Movil", &tc.enabledOnMobile);
    ImGui::Checkbox("Simular controles tactiles en PC (Viewport)", &tc.simulateInEditor);

    char bufA[64]{};
    char bufALbl[16]{};
    char bufB[64]{};
    char bufBLbl[16]{};
    std::strncpy(bufA, tc.buttonAAction.c_str(), sizeof(bufA) - 1);
    std::strncpy(bufALbl, tc.buttonALabel.c_str(), sizeof(bufALbl) - 1);
    std::strncpy(bufB, tc.buttonBAction.c_str(), sizeof(bufB) - 1);
    std::strncpy(bufBLbl, tc.buttonBLabel.c_str(), sizeof(bufBLbl) - 1);

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Accion Boton A", bufA, sizeof(bufA))) tc.buttonAAction = bufA;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::InputText("Etiqueta A", bufALbl, sizeof(bufALbl))) tc.buttonALabel = bufALbl;

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Accion Boton B", bufB, sizeof(bufB))) tc.buttonBAction = bufB;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::InputText("Etiqueta B", bufBLbl, sizeof(bufBLbl))) tc.buttonBLabel = bufBLbl;

    ImGui::SliderFloat("Opacidad controles", &tc.opacity, 0.2f, 1.0f);

    ImGui::Spacing();
    if (ImGui::Button("Guardar Controles", ImVec2(160.0f, 0.0f))) {
        if (ProjectManager::HasActiveProject()) {
            const std::filesystem::path projDir = ProjectManager::GetActiveProjectDir();
            InputMap::SaveToProject(projDir);
            TouchControlsOverlay::SaveToProject(projDir);
        }
        m_ListeningActionName.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cerrar", ImVec2(100.0f, 0.0f))) {
        m_ListeningActionName.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace Kyumist
