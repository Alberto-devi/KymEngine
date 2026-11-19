#pragma once

#include "core/Timestep.hpp"
#include "core/Window.hpp"
#include "renderer/VulkanRenderer.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Kyumist {

struct ApplicationSpecification {
    std::string name = "KymEngine";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool headless = false;
    uint32_t maxFrames = 0;
};

class Application {
public:
    explicit Application(const ApplicationSpecification& spec = ApplicationSpecification{});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();

    const ApplicationSpecification& GetSpecification() const { return m_Spec; }
    Window* GetWindow() const { return m_Window.get(); }
    VulkanRenderer* GetRenderer() const { return m_Renderer.get(); }
    void RequestClose() { m_ShouldClose = true; }

protected:
    virtual void OnInit() {}
    virtual void OnUpdate(Timestep ts) {}
    virtual void OnImGuiRender() {}
    virtual void OnShutdown() {}

private:
    ApplicationSpecification m_Spec;
    bool m_ShouldClose = false;
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<VulkanRenderer> m_Renderer;
};

} // namespace Kyumist

