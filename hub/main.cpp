#include "VersionManager.hpp"

#include "core/Application.hpp"
#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "project/ProjectManager.hpp"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class HubTab {
    Projects = 0,
    Installs = 1
};

std::filesystem::path GetDefaultProjectsParentDir() {
#ifdef _WIN32
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        if (*userProfile != '\0') {
            return std::filesystem::path(userProfile) / "KymEngineProjects";
        }
    }
#else
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') {
            return std::filesystem::path(home) / "KymEngineProjects";
        }
    }
#endif
    return Kyumist::PlatformPaths::GetUserDataDir() / "Projects";
}

const char* ChannelLabel(Kyumist::Hub::ReleaseChannel ch) {
    switch (ch) {
    case Kyumist::Hub::ReleaseChannel::Stable:
        return "Estable";
    case Kyumist::Hub::ReleaseChannel::Prerelease:
        return "Pruebas";
    case Kyumist::Hub::ReleaseChannel::Local:
        return "Local";
    }
    return "Estable";
}

class HubApplication final : public Kyumist::Application {
public:
    explicit HubApplication(const Kyumist::ApplicationSpecification& spec)
        : Kyumist::Application(spec) {}

protected:
    void OnInit() override {
        RefreshLocalState();

        const std::string defParent = Kyumist::PlatformPaths::PathToUtf8(GetDefaultProjectsParentDir());
        std::strncpy(m_NewProjectParent.data(), defParent.c_str(), m_NewProjectParent.size() - 1);
        std::strncpy(m_NewProjectName.data(), "MiProyecto", m_NewProjectName.size() - 1);

        if (!GetSpecification().headless) {
            StartFetchReleases();
        }
    }

    void OnUpdate(Kyumist::Timestep) override {
        PollAsyncTasks();
    }

    void OnImGuiRender() override {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        constexpr ImGuiWindowFlags rootFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (!ImGui::Begin("##KymEngineHubRoot", nullptr, rootFlags)) {
            ImGui::End();
            return;
        }

        RenderSidebar();
        ImGui::SameLine();
        RenderMainContent();

        RenderNewProjectModal();
        RenderAddExistingModal();
        RenderMissingVersionModal();
        RenderChangeVersionModal();

        ImGui::End();
    }

private:
    void RefreshLocalState() {
        m_Installed = Kyumist::Hub::VersionManager::ScanInstalledEditors();
        m_Projects = Kyumist::Hub::VersionManager::LoadProjects();
        if (m_NewProjectEditorIdx >= static_cast<int>(m_Installed.size())) {
            m_NewProjectEditorIdx = 0;
        }
    }

    void StartFetchReleases() {
        if (m_IsFetchingReleases) {
            return;
        }
        m_IsFetchingReleases = true;
        m_StatusMessage = "Consultando versiones en GitHub...";
        m_FetchFuture = std::async(std::launch::async, [this]() {
            std::string err;
            auto list = Kyumist::Hub::VersionManager::FetchRemoteReleases(&err);
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            m_PendingFetchError = err;
            return list;
        });
    }

    void StartInstallRelease(const Kyumist::Hub::RemoteRelease& rel, std::filesystem::path openProjectAfter = {}) {
        if (m_IsInstalling) {
            return;
        }
        m_IsInstalling = true;
        m_InstallingVersion = rel.version;
        m_PendingOpenAfterInstall = std::move(openProjectAfter);
        {
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            m_InstallStatusText = "Preparando descarga de " + rel.version + "...";
            m_PendingInstallError.clear();
        }

        m_InstallFuture = std::async(std::launch::async, [this, rel]() {
            std::string err;
            std::string status;
            const bool ok = Kyumist::Hub::VersionManager::InstallRelease(rel, &status, &err);
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            if (!status.empty()) {
                m_InstallStatusText = status;
            }
            m_PendingInstallError = err;
            return ok;
        });
    }

    void StartHubSelfUpdate() {
        if (m_IsUpdatingHub || !m_HasHubUpdate) {
            return;
        }
        m_IsUpdatingHub = true;
        m_StatusMessage = "Descargando actualizacion del Hub v" + m_AvailableHubUpdate.version + "...";
        const auto rel = m_AvailableHubUpdate;
        m_HubUpdateFuture = std::async(std::launch::async, [this, rel]() {
            bool shouldClose = false;
            std::string msg;
            std::string err;
            const bool ok = Kyumist::Hub::VersionManager::DownloadAndApplyHubUpdate(rel, &shouldClose, &msg, &err);
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            m_HubUpdateShouldClose = shouldClose;
            m_PendingHubUpdateMsg = ok ? msg : err;
            return ok;
        });
    }

    void PollAsyncTasks() {
        if (m_IsFetchingReleases && m_FetchFuture.valid() &&
            m_FetchFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_RemoteReleases = m_FetchFuture.get();
            m_IsFetchingReleases = false;

            m_HasHubUpdate = false;
            for (const auto& rel : m_RemoteReleases) {
                if (rel.prerelease || rel.hubInstallerUrl.empty()) {
                    continue;
                }
                if (Kyumist::Hub::VersionManager::CompareVersions(rel.version, KM_ENGINE_VERSION) > 0) {
                    if (!m_HasHubUpdate ||
                        Kyumist::Hub::VersionManager::CompareVersions(rel.version, m_AvailableHubUpdate.version) > 0) {
                        m_AvailableHubUpdate = rel;
                        m_HasHubUpdate = true;
                    }
                }
            }

            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            if (!m_PendingFetchError.empty()) {
                m_StatusMessage = m_PendingFetchError;
                m_PendingFetchError.clear();
            } else {
                m_StatusMessage = "Versiones actualizadas desde GitHub.";
            }
        }

        if (m_IsUpdatingHub && m_HubUpdateFuture.valid() &&
            m_HubUpdateFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const bool ok = m_HubUpdateFuture.get();
            m_IsUpdatingHub = false;
            bool shouldClose = false;
            std::string msg;
            {
                std::lock_guard<std::mutex> lock(m_AsyncMutex);
                shouldClose = m_HubUpdateShouldClose;
                msg = m_PendingHubUpdateMsg;
                m_PendingHubUpdateMsg.clear();
            }
            if (!msg.empty()) {
                m_StatusMessage = msg;
            } else if (!ok) {
                m_StatusMessage = "No se pudo actualizar el Hub.";
            }
            if (ok && shouldClose) {
                RequestClose();
            }
        }

        if (m_IsInstalling && m_InstallFuture.valid() &&
            m_InstallFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const bool ok = m_InstallFuture.get();
            m_IsInstalling = false;
            RefreshLocalState();

            std::string errCopy;
            {
                std::lock_guard<std::mutex> lock(m_AsyncMutex);
                errCopy = m_PendingInstallError;
                m_PendingInstallError.clear();
            }

            if (ok) {
                m_StatusMessage = "KymEngine " + m_InstallingVersion + " instalado correctamente.";
                if (!m_PendingOpenAfterInstall.empty()) {
                    if (const auto* ed = Kyumist::Hub::VersionManager::FindInstalledVersion(m_Installed, m_InstallingVersion)) {
                        std::string launchErr;
                        if (!Kyumist::Hub::VersionManager::LaunchEditor(ed->executablePath, m_PendingOpenAfterInstall, &launchErr)) {
                            m_StatusMessage = launchErr;
                        }
                    }
                    m_PendingOpenAfterInstall.clear();
                }
            } else {
                m_StatusMessage = errCopy.empty() ? "Fallo al instalar la version." : errCopy;
                m_PendingOpenAfterInstall.clear();
            }
            m_InstallingVersion.clear();
        }
    }

    void TryOpenProject(const Kyumist::Hub::HubProjectEntry& proj) {
        if (!proj.fileExists) {
            m_StatusMessage = "No existe el archivo: " + Kyumist::PlatformPaths::PathToUtf8(proj.projectFile);
            return;
        }
        const auto* ed = Kyumist::Hub::VersionManager::FindInstalledVersion(m_Installed, proj.engineVersion);
        if (ed) {
            std::string err;
            if (Kyumist::Hub::VersionManager::LaunchEditor(ed->executablePath, proj.projectFile, &err)) {
                m_StatusMessage = "Abriendo '" + proj.name + "' con KymEngine " + ed->version + "...";
            } else {
                m_StatusMessage = err;
            }
            return;
        }

        m_MissingModalProject = proj;
        m_OpenMissingVersionModal = true;
    }

    void RenderSidebar() {
        ImGui::BeginChild("##HubSidebar", ImVec2(220.0f, 0.0f), ImGuiChildFlags_Border);

        ImGui::TextUnformatted("KymEngine Hub");
        ImGui::TextDisabled("Launcher v%s", KM_ENGINE_VERSION);
        ImGui::Separator();
        ImGui::Spacing();

        auto navBtn = [&](const char* label, HubTab tab) {
            const bool selected = (m_ActiveTab == tab);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.72f, 1.0f));
            }
            if (ImGui::Button(label, ImVec2(-1.0f, 34.0f))) {
                m_ActiveTab = tab;
            }
            if (selected) {
                ImGui::PopStyleColor();
            }
        };

        navBtn("Proyectos", HubTab::Projects);
        navBtn("Instalaciones", HubTab::Installs);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Editores instalados: %zu", m_Installed.size());
        ImGui::TextDisabled("Proyectos: %zu", m_Projects.size());

        if (m_IsInstalling) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "Instalando %s...", m_InstallingVersion.c_str());
            ImGui::TextWrapped("Descargando y extrayendo paquete desde GitHub...");
        }

        if (m_HasHubUpdate) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.92f, 0.50f, 1.0f), "Hub v%s disponible", m_AvailableHubUpdate.version.c_str());
            ImGui::BeginDisabled(m_IsUpdatingHub || m_IsInstalling);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.56f, 0.34f, 1.0f));
            if (ImGui::Button(m_IsUpdatingHub ? "Actualizando..." : "Actualizar Hub", ImVec2(-1.0f, 32.0f))) {
                StartHubSelfUpdate();
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
        }

        ImGui::EndChild();
    }

    void RenderMainContent() {
        ImGui::BeginChild("##HubContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

        if (m_HasHubUpdate) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.92f, 0.50f, 1.0f));
            ImGui::Text("Nueva version de KymEngine Hub disponible: v%s (actual: v%s)",
                        m_AvailableHubUpdate.version.c_str(), KM_ENGINE_VERSION);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_IsUpdatingHub || m_IsInstalling);
            if (ImGui::SmallButton(m_IsUpdatingHub ? "Descargando..." : "Actualizar Launcher")) {
                StartHubSelfUpdate();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
        }

        if (!m_StatusMessage.empty()) {
            ImGui::TextColored(ImVec4(0.75f, 0.88f, 1.0f, 1.0f), "%s", m_StatusMessage.c_str());
            ImGui::Separator();
        }

        if (m_ActiveTab == HubTab::Projects) {
            RenderProjectsTab();
        } else {
            RenderInstallsTab();
        }

        ImGui::EndChild();
    }

    void RenderProjectsTab() {
        ImGui::TextUnformatted("Tus Proyectos");
        ImGui::SameLine();
        if (ImGui::Button("+ Nuevo Proyecto")) {
            m_OpenNewProjectModal = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Anadir existente")) {
            m_AddExistingPath.fill('\0');
            m_OpenAddExistingModal = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Refrescar")) {
            RefreshLocalState();
        }
        ImGui::Separator();

        if (m_Projects.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No hay proyectos registrados todavia.");
            ImGui::TextDisabled("Pulsa '+ Nuevo Proyecto' o 'Anadir existente' para empezar.");
            return;
        }

        if (ImGui::BeginTable("##ProjectsTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Proyecto", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("Version del Motor", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Estado", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Acciones", ImGuiTableColumnFlags_WidthFixed, 230.0f);
            ImGui::TableHeadersRow();

            std::filesystem::path pendingRemove;
            for (size_t i = 0; i < m_Projects.size(); ++i) {
                const auto& proj = m_Projects[i];
                const bool hasEditor = Kyumist::Hub::VersionManager::FindInstalledVersion(m_Installed, proj.engineVersion) != nullptr;

                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(proj.name.c_str());
                ImGui::TextDisabled("%s", Kyumist::PlatformPaths::PathToUtf8(proj.projectFile.parent_path()).c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("v%s", proj.engineVersion.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Cambiar")) {
                    m_ChangeVersionProject = proj;
                    m_ChangeVersionIdx = 0;
                    m_OpenChangeVersionModal = true;
                }

                ImGui::TableSetColumnIndex(2);
                if (!proj.fileExists) {
                    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "No encontrado");
                } else if (hasEditor) {
                    ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.45f, 1.0f), "Listo");
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Falta editor");
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(!proj.fileExists || m_IsInstalling);
                if (ImGui::Button("Abrir", ImVec2(90.0f, 0.0f))) {
                    TryOpenProject(proj);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Quitar", ImVec2(70.0f, 0.0f))) {
                    pendingRemove = proj.projectFile;
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (!pendingRemove.empty()) {
                Kyumist::Hub::VersionManager::RemoveProject(pendingRemove);
                RefreshLocalState();
            }
        }
    }

    void RenderInstallsTab() {
        ImGui::TextUnformatted("Versiones del Editor Instaladas");
        ImGui::SameLine();
        if (ImGui::Button("Refrescar")) {
            RefreshLocalState();
        }
        ImGui::TextDisabled("Carpeta: %s", Kyumist::PlatformPaths::PathToUtf8(Kyumist::Hub::VersionManager::GetEditorsRootDir()).c_str());
        ImGui::Separator();

        if (m_Installed.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "No tienes ninguna version instalada.");
        } else if (ImGui::BeginTable("##InstalledTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Ruta", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Accion", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();

            std::string pendingUninstall;
            for (size_t i = 0; i < m_Installed.size(); ++i) {
                const auto& ed = m_Installed[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("v%s", ed.version.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ed.isBundled ? "Incluida" : ChannelLabel(ed.channel));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", Kyumist::PlatformPaths::PathToUtf8(ed.executablePath).c_str());
                ImGui::TableSetColumnIndex(3);
                if (ed.isBundled) {
                    ImGui::TextDisabled("Base");
                } else {
                    ImGui::BeginDisabled(m_IsInstalling);
                    if (ImGui::Button("Desinstalar")) {
                        pendingUninstall = ed.version;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();

            if (!pendingUninstall.empty()) {
                std::string err;
                if (Kyumist::Hub::VersionManager::UninstallEditor(pendingUninstall, &err)) {
                    m_StatusMessage = "Version " + pendingUninstall + " desinstalada.";
                } else {
                    m_StatusMessage = err;
                }
                RefreshLocalState();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Descargar desde GitHub Releases");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_IsFetchingReleases);
        if (ImGui::Button(m_IsFetchingReleases ? "Consultando..." : "Buscar actualizaciones")) {
            StartFetchReleases();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Mostrar Pruebas (Beta/Alpha)", &m_ShowPrereleases);

        if (m_RemoteReleases.empty()) {
            ImGui::TextDisabled("No hay releases cargadas todavia.");
            return;
        }

        if (ImGui::BeginTable("##RemoteTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Fecha", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Paquete", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Accion", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_RemoteReleases.size(); ++i) {
                const auto& rel = m_RemoteReleases[i];
                if (rel.prerelease && !m_ShowPrereleases) {
                    continue;
                }

                const bool alreadyInstalled =
                    Kyumist::Hub::VersionManager::FindInstalledVersion(m_Installed, rel.version) != nullptr;
                const double sizeMb = static_cast<double>(rel.assetSizeBytes) / (1024.0 * 1024.0);

                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("v%s", rel.version.c_str());
                ImGui::TableSetColumnIndex(1);
                if (rel.prerelease) {
                    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Pruebas");
                } else {
                    ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.45f, 1.0f), "Estable");
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", rel.publishedAt.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s (%.1f MB)", rel.assetName.c_str(), sizeMb);
                ImGui::TableSetColumnIndex(4);
                if (alreadyInstalled) {
                    ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.45f, 1.0f), "Instalada");
                } else {
                    ImGui::BeginDisabled(m_IsInstalling);
                    if (ImGui::Button("Instalar", ImVec2(100.0f, 0.0f))) {
                        StartInstallRelease(rel);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void RenderNewProjectModal() {
        if (m_OpenNewProjectModal) {
            ImGui::OpenPopup("Nuevo Proyecto");
            m_OpenNewProjectModal = false;
        }
        if (!ImGui::BeginPopupModal("Nuevo Proyecto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::InputText("Nombre", m_NewProjectName.data(), m_NewProjectName.size());
        ImGui::InputText("Carpeta padre", m_NewProjectParent.data(), m_NewProjectParent.size());

        const char* templates[] = {"Blank 3D", "Platformer 2D", "Shooter 3D", "Hybrid Sanctuary"};
        ImGui::Combo("Plantilla", &m_NewProjectTemplateIdx, templates, 4);

        if (m_Installed.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Instala primero una version del editor en 'Instalaciones'.");
        } else {
            const std::string preview = "v" + m_Installed[m_NewProjectEditorIdx].version;
            if (ImGui::BeginCombo("Version del motor", preview.c_str())) {
                for (int i = 0; i < static_cast<int>(m_Installed.size()); ++i) {
                    const bool sel = (i == m_NewProjectEditorIdx);
                    const std::string label = "v" + m_Installed[i].version + " (" +
                        (m_Installed[i].isBundled ? "Incluida" : ChannelLabel(m_Installed[i].channel)) + ")";
                    if (ImGui::Selectable(label.c_str(), sel)) {
                        m_NewProjectEditorIdx = i;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(m_Installed.empty());
        if (ImGui::Button("Crear y Abrir", ImVec2(130.0f, 0.0f))) {
            std::string err;
            const std::filesystem::path parentDir = Kyumist::PlatformPaths::Utf8ToPath(m_NewProjectParent.data());
            const std::string projName(m_NewProjectName.data());
            if (Kyumist::ProjectManager::CreateNewProject(parentDir, projName, m_NewProjectTemplateIdx, &err)) {
                const std::filesystem::path projFile = parentDir / Kyumist::PlatformPaths::Utf8ToPath(projName) / "project.kmproj";
                const auto& chosenEd = m_Installed[m_NewProjectEditorIdx];
                Kyumist::Hub::VersionManager::UpdateProjectEngineVersion(projFile, chosenEd.version);
                Kyumist::Hub::VersionManager::AddProject(projFile, &err);
                RefreshLocalState();
                std::string launchErr;
                if (!Kyumist::Hub::VersionManager::LaunchEditor(chosenEd.executablePath, projFile, &launchErr)) {
                    m_StatusMessage = launchErr;
                } else {
                    m_StatusMessage = err.empty() ? ("Proyecto '" + projName + "' creado.") : err;
                }
                ImGui::CloseCurrentPopup();
            } else {
                m_StatusMessage = err;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void RenderAddExistingModal() {
        if (m_OpenAddExistingModal) {
            ImGui::OpenPopup("Anadir Proyecto Existente");
            m_OpenAddExistingModal = false;
        }
        if (!ImGui::BeginPopupModal("Anadir Proyecto Existente", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextUnformatted("Ruta a la carpeta del proyecto o al archivo project.kmproj:");
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("##ExistingPath", m_AddExistingPath.data(), m_AddExistingPath.size());

        if (ImGui::Button("Anadir", ImVec2(110.0f, 0.0f))) {
            std::string err;
            if (Kyumist::Hub::VersionManager::AddProject(Kyumist::PlatformPaths::Utf8ToPath(m_AddExistingPath.data()), &err)) {
                RefreshLocalState();
                m_StatusMessage = "Proyecto anadido al Hub.";
                ImGui::CloseCurrentPopup();
            } else {
                m_StatusMessage = err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void RenderMissingVersionModal() {
        if (m_OpenMissingVersionModal) {
            ImGui::OpenPopup("Version no instalada");
            m_OpenMissingVersionModal = false;
        }
        if (!ImGui::BeginPopupModal("Version no instalada", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::Text("El proyecto '%s' usa KymEngine v%s.",
                    m_MissingModalProject.name.c_str(),
                    m_MissingModalProject.engineVersion.c_str());
        ImGui::TextUnformatted("Esa version no esta instalada actualmente en este equipo.");
        ImGui::Spacing();

        const Kyumist::Hub::RemoteRelease* matchingRemote = nullptr;
        for (const auto& rel : m_RemoteReleases) {
            if (rel.version == m_MissingModalProject.engineVersion) {
                matchingRemote = &rel;
                break;
            }
        }

        if (matchingRemote) {
            const std::string btnLabel = "Instalar v" + matchingRemote->version + " y abrir";
            if (ImGui::Button(btnLabel.c_str())) {
                StartInstallRelease(*matchingRemote, m_MissingModalProject.projectFile);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }

        if (!m_Installed.empty() && ImGui::Button("Cambiar a una version instalada")) {
            m_ChangeVersionProject = m_MissingModalProject;
            m_ChangeVersionIdx = 0;
            m_OpenChangeVersionModal = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cerrar")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void RenderChangeVersionModal() {
        if (m_OpenChangeVersionModal) {
            ImGui::OpenPopup("Cambiar Version del Proyecto");
            m_OpenChangeVersionModal = false;
        }
        if (!ImGui::BeginPopupModal("Cambiar Version del Proyecto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::Text("Proyecto: %s", m_ChangeVersionProject.name.c_str());
        ImGui::Text("Version actual: v%s", m_ChangeVersionProject.engineVersion.c_str());
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                           "Se guardara una copia de seguridad en project.kmproj.bak antes de cambiar.");
        ImGui::Spacing();

        if (m_Installed.empty()) {
            ImGui::TextDisabled("No hay versiones instaladas disponibles.");
        } else {
            const std::string preview = "v" + m_Installed[m_ChangeVersionIdx].version;
            if (ImGui::BeginCombo("Nueva version", preview.c_str())) {
                for (int i = 0; i < static_cast<int>(m_Installed.size()); ++i) {
                    const bool sel = (i == m_ChangeVersionIdx);
                    const std::string label = "v" + m_Installed[i].version;
                    if (ImGui::Selectable(label.c_str(), sel)) {
                        m_ChangeVersionIdx = i;
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Aplicar cambio")) {
                std::string err;
                const std::string targetVer = m_Installed[m_ChangeVersionIdx].version;
                if (Kyumist::Hub::VersionManager::UpdateProjectEngineVersion(
                        m_ChangeVersionProject.projectFile, targetVer, &err)) {
                    RefreshLocalState();
                    m_StatusMessage = "Proyecto actualizado a v" + targetVer + " (backup creado).";
                    ImGui::CloseCurrentPopup();
                } else {
                    m_StatusMessage = err;
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancelar")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    HubTab m_ActiveTab = HubTab::Projects;
    std::vector<Kyumist::Hub::InstalledEditor> m_Installed;
    std::vector<Kyumist::Hub::RemoteRelease> m_RemoteReleases;
    std::vector<Kyumist::Hub::HubProjectEntry> m_Projects;

    bool m_ShowPrereleases = true;
    std::string m_StatusMessage;

    std::atomic<bool> m_IsFetchingReleases{false};
    std::future<std::vector<Kyumist::Hub::RemoteRelease>> m_FetchFuture;

    std::atomic<bool> m_IsInstalling{false};
    std::future<bool> m_InstallFuture;
    std::string m_InstallingVersion;
    std::filesystem::path m_PendingOpenAfterInstall;

    bool m_HasHubUpdate = false;
    Kyumist::Hub::RemoteRelease m_AvailableHubUpdate;
    std::atomic<bool> m_IsUpdatingHub{false};
    std::future<bool> m_HubUpdateFuture;
    bool m_HubUpdateShouldClose = false;
    std::string m_PendingHubUpdateMsg;

    std::mutex m_AsyncMutex;
    std::string m_PendingFetchError;
    std::string m_PendingInstallError;
    std::string m_InstallStatusText;

    bool m_OpenNewProjectModal = false;
    std::array<char, 128> m_NewProjectName{};
    std::array<char, 512> m_NewProjectParent{};
    int m_NewProjectTemplateIdx = 0;
    int m_NewProjectEditorIdx = 0;

    bool m_OpenAddExistingModal = false;
    std::array<char, 512> m_AddExistingPath{};

    bool m_OpenMissingVersionModal = false;
    Kyumist::Hub::HubProjectEntry m_MissingModalProject;

    bool m_OpenChangeVersionModal = false;
    Kyumist::Hub::HubProjectEntry m_ChangeVersionProject;
    int m_ChangeVersionIdx = 0;
};

} // namespace

int main(int argc, char** argv) {
    Kyumist::ApplicationSpecification spec;
    spec.name = "KymEngine Hub";
    spec.width = 1040;
    spec.height = 660;

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
        }
    }

    try {
        HubApplication app(spec);
        app.Run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

