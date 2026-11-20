#pragma once

#include "core/Timestep.hpp"
#include "panels/ScriptEditorPanel.hpp"

#include <entt/entt.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace Kyumist {

class EditorCamera;
class Scene;
class VulkanRenderer;

class EditorPanels {
public:
    EditorPanels();

    void SyncProjectState(Scene& scene);

    void Render(
        VulkanRenderer* renderer,
        Scene& scene,
        EditorCamera& camera,
        Timestep ts,
        uint32_t frameCount,
        bool& shouldClose
    );

    bool IsViewportHovered() const { return m_ViewportHovered; }
    bool IsPlaying() const { return m_IsPlaying; }
    bool IsPlayingInProcess() const {
        return m_IsPlaying && m_RuntimePid <= 0 && m_RuntimeProcessHandle == 0;
    }
    void SetHeadlessMode(bool headless) { m_HeadlessMode = headless; }
    void StartPlayMode(Scene& scene);
    void StopPlayMode(Scene& scene);

    float ConsumeScrollDelta() {
        const float delta = m_ScrollDelta;
        m_ScrollDelta = 0.0f;
        return delta;
    }

private:
    enum class FolderPickerMode {
        None = 0,
        CreateParentDir,
        OpenExistingProject
    };

    struct SceneHistoryEntry {
        std::string sceneJson;
        std::string selectedTag;
    };

    void ResetSceneHistory(const Scene& scene);
    void CaptureSceneUndoStateIfChanged(const Scene& scene);
    bool UndoScene(Scene& scene);
    bool RedoScene(Scene& scene);
    std::string GetSelectedEntityTag(const Scene& scene) const;

    entt::entity PickEntityInViewport(
        const Scene& scene,
        const EditorCamera& camera,
        const ImVec2& mousePos,
        const ImVec2& imageMin,
        const ImVec2& imageSize
    ) const;

    void RenderProjectManager(Scene& scene, bool& shouldClose);
    void RenderDirectoryBrowserModal(Scene& scene);

    void RenderMenuBar(Scene& scene, bool& shouldClose);
    void RenderAssetImportModal(VulkanRenderer* renderer, Scene& scene);
    void RenderHierarchy(Scene& scene);
    void RenderViewport(VulkanRenderer* renderer, Scene& scene, EditorCamera& camera, Timestep ts);
    void RenderInspector(Scene& scene);
    void RenderStats(VulkanRenderer* renderer, Timestep ts, uint32_t frameCount);
    void RenderConsole();
    void RenderContentBrowser(Scene& scene);

    void SpawnModelEntity(Scene& scene, const std::filesystem::path& modelFile);
    void ApplyMaterialToEntity(Scene& scene, entt::entity entity, const std::filesystem::path& materialFile);

    void RenderDirectoryTree(const std::filesystem::path& dir);
    void DrawVectorAssetIcon(
        const std::filesystem::path& entryPath,
        bool isDir,
        ImVec2 minPos,
        ImVec2 maxPos
    ) const;

    entt::entity DuplicateEntity(Scene& scene, entt::entity src);
    void RenderExportProjectModal();
    void RenderInputMapModal();

    ScriptEditorPanel m_ScriptEditor;

    bool m_LayoutInitialized = false;
    bool m_ShowHierarchy = true;
    bool m_ShowViewport = true;
    bool m_ShowScriptEditor = true;
    bool m_ShowInspector = true;
    bool m_ShowStats = true;
    bool m_ShowConsole = true;
    bool m_ShowContentBrowser = true;
    bool m_FocusScriptEditorNext = false;
    bool m_FocusViewportNext = false;
    bool m_AutoScroll = true;
    bool m_ShowTrace = true;
    bool m_ShowInfo = true;
    bool m_ShowWarn = true;
    bool m_ShowError = true;
    bool m_ViewportHovered = false;
    bool m_IsPlaying = false;
    bool m_HeadlessMode = false;
    int m_RuntimePid = -1;
    std::uintptr_t m_RuntimeProcessHandle = 0;
    float m_ScrollDelta = 0.0f;
    float m_ContentCardSize = 94.0f;
    char m_FilterBuffer[128]{};
    char m_HierarchyFilter[128]{};

    char m_ProjectSearch[128]{};
    char m_NewProjectName[128] = "MiProyectoKyumist";
    char m_NewProjectParentDir[512]{};
    int m_NewProjectTemplate = 0;
    std::string m_ProjectError;

    FolderPickerMode m_FolderPickerMode = FolderPickerMode::None;
    bool m_OpenFolderPickerPopup = false;
    std::filesystem::path m_BrowserCurrentDir;
    char m_NewSubfolderName[128] = "NuevoJuego";
    char m_FsNewName[64] = "NuevoElemento";

    bool m_OpenAssetImportPopup = false;
    std::filesystem::path m_AssetImportDir;
    std::filesystem::path m_SelectedImportFile;
    int m_AssetImportFilter = 0;
    bool m_ImportAutoSubfolder = true;
    bool m_ImportSpawnInScene = true;
    std::string m_AssetImportError;

    entt::entity m_SelectedEntity = entt::null;
    ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_GizmoMode = ImGuizmo::LOCAL;

    std::filesystem::path m_AssetsRoot;
    std::filesystem::path m_CurrentAssetDir;
    std::filesystem::path m_ActiveSceneFile;

    std::vector<SceneHistoryEntry> m_SceneUndoStack;
    std::vector<SceneHistoryEntry> m_SceneRedoStack;
    std::string m_LastCommittedSceneState;
    std::string m_LastCommittedSelectedTag;

    bool m_OpenExportModal = false;
    int m_ExportPlatformIdx =
#ifdef _WIN32
        0;
#else
        1;
#endif
    char m_ExportExeName[128] = "KymEngineGame";
    char m_ExportOutputDir[512]{};
    bool m_ExportCreateArchive = true;
    bool m_IsExporting = false;
    std::future<bool> m_ExportFuture;
    std::string m_ExportStatus;
    std::string m_ExportError;

    bool m_OpenInputMapModal = false;
    char m_NewActionName[64] = "shoot";
    std::string m_ListeningActionName;
};

} // namespace Kyumist
