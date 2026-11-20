#include "ui/UISystem.hpp"

#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "project/ProjectManager.hpp"
#include "renderer/VulkanRenderer.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Kyumist {

namespace {

std::filesystem::path ResolveUIFilePath(const std::string& source) {
    std::error_code ec;
    std::filesystem::path p = PlatformPaths::Utf8ToPath(source);
    if (p.is_absolute() && std::filesystem::exists(p, ec)) {
        return p;
    }
    if (ProjectManager::HasActiveProject()) {
        const std::filesystem::path inProj = ProjectManager::GetActiveProjectDir() / p;
        if (std::filesystem::exists(inProj, ec)) {
            return inProj;
        }
    }
    const std::filesystem::path inAssets = PlatformPaths::ResolveAssetsDir() / p;
    if (std::filesystem::exists(inAssets, ec)) {
        return inAssets;
    }
    return p;
}

class KyumistRmlSystem final : public Rml::SystemInterface {
public:
    KyumistRmlSystem() : m_Start(std::chrono::steady_clock::now()) {}

    double GetElapsedTime() override {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - m_Start).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            KM_CORE_ERROR("[RmlUi] {}", message);
            break;
        case Rml::Log::LT_WARNING:
            KM_CORE_WARN("[RmlUi] {}", message);
            break;
        default:
            break;
        }
        return true;
    }

private:
    std::chrono::steady_clock::time_point m_Start;
};

class KyumistRmlFile final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        const std::filesystem::path resolved = ResolveUIFilePath(path);
        auto* stream = new std::ifstream(resolved, std::ios::binary);
        if (!stream->is_open()) {
            delete stream;
            return 0;
        }
        return reinterpret_cast<Rml::FileHandle>(stream);
    }

    void Close(Rml::FileHandle file) override {
        auto* stream = reinterpret_cast<std::ifstream*>(file);
        delete stream;
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        auto* stream = reinterpret_cast<std::ifstream*>(file);
        if (!stream) {
            return 0;
        }
        stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
        return static_cast<size_t>(stream->gcount());
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        auto* stream = reinterpret_cast<std::ifstream*>(file);
        if (!stream) {
            return false;
        }
        std::ios::seekdir dir = std::ios::beg;
        if (origin == SEEK_CUR) {
            dir = std::ios::cur;
        } else if (origin == SEEK_END) {
            dir = std::ios::end;
        }
        stream->clear();
        stream->seekg(offset, dir);
        return !stream->fail();
    }

    size_t Tell(Rml::FileHandle file) override {
        auto* stream = reinterpret_cast<std::ifstream*>(file);
        if (!stream) {
            return 0;
        }
        return static_cast<size_t>(stream->tellg());
    }

    size_t Length(Rml::FileHandle file) override {
        auto* stream = reinterpret_cast<std::ifstream*>(file);
        if (!stream) {
            return 0;
        }
        stream->clear();
        const auto cur = stream->tellg();
        stream->seekg(0, std::ios::end);
        const auto len = stream->tellg();
        stream->clear();
        stream->seekg(cur, std::ios::beg);
        return len >= 0 ? static_cast<size_t>(len) : 0;
    }
};

struct CompiledRmlGeometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};

class KyumistRmlRender final : public Rml::RenderInterface {
public:
    explicit KyumistRmlRender(VulkanRenderer* renderer) : m_Renderer(renderer) {}

    void SetDrawTarget(ImDrawList* drawList, ImVec2 origin, ImVec2 size) {
        m_DrawList = drawList;
        m_Origin = origin;
        m_Size = size;
        m_ScissorEnabled = false;
    }

    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices
    ) override {
        auto* geo = new CompiledRmlGeometry();
        geo->vertices.assign(vertices.begin(), vertices.end());
        geo->indices.assign(indices.begin(), indices.end());
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geo);
    }

    void RenderGeometry(
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture
    ) override {
        if (!m_DrawList || geometry == 0) {
            return;
        }
        const auto* geo = reinterpret_cast<const CompiledRmlGeometry*>(geometry);
        if (geo->vertices.empty() || geo->indices.empty()) {
            return;
        }

        const bool hasTexture = (texture != 0);
        const ImTextureID texId = hasTexture
            ? static_cast<ImTextureID>(texture)
            : ImGui::GetIO().Fonts->TexID;
        const ImVec2 whiteUv = ImGui::GetIO().Fonts->TexUvWhitePixel;

        if (m_ScissorEnabled) {
            const ImVec2 clipMin(
                m_Origin.x + static_cast<float>(m_ScissorRect.Left()),
                m_Origin.y + static_cast<float>(m_ScissorRect.Top())
            );
            const ImVec2 clipMax(
                m_Origin.x + static_cast<float>(m_ScissorRect.Right()),
                m_Origin.y + static_cast<float>(m_ScissorRect.Bottom())
            );
            m_DrawList->PushClipRect(clipMin, clipMax, true);
        }

        m_DrawList->PushTextureID(texId);
        const int vtxCount = static_cast<int>(geo->vertices.size());
        const int idxCount = static_cast<int>(geo->indices.size());
        m_DrawList->PrimReserve(idxCount, vtxCount);
        const ImDrawIdx baseIdx = static_cast<ImDrawIdx>(m_DrawList->_VtxCurrentIdx);

        for (const auto& v : geo->vertices) {
            const ImVec2 pos(
                m_Origin.x + translation.x + v.position.x,
                m_Origin.y + translation.y + v.position.y
            );
            const ImVec2 uv = hasTexture ? ImVec2(v.tex_coord.x, v.tex_coord.y) : whiteUv;
            const ImU32 col = IM_COL32(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
            m_DrawList->PrimWriteVtx(pos, uv, col);
        }
        for (int idx : geo->indices) {
            m_DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(baseIdx + idx));
        }

        m_DrawList->PopTextureID();

        if (m_ScissorEnabled) {
            m_DrawList->PopClipRect();
        }
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
        auto* geo = reinterpret_cast<CompiledRmlGeometry*>(geometry);
        delete geo;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override {
        if (!m_Renderer) {
            return 0;
        }
        const std::filesystem::path resolved = ResolveUIFilePath(source);
        uint32_t w = 0;
        uint32_t h = 0;
        const uint64_t handle = m_Renderer->LoadUITextureFile(PlatformPaths::PathToUtf8(resolved), w, h);
        textureDimensions.x = static_cast<int>(w);
        textureDimensions.y = static_cast<int>(h);
        return static_cast<Rml::TextureHandle>(handle);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override {
        if (!m_Renderer || sourceDimensions.x <= 0 || sourceDimensions.y <= 0) {
            return 0;
        }
        const uint64_t handle = m_Renderer->CreateUITextureRGBA(
            static_cast<uint32_t>(sourceDimensions.x),
            static_cast<uint32_t>(sourceDimensions.y),
            source.data()
        );
        return static_cast<Rml::TextureHandle>(handle);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (m_Renderer && texture != 0) {
            m_Renderer->DestroyUITexture(static_cast<uint64_t>(texture));
        }
    }

    void EnableScissorRegion(bool enable) override {
        m_ScissorEnabled = enable;
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        m_ScissorRect = region;
    }

private:
    VulkanRenderer* m_Renderer = nullptr;
    ImDrawList* m_DrawList = nullptr;
    ImVec2 m_Origin{0.0f, 0.0f};
    ImVec2 m_Size{1280.0f, 720.0f};
    bool m_ScissorEnabled = false;
    Rml::Rectanglei m_ScissorRect;
};

class KyumistRmlClickListener final : public Rml::EventListener {
public:
    explicit KyumistRmlClickListener(UISystem::UIEventCallback* cb) : m_Callback(cb) {}

    void ProcessEvent(Rml::Event& event) override {
        if (!m_Callback || !*m_Callback) {
            return;
        }
        Rml::Element* target = event.GetTargetElement();
        while (target) {
            const Rml::String id = target->GetId();
            if (!id.empty()) {
                (*m_Callback)(id, event.GetType());
                return;
            }
            target = target->GetParentNode();
        }
    }

private:
    UISystem::UIEventCallback* m_Callback = nullptr;
};

struct UISystemState {
    bool initialized = false;
    std::unique_ptr<KyumistRmlSystem> sysIface;
    std::unique_ptr<KyumistRmlFile> fileIface;
    std::unique_ptr<KyumistRmlRender> renderIface;
    Rml::Context* context = nullptr;
    UISystem::UIEventCallback eventCallback;
    std::unique_ptr<KyumistRmlClickListener> clickListener;
    std::unordered_map<std::string, Rml::ElementDocument*> documents;
    int lastWidth = 1280;
    int lastHeight = 720;
    bool prevMouseDown = false;
};

UISystemState s_UI;

Rml::Element* FindElementByIdInDocs(const std::string& id) {
    for (auto& [path, doc] : s_UI.documents) {
        if (!doc) {
            continue;
        }
        if (Rml::Element* el = doc->GetElementById(id)) {
            return el;
        }
    }
    return nullptr;
}

} // namespace

void UISystem::Init(VulkanRenderer* renderer) {
    if (s_UI.initialized) {
        return;
    }

    s_UI.sysIface = std::make_unique<KyumistRmlSystem>();
    s_UI.fileIface = std::make_unique<KyumistRmlFile>();
    s_UI.renderIface = std::make_unique<KyumistRmlRender>(renderer);
    s_UI.clickListener = std::make_unique<KyumistRmlClickListener>(&s_UI.eventCallback);

    Rml::SetSystemInterface(s_UI.sysIface.get());
    Rml::SetFileInterface(s_UI.fileIface.get());
    Rml::SetRenderInterface(s_UI.renderIface.get());

    if (!Rml::Initialise()) {
        KM_CORE_ERROR("Failed to initialize RmlUi.");
        return;
    }

    const std::filesystem::path fontsDir = PlatformPaths::ResolveAssetsDir() / "fonts";
    std::error_code ec;
    const std::filesystem::path roboto = fontsDir / "Roboto-Medium.ttf";
    const std::filesystem::path cousine = fontsDir / "Cousine-Regular.ttf";
    if (std::filesystem::exists(roboto, ec)) {
        Rml::LoadFontFace(PlatformPaths::PathToUtf8(roboto), true);
    }
    if (std::filesystem::exists(cousine, ec)) {
        Rml::LoadFontFace(PlatformPaths::PathToUtf8(cousine), false);
    }

    s_UI.context = Rml::CreateContext("main", Rml::Vector2i(1280, 720));
    s_UI.initialized = (s_UI.context != nullptr);
    if (s_UI.initialized) {
        KM_CORE_INFO("RmlUi HTML/CSS UI System initialized.");
    }
}

void UISystem::Shutdown() {
    if (!s_UI.initialized) {
        return;
    }
    UnloadAllDocuments();
    if (s_UI.context) {
        Rml::RemoveContext("main");
        s_UI.context = nullptr;
    }
    Rml::Shutdown();
    s_UI.clickListener.reset();
    s_UI.renderIface.reset();
    s_UI.fileIface.reset();
    s_UI.sysIface.reset();
    s_UI.initialized = false;
}

bool UISystem::IsInitialized() {
    return s_UI.initialized;
}

void UISystem::SetEventCallback(UIEventCallback cb) {
    s_UI.eventCallback = std::move(cb);
}

bool UISystem::LoadDocument(const std::string& relativePath) {
    if (!s_UI.initialized || !s_UI.context || relativePath.empty()) {
        return false;
    }
    UnloadDocument(relativePath);

    Rml::ElementDocument* doc = s_UI.context->LoadDocument(relativePath);
    if (!doc) {
        KM_CORE_WARN("RmlUi failed to load HTML document '{}'.", relativePath);
        return false;
    }
    doc->AddEventListener(Rml::EventId::Click, s_UI.clickListener.get());
    doc->Show();
    s_UI.documents[relativePath] = doc;
    return true;
}

void UISystem::UnloadDocument(const std::string& relativePath) {
    const auto it = s_UI.documents.find(relativePath);
    if (it == s_UI.documents.end()) {
        return;
    }
    if (it->second) {
        it->second->RemoveEventListener(Rml::EventId::Click, s_UI.clickListener.get());
        it->second->Close();
    }
    s_UI.documents.erase(it);
}

void UISystem::UnloadAllDocuments() {
    for (auto& [path, doc] : s_UI.documents) {
        if (doc) {
            doc->RemoveEventListener(Rml::EventId::Click, s_UI.clickListener.get());
            doc->Close();
        }
    }
    s_UI.documents.clear();
}

bool UISystem::SetText(const std::string& elementId, const std::string& text) {
    Rml::Element* el = FindElementByIdInDocs(elementId);
    if (!el) {
        return false;
    }
    el->SetInnerRML(text);
    return true;
}

bool UISystem::SetVisible(const std::string& elementId, bool visible) {
    Rml::Element* el = FindElementByIdInDocs(elementId);
    if (!el) {
        return false;
    }
    el->SetProperty("display", visible ? "block" : "none");
    return true;
}

bool UISystem::SetClass(const std::string& elementId, const std::string& className, bool active) {
    Rml::Element* el = FindElementByIdInDocs(elementId);
    if (!el) {
        return false;
    }
    el->SetClass(className, active);
    return true;
}

void UISystem::UpdateAndRender(
    ImDrawList* drawList,
    ImVec2 viewportMin,
    ImVec2 viewportSize,
    bool allowInput
) {
    if (!s_UI.initialized || !s_UI.context) {
        return;
    }

    const int w = std::max(1, static_cast<int>(viewportSize.x));
    const int h = std::max(1, static_cast<int>(viewportSize.y));
    if (w != s_UI.lastWidth || h != s_UI.lastHeight) {
        s_UI.lastWidth = w;
        s_UI.lastHeight = h;
        s_UI.context->SetDimensions(Rml::Vector2i(w, h));
    }

    if (allowInput) {
        const ImGuiIO& io = ImGui::GetIO();
        const int mx = static_cast<int>(io.MousePos.x - viewportMin.x);
        const int my = static_cast<int>(io.MousePos.y - viewportMin.y);
        s_UI.context->ProcessMouseMove(mx, my, 0);

        const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (mouseDown && !s_UI.prevMouseDown) {
            s_UI.context->ProcessMouseButtonDown(0, 0);
        } else if (!mouseDown && s_UI.prevMouseDown) {
            s_UI.context->ProcessMouseButtonUp(0, 0);
        }
        s_UI.prevMouseDown = mouseDown;
    } else if (s_UI.prevMouseDown) {
        s_UI.context->ProcessMouseButtonUp(0, 0);
        s_UI.prevMouseDown = false;
    }

    s_UI.context->Update();
    if (drawList && !s_UI.documents.empty()) {
        s_UI.renderIface->SetDrawTarget(drawList, viewportMin, viewportSize);
        s_UI.context->Render();
    }
}

} // namespace Kyumist

