#define VMA_IMPLEMENTATION
#include "renderer/VulkanRenderer.hpp"

#include "core/Log.hpp"
#include "core/PlatformPaths.hpp"
#include "core/Window.hpp"
#include "project/ProjectManager.hpp"
#include "renderer/ModelLoader.hpp"
#include "scene/Components.hpp"
#include "scene/Scene.hpp"

#include <VkBootstrap.h>
#include <imgui.h>
#ifndef __ANDROID__
#include <imgui_impl_glfw.h>
#else
#include <vulkan/vulkan_android.h>
#endif
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kyumist {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/
) {
    const char* msg = pCallbackData && pCallbackData->pMessage ? pCallbackData->pMessage : "";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        KM_CORE_ERROR("[Vulkan Validation] {}", msg);
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        KM_CORE_WARN("[Vulkan Validation] {}", msg);
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        KM_CORE_INFO("[Vulkan Validation] {}", msg);
    } else {
        KM_CORE_TRACE("[Vulkan Validation] {}", msg);
    }
    return VK_FALSE;
}

void CheckVkResult(VkResult result, const char* context) {
    if (result != VK_SUCCESS) {
        KM_CORE_CRITICAL("Vulkan call failed ({}): VkResult = {}", context, static_cast<int>(result));
        throw std::runtime_error(std::string("Vulkan error in ") + context);
    }
}

std::filesystem::path ResolveShaderPath(const std::string& filename) {
    std::filesystem::path configured = PlatformPaths::ResolveShadersDir() / filename;
    if (std::filesystem::exists(configured)) {
        return configured;
    }
    std::filesystem::path fallback = std::filesystem::path("assets/shaders") / filename;
    if (std::filesystem::exists(fallback)) {
        return fallback;
    }
    throw std::runtime_error("Shader SPIR-V file not found: " + filename);
}

std::vector<uint32_t> ReadSpirvFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0 || (fileSize % 4) != 0) {
        throw std::runtime_error("Invalid SPIR-V shader file size: " + path.string());
    }

    std::vector<uint32_t> buffer(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return buffer;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    CheckVkResult(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule), "vkCreateShaderModule");
    return shaderModule;
}

std::filesystem::path ResolveAssetFile(const std::string& relativePath) {
    std::filesystem::path configured = PlatformPaths::ResolveAssetsDir() / relativePath;
    if (std::filesystem::exists(configured)) {
        return configured;
    }
    std::filesystem::path fallback = std::filesystem::path("assets") / relativePath;
    if (std::filesystem::exists(fallback)) {
        return fallback;
    }
    return {};
}

void ApplyKyumistEditorTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.56f, 0.64f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.082f, 0.090f, 0.118f, 1.00f);       // #15171E
    colors[ImGuiCol_ChildBg] = ImVec4(0.094f, 0.106f, 0.137f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.098f, 0.110f, 0.145f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.180f, 0.212f, 0.278f, 0.90f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.118f, 0.133f, 0.176f, 1.00f);        // #1E222D
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.165f, 0.192f, 0.255f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.196f, 0.235f, 0.314f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.063f, 0.071f, 0.094f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.090f, 0.106f, 0.145f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.063f, 0.071f, 0.094f, 0.85f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.075f, 0.086f, 0.114f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.075f, 0.082f, 0.106f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.36f, 0.46f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);      // #3B82F6
    colors[ImGuiCol_SliderGrab] = ImVec4(0.231f, 0.510f, 0.965f, 0.90f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.365f, 0.608f, 0.988f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.145f, 0.173f, 0.231f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.208f, 0.282f, 0.408f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.165f, 0.231f, 0.349f, 0.75f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.231f, 0.510f, 0.965f, 0.45f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.231f, 0.510f, 0.965f, 0.78f);

    colors[ImGuiCol_Separator] = ImVec4(0.180f, 0.212f, 0.278f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.231f, 0.510f, 0.965f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.086f, 0.098f, 0.133f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.200f, 0.345f, 0.580f, 0.85f);
    colors[ImGuiCol_TabActive] = ImVec4(0.149f, 0.235f, 0.380f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.075f, 0.082f, 0.110f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.118f, 0.153f, 0.220f, 1.00f);

    colors[ImGuiCol_DockingPreview] = ImVec4(0.231f, 0.510f, 0.965f, 0.65f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.118f, 0.137f, 0.184f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.180f, 0.212f, 0.278f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.141f, 0.165f, 0.220f, 0.75f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);
}

void BuildCubeData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    struct FaceDef {
        glm::vec3 normal;
        glm::vec3 p0, p1, p2, p3;
    };

    const std::array<FaceDef, 6> faces{{
        {{ 0,  0,  1}, {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}},
        {{ 0,  0, -1}, { 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}},
        {{ 1,  0,  0}, { 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}},
        {{-1,  0,  0}, {-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}},
        {{ 0,  1,  0}, {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}},
        {{ 0, -1,  0}, {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}},
    }};

    vertices.clear();
    indices.clear();
    const glm::vec3 white{1.0f, 1.0f, 1.0f};

    for (const auto& f : faces) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({f.p0, f.normal, white, {0.0f, 0.0f}});
        vertices.push_back({f.p1, f.normal, white, {1.0f, 0.0f}});
        vertices.push_back({f.p2, f.normal, white, {1.0f, 1.0f}});
        vertices.push_back({f.p3, f.normal, white, {0.0f, 1.0f}});
        indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    }
}

void BuildSphereData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, uint32_t slices = 24, uint32_t stacks = 16) {
    vertices.clear();
    indices.clear();

    constexpr float pi = 3.14159265359f;
    constexpr float radius = 0.5f;
    const glm::vec3 white{1.0f, 1.0f, 1.0f};

    for (uint32_t i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);
        const float phi = v * pi;
        for (uint32_t j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u * 2.0f * pi;

            const glm::vec3 normal{
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta)
            };
            vertices.push_back({normal * radius, normal, white, {u, v}});
        }
    }

    for (uint32_t i = 0; i < stacks; ++i) {
        for (uint32_t j = 0; j < slices; ++j) {
            const uint32_t first = i * (slices + 1) + j;
            const uint32_t second = first + slices + 1;
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }
}

void BuildPlaneData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    const glm::vec3 n{0.0f, 1.0f, 0.0f};
    const glm::vec3 white{1.0f, 1.0f, 1.0f};
    vertices = {
        {{-0.5f, 0.0f,  0.5f}, n, white, {0.0f, 0.0f}},
        {{ 0.5f, 0.0f,  0.5f}, n, white, {1.0f, 0.0f}},
        {{ 0.5f, 0.0f, -0.5f}, n, white, {1.0f, 1.0f}},
        {{-0.5f, 0.0f, -0.5f}, n, white, {0.0f, 1.0f}},
    };
}

} // namespace

void VulkanRenderer::ClearModelCache() {
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }
    for (auto& [key, mesh] : m_ObjMeshes) {
        DestroyMesh(mesh);
    }
    m_ObjMeshes.clear();
    m_LoadedModels.clear();
    for (auto& [key, dyn] : m_SkinnedEntityBuffers) {
        DestroyDynamicSkinnedBuffer(dyn);
    }
    m_SkinnedEntityBuffers.clear();
    for (auto& [key, tex] : m_Textures) {
        DestroyTexture(tex);
    }
    m_Textures.clear();
}

VulkanRenderer::VulkanRenderer(Window* window, bool headless)
    : m_Window(window), m_Headless(headless) {
    if (!m_Headless && !m_Window) {
        throw std::runtime_error("VulkanRenderer requires a valid Window when not in headless mode");
    }

    InitVulkan();

    if (m_Headless) {
        CreateHeadlessTarget();
    } else {
        CreateSwapchain();
    }

    CreateSceneTarget(m_Extent.width, m_Extent.height);
    CreateDescriptorResources();
    CreateGraphicsPipeline();
    CreateBuiltinMeshes();
    CreateCommandsAndSync();
    CreateDefaultTextures();
    InitImGui();

    KM_CORE_INFO("VulkanRenderer initialized ({} mode, swapchain {}x{}, scene {}x{}).",
                 m_Headless ? "headless" : "windowed",
                 m_Extent.width,
                 m_Extent.height,
                 m_SceneExtent.width,
                 m_SceneExtent.height);
}

VulkanRenderer::~VulkanRenderer() {
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }

    DestroySceneTarget();

    if (m_SceneSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_SceneSampler, nullptr);
        m_SceneSampler = VK_NULL_HANDLE;
    }

    ShutdownImGui();

    DestroyTexture(m_WhiteTexture);
    for (auto& [key, tex] : m_Textures) {
        DestroyTexture(tex);
    }
    m_Textures.clear();
    for (auto& [handle, tex] : m_UITextures) {
        DestroyTexture(tex);
    }
    m_UITextures.clear();

    for (auto& frame : m_Frames) {
        if (frame.spriteBatchBuffer != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_Allocator, frame.spriteBatchBuffer, frame.spriteBatchAllocation);
        }
        if (frame.lightsBuffer != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_Allocator, frame.lightsBuffer, frame.lightsAllocation);
        }
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, frame.imageAvailableSemaphore, nullptr);
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_Device, frame.inFlightFence, nullptr);
        }
        if (frame.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, frame.commandPool, nullptr);
        }
    }

    if (m_SceneDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_SceneDescPool, nullptr);
        m_SceneDescPool = VK_NULL_HANDLE;
    }
    if (m_TextureDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_TextureDescSetLayout, nullptr);
        m_TextureDescSetLayout = VK_NULL_HANDLE;
    }
    if (m_LightsDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_LightsDescSetLayout, nullptr);
        m_LightsDescSetLayout = VK_NULL_HANDLE;
    }

    if (m_GraphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
    }
    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    }

    DestroyMesh(m_CubeMesh);
    DestroyMesh(m_SphereMesh);
    DestroyMesh(m_PlaneMesh);
    for (auto& [key, mesh] : m_ObjMeshes) {
        DestroyMesh(mesh);
    }
    m_ObjMeshes.clear();
    m_LoadedModels.clear();
    for (auto& [key, dyn] : m_SkinnedEntityBuffers) {
        DestroyDynamicSkinnedBuffer(dyn);
    }
    m_SkinnedEntityBuffers.clear();

    if (m_Headless) {
        DestroyHeadlessTarget();
    } else {
        DestroySwapchain();
    }

    if (m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_Allocator);
    }
    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
    }
    if (m_Surface != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    }
    if (m_DebugMessenger != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE) {
        vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
    }
    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_Instance, nullptr);
    }
}

void VulkanRenderer::InitVulkan() {
    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Kyumist Engine")
        .request_validation_layers(true)
        .require_api_version(1, 3, 0)
        .set_debug_callback(VulkanDebugCallback)
        .set_headless(m_Headless);

    auto instanceRet = instanceBuilder.build();
    if (!instanceRet) {
        KM_CORE_CRITICAL("Failed to create Vulkan instance: {}", instanceRet.error().message());
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    vkb::Instance vkbInstance = instanceRet.value();
    m_Instance = vkbInstance.instance;
    m_DebugMessenger = vkbInstance.debug_messenger;

    if (!m_Headless) {
#ifdef __ANDROID__
        VkAndroidSurfaceCreateInfoKHR surfInfo{};
        surfInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfInfo.window = m_Window->GetAndroidWindow();
        CheckVkResult(
            vkCreateAndroidSurfaceKHR(m_Instance, &surfInfo, nullptr, &m_Surface),
            "vkCreateAndroidSurfaceKHR"
        );
#else
        CheckVkResult(
            glfwCreateWindowSurface(m_Instance, m_Window->GetNativeWindow(), nullptr, &m_Surface),
            "glfwCreateWindowSurface"
        );
#endif
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    selector.set_minimum_version(1, 3)
        .set_required_features_13(features13);

    if (m_Headless) {
        selector.require_present(false);
    } else {
        selector.set_surface(m_Surface);
    }

    auto physRet = selector.select();
    if (!physRet) {
        KM_CORE_CRITICAL("Failed to select Vulkan 1.3 physical device: {}", physRet.error().message());
        throw std::runtime_error("Failed to select Vulkan 1.3 physical device");
    }

    vkb::PhysicalDevice vkbPhysicalDevice = physRet.value();
    m_PhysicalDevice = vkbPhysicalDevice.physical_device;
    m_DeviceName = vkbPhysicalDevice.name;
    KM_CORE_INFO("Selected GPU: {}", m_DeviceName);

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    auto devRet = deviceBuilder.build();
    if (!devRet) {
        KM_CORE_CRITICAL("Failed to create Vulkan logical device: {}", devRet.error().message());
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    vkb::Device vkbDevice = devRet.value();
    m_Device = vkbDevice.device;

    auto graphicsQueueRet = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto graphicsFamilyRet = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    if (!graphicsQueueRet || !graphicsFamilyRet) {
        throw std::runtime_error("Failed to retrieve graphics queue");
    }
    m_GraphicsQueue = graphicsQueueRet.value();
    m_GraphicsQueueFamily = graphicsFamilyRet.value();

    if (!m_Headless) {
        auto presentQueueRet = vkbDevice.get_queue(vkb::QueueType::present);
        if (!presentQueueRet) {
            throw std::runtime_error("Failed to retrieve present queue");
        }
        m_PresentQueue = presentQueueRet.value();
    }

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = m_PhysicalDevice;
    allocatorInfo.device = m_Device;
    allocatorInfo.instance = m_Instance;

    CheckVkResult(vmaCreateAllocator(&allocatorInfo, &m_Allocator), "vmaCreateAllocator");
    KM_CORE_INFO("VMA allocator initialized.");
}

void VulkanRenderer::CreateSwapchain() {
    uint32_t width = m_Window->GetWidth();
    uint32_t height = m_Window->GetHeight();
    if (width == 0 || height == 0) {
        width = 1280;
        height = 720;
    }

    VkSwapchainKHR oldSwapchain = m_Swapchain;

    vkb::SwapchainBuilder swapchainBuilder{m_PhysicalDevice, m_Device, m_Surface};
    swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height);

    if (oldSwapchain != VK_NULL_HANDLE) {
        swapchainBuilder.set_old_swapchain(oldSwapchain);
    }

    auto swapRet = swapchainBuilder.build();
    if (!swapRet) {
        KM_CORE_CRITICAL("Failed to create Vulkan swapchain: {}", swapRet.error().message());
        throw std::runtime_error("Failed to create Vulkan swapchain");
    }

    vkb::Swapchain vkbSwapchain = swapRet.value();
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_Device, oldSwapchain, nullptr);
    }

    m_Swapchain = vkbSwapchain.swapchain;
    m_ColorFormat = vkbSwapchain.image_format;
    m_Extent = vkbSwapchain.extent;
    m_SwapchainImages = vkbSwapchain.get_images().value();
    m_SwapchainImageViews = vkbSwapchain.get_image_views().value();

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    m_RenderFinishedSemaphores.resize(m_SwapchainImages.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < m_SwapchainImages.size(); ++i) {
        CheckVkResult(
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]),
            "vkCreateSemaphore (renderFinished)"
        );
    }
}

void VulkanRenderer::DestroySwapchain() {
    for (VkSemaphore semaphore : m_RenderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        }
    }
    m_RenderFinishedSemaphores.clear();

    for (VkImageView imageView : m_SwapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
    }
    m_SwapchainImageViews.clear();
    m_SwapchainImages.clear();

    if (m_Swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::RecreateSwapchain() {
    int width = static_cast<int>(m_Window->GetWidth());
    int height = static_cast<int>(m_Window->GetHeight());
    while (width == 0 || height == 0) {
#ifndef __ANDROID__
        glfwWaitEvents();
#endif
        if (m_Window->ShouldClose()) {
            return;
        }
        width = static_cast<int>(m_Window->GetWidth());
        height = static_cast<int>(m_Window->GetHeight());
    }

    vkDeviceWaitIdle(m_Device);

    for (VkSemaphore semaphore : m_RenderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        }
    }
    m_RenderFinishedSemaphores.clear();

    for (VkImageView imageView : m_SwapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
    }
    m_SwapchainImageViews.clear();
    m_SwapchainImages.clear();

    CreateSwapchain();
}

void VulkanRenderer::CreateHeadlessTarget() {
    m_ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    m_Extent = {1280, 720};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_ColorFormat;
    imageInfo.extent = {m_Extent.width, m_Extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    CheckVkResult(
        vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_HeadlessImage, &m_HeadlessAllocation, nullptr),
        "vmaCreateImage (headless target)"
    );

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_HeadlessImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_ColorFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    CheckVkResult(
        vkCreateImageView(m_Device, &viewInfo, nullptr, &m_HeadlessImageView),
        "vkCreateImageView (headless target)"
    );
}

void VulkanRenderer::DestroyHeadlessTarget() {
    if (m_HeadlessImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_HeadlessImageView, nullptr);
        m_HeadlessImageView = VK_NULL_HANDLE;
    }
    if (m_HeadlessImage != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, m_HeadlessImage, m_HeadlessAllocation);
        m_HeadlessImage = VK_NULL_HANDLE;
        m_HeadlessAllocation = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::CreateSceneTarget(uint32_t width, uint32_t height) {
    m_SceneExtent = {std::max(width, 1u), std::max(height, 1u)};
    m_RequestedSceneExtent = m_SceneExtent;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    // Color target
    VkImageCreateInfo colorImageInfo{};
    colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
    colorImageInfo.format = m_SceneFormat;
    colorImageInfo.extent = {m_SceneExtent.width, m_SceneExtent.height, 1};
    colorImageInfo.mipLevels = 1;
    colorImageInfo.arrayLayers = 1;
    colorImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    CheckVkResult(
        vmaCreateImage(m_Allocator, &colorImageInfo, &allocInfo, &m_SceneImage, &m_SceneAllocation, nullptr),
        "vmaCreateImage (scene color target)"
    );

    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = m_SceneImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = m_SceneFormat;
    colorViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    CheckVkResult(
        vkCreateImageView(m_Device, &colorViewInfo, nullptr, &m_SceneImageView),
        "vkCreateImageView (scene color target)"
    );

    // Depth target
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = m_SceneDepthFormat;
    depthImageInfo.extent = {m_SceneExtent.width, m_SceneExtent.height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    CheckVkResult(
        vmaCreateImage(m_Allocator, &depthImageInfo, &allocInfo, &m_SceneDepthImage, &m_SceneDepthAllocation, nullptr),
        "vmaCreateImage (scene depth target)"
    );

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_SceneDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = m_SceneDepthFormat;
    depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    CheckVkResult(
        vkCreateImageView(m_Device, &depthViewInfo, nullptr, &m_SceneDepthImageView),
        "vkCreateImageView (scene depth target)"
    );

    if (m_SceneSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;

        CheckVkResult(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_SceneSampler), "vkCreateSampler");
    }

    if (m_ImGuiInitialized) {
        m_SceneTextureDescriptor = ImGui_ImplVulkan_AddTexture(
            m_SceneSampler,
            m_SceneImageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }
}

void VulkanRenderer::DestroySceneTarget() {
    if (m_ImGuiInitialized && m_SceneTextureDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_SceneTextureDescriptor);
        m_SceneTextureDescriptor = VK_NULL_HANDLE;
    }

    if (m_SceneDepthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_SceneDepthImageView, nullptr);
        m_SceneDepthImageView = VK_NULL_HANDLE;
    }
    if (m_SceneDepthImage != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, m_SceneDepthImage, m_SceneDepthAllocation);
        m_SceneDepthImage = VK_NULL_HANDLE;
        m_SceneDepthAllocation = VK_NULL_HANDLE;
    }

    if (m_SceneImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_SceneImageView, nullptr);
        m_SceneImageView = VK_NULL_HANDLE;
    }
    if (m_SceneImage != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, m_SceneImage, m_SceneAllocation);
        m_SceneImage = VK_NULL_HANDLE;
        m_SceneAllocation = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::RequestSceneResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    m_RequestedSceneExtent = {width, height};
}

void VulkanRenderer::SetActiveScene(const Scene* scene, const glm::mat4& viewProj) {
    m_ActiveScene = scene;
    m_ViewProj = viewProj;
}

void VulkanRenderer::CreateDescriptorResources() {
    // Set 0: SceneLightsUBO
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lightsLayoutInfo{};
    lightsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightsLayoutInfo.bindingCount = 1;
    lightsLayoutInfo.pBindings = &uboBinding;
    CheckVkResult(
        vkCreateDescriptorSetLayout(m_Device, &lightsLayoutInfo, nullptr, &m_LightsDescSetLayout),
        "vkCreateDescriptorSetLayout (lights)"
    );

    // Set 1: Albedo Texture Sampler
    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding = 0;
    texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo texLayoutInfo{};
    texLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    texLayoutInfo.bindingCount = 1;
    texLayoutInfo.pBindings = &texBinding;
    CheckVkResult(
        vkCreateDescriptorSetLayout(m_Device, &texLayoutInfo, nullptr, &m_TextureDescSetLayout),
        "vkCreateDescriptorSetLayout (texture)"
    );

    std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 256;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    CheckVkResult(
        vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_SceneDescPool),
        "vkCreateDescriptorPool (scene)"
    );
}

void VulkanRenderer::CreateGraphicsPipeline() {
    const std::vector<uint32_t> vertCode = ReadSpirvFile(ResolveShaderPath("scene.vert.spv"));
    const std::vector<uint32_t> fragCode = ReadSpirvFile(ResolveShaderPath("scene.frag.spv"));

    VkShaderModule vertModule = CreateShaderModule(m_Device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(m_Device, fragCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{vertStageInfo, fragStageInfo};

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescs{};
    attributeDescs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, position))};
    attributeDescs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, normal))};
    attributeDescs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, color))};
    attributeDescs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, uv))};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(DrawPushConstants);

    std::array<VkDescriptorSetLayout, 2> setLayouts{m_LightsDescSetLayout, m_TextureDescSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    CheckVkResult(
        vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout),
        "vkCreatePipelineLayout"
    );

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &m_SceneFormat;
    renderingInfo.depthAttachmentFormat = m_SceneDepthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_Device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &m_GraphicsPipeline
    );

    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);

    CheckVkResult(pipelineResult, "vkCreateGraphicsPipelines");
    KM_CORE_INFO("Hybrid 3D/2D Graphics pipeline created (Dynamic Rendering + Depth D32).");
}

VulkanRenderer::GpuMesh VulkanRenderer::UploadMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices
) {
    GpuMesh mesh{};
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBufferCreateInfo vbInfo{};
    vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbInfo.size = vertices.size() * sizeof(Vertex);
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationInfo vbAllocInfo{};
    CheckVkResult(
        vmaCreateBuffer(m_Allocator, &vbInfo, &allocInfo, &mesh.vertexBuffer, &mesh.vertexAllocation, &vbAllocInfo),
        "vmaCreateBuffer (mesh vertex)"
    );
    std::memcpy(vbAllocInfo.pMappedData, vertices.data(), vbInfo.size);
    CheckVkResult(vmaFlushAllocation(m_Allocator, mesh.vertexAllocation, 0, VK_WHOLE_SIZE), "vmaFlushAllocation (mesh vb)");

    VkBufferCreateInfo ibInfo{};
    ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ibInfo.size = indices.size() * sizeof(uint32_t);
    ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationInfo ibAllocInfo{};
    CheckVkResult(
        vmaCreateBuffer(m_Allocator, &ibInfo, &allocInfo, &mesh.indexBuffer, &mesh.indexAllocation, &ibAllocInfo),
        "vmaCreateBuffer (mesh index)"
    );
    std::memcpy(ibAllocInfo.pMappedData, indices.data(), ibInfo.size);
    CheckVkResult(vmaFlushAllocation(m_Allocator, mesh.indexAllocation, 0, VK_WHOLE_SIZE), "vmaFlushAllocation (mesh ib)");

    return mesh;
}

void VulkanRenderer::DestroyMesh(GpuMesh& mesh) {
    if (mesh.vertexBuffer != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_Allocator, mesh.vertexBuffer, mesh.vertexAllocation);
        mesh.vertexBuffer = VK_NULL_HANDLE;
        mesh.vertexAllocation = VK_NULL_HANDLE;
    }
    if (mesh.indexBuffer != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_Allocator, mesh.indexBuffer, mesh.indexAllocation);
        mesh.indexBuffer = VK_NULL_HANDLE;
        mesh.indexAllocation = VK_NULL_HANDLE;
    }
    mesh.indexCount = 0;
}

void VulkanRenderer::DestroyDynamicSkinnedBuffer(DynamicSkinnedBuffer& dyn) {
    if (m_Allocator != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (dyn.vertexBuffers[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_Allocator, dyn.vertexBuffers[i], dyn.vertexAllocations[i]);
                dyn.vertexBuffers[i] = VK_NULL_HANDLE;
                dyn.vertexAllocations[i] = VK_NULL_HANDLE;
                dyn.mapped[i] = nullptr;
            }
        }
    }
    dyn.vertexCapacity = 0;
}

VkBuffer VulkanRenderer::GetOrUpdateSkinnedVertexBuffer(
    uint32_t entityKey,
    const LoadedModelData& modelData,
    const AnimatorComponent& anim
) {
    const uint32_t vertCount = static_cast<uint32_t>(modelData.bindVertices.size());
    if (vertCount == 0) {
        return VK_NULL_HANDLE;
    }

    auto& dyn = m_SkinnedEntityBuffers[entityKey];
    if (dyn.vertexCapacity < vertCount) {
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
        }
        DestroyDynamicSkinnedBuffer(dyn);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBufferCreateInfo vbInfo{};
        vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbInfo.size = static_cast<VkDeviceSize>(vertCount) * sizeof(Vertex);
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            VmaAllocationInfo vbAllocInfo{};
            CheckVkResult(
                vmaCreateBuffer(m_Allocator, &vbInfo, &allocInfo, &dyn.vertexBuffers[i], &dyn.vertexAllocations[i], &vbAllocInfo),
                "vmaCreateBuffer (skinned vertex)"
            );
            dyn.mapped[i] = vbAllocInfo.pMappedData;
        }
        dyn.vertexCapacity = vertCount;
    }

    ModelLoader::EvaluateSkinnedVertices(
        modelData,
        anim.currentAnimation,
        anim.time,
        anim.loop,
        anim.autoPlayFirst,
        m_SkinnedScratchVerts
    );

    const uint32_t frameIdx = m_CurrentFrame % MAX_FRAMES_IN_FLIGHT;
    if (dyn.mapped[frameIdx] && m_SkinnedScratchVerts.size() == vertCount) {
        std::memcpy(dyn.mapped[frameIdx], m_SkinnedScratchVerts.data(), static_cast<size_t>(vertCount) * sizeof(Vertex));
        vmaFlushAllocation(m_Allocator, dyn.vertexAllocations[frameIdx], 0, VK_WHOLE_SIZE);
    }
    return dyn.vertexBuffers[frameIdx];
}

const VulkanRenderer::GpuMesh* VulkanRenderer::GetOrLoadObjMesh(const std::string& path) {
    if (path.empty()) {
        return &m_CubeMesh;
    }
    auto it = m_ObjMeshes.find(path);
    if (it != m_ObjMeshes.end()) {
        return &it->second;
    }

    std::filesystem::path resolved = path;
    if (!std::filesystem::exists(resolved)) {
        if (ProjectManager::HasActiveProject()) {
            std::filesystem::path inProject = ProjectManager::GetActiveProjectDir() / path;
            if (std::filesystem::exists(inProject)) {
                resolved = inProject;
            }
        }
    }
    if (!std::filesystem::exists(resolved)) {
        std::filesystem::path inAssets = PlatformPaths::ResolveAssetsDir() / path;
        if (std::filesystem::exists(inAssets)) {
            resolved = inAssets;
        }
    }

    LoadedModelData loaded;
    if (!ModelLoader::LoadModel(resolved, loaded)) {
        KM_CORE_WARN("Failed to load 3D model '{}', falling back to Cube.", path);
        return &m_CubeMesh;
    }

    GpuMesh uploaded = UploadMesh(loaded.vertices, loaded.indices);
    auto [inserted, _] = m_ObjMeshes.emplace(path, uploaded);
    KM_CORE_INFO("Loaded 3D model '{}' ({} verts, {} indices).", resolved.string(), loaded.vertices.size(), uploaded.indexCount);
    if (loaded.hasSkin) {
        KM_CORE_INFO("  -> Skeletal skin detected: {} joints, {} animation clip(s).",
            loaded.skeleton.jointNodeIndices.size(), loaded.skeleton.animations.size());
    }
    m_LoadedModels.emplace(path, std::move(loaded));
    return &inserted->second;
}

void VulkanRenderer::DestroyTexture(GpuTexture& tex) {
    if (tex.descriptorSet != VK_NULL_HANDLE && m_SceneDescPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(m_Device, m_SceneDescPool, 1, &tex.descriptorSet);
        tex.descriptorSet = VK_NULL_HANDLE;
    }
    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, tex.sampler, nullptr);
        tex.sampler = VK_NULL_HANDLE;
    }
    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
    }
}

VulkanRenderer::GpuTexture VulkanRenderer::CreateTextureFromRGBA(
    uint32_t width,
    uint32_t height,
    const uint8_t* rgbaPixels
) {
    GpuTexture tex{};
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo mappedInfo{};
    CheckVkResult(
        vmaCreateBuffer(m_Allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, &mappedInfo),
        "vmaCreateBuffer (texture staging)"
    );
    std::memcpy(mappedInfo.pMappedData, rgbaPixels, imageSize);
    CheckVkResult(vmaFlushAllocation(m_Allocator, stagingAllocation, 0, VK_WHOLE_SIZE), "vmaFlushAllocation (texture)");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    CheckVkResult(
        vmaCreateImage(m_Allocator, &imageInfo, &imgAllocInfo, &tex.image, &tex.allocation, nullptr),
        "vmaCreateImage (texture)"
    );

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = m_Frames[0].commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    CheckVkResult(vkAllocateCommandBuffers(m_Device, &cmdAlloc, &cmd), "vkAllocateCommandBuffers (tex upload)");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer (tex upload)");

    VkImageMemoryBarrier2 toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.image = tex.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep1{};
    dep1.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep1.imageMemoryBarrierCount = 1;
    dep1.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(cmd, &dep1);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier2 toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toShaderRead.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toShaderRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toShaderRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.image = tex.image;
    toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep2{};
    dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep2.imageMemoryBarrierCount = 1;
    dep2.pImageMemoryBarriers = &toShaderRead;
    vkCmdPipelineBarrier2(cmd, &dep2);

    CheckVkResult(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (tex upload)");

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;
    CheckVkResult(vkQueueSubmit2(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit2 (tex upload)");
    CheckVkResult(vkQueueWaitIdle(m_GraphicsQueue), "vkQueueWaitIdle (tex upload)");

    vkFreeCommandBuffers(m_Device, m_Frames[0].commandPool, 1, &cmd);
    vmaDestroyBuffer(m_Allocator, stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    CheckVkResult(vkCreateImageView(m_Device, &viewInfo, nullptr, &tex.view), "vkCreateImageView (texture)");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    CheckVkResult(vkCreateSampler(m_Device, &samplerInfo, nullptr, &tex.sampler), "vkCreateSampler (texture)");

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = m_SceneDescPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_TextureDescSetLayout;
    CheckVkResult(vkAllocateDescriptorSets(m_Device, &dsAlloc, &tex.descriptorSet), "vkAllocateDescriptorSets (texture)");

    VkDescriptorImageInfo imgDescInfo{};
    imgDescInfo.sampler = tex.sampler;
    imgDescInfo.imageView = tex.view;
    imgDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = tex.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgDescInfo;
    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

    return tex;
}

void VulkanRenderer::CreateDefaultTextures() {
    const std::array<uint8_t, 4> whitePixel{255, 255, 255, 255};
    m_WhiteTexture = CreateTextureFromRGBA(1, 1, whitePixel.data());
}

VkDescriptorSet VulkanRenderer::GetOrLoadTextureDescriptor(const std::string& texturePath) {
    if (texturePath.empty()) {
        return m_WhiteTexture.descriptorSet;
    }
    auto it = m_Textures.find(texturePath);
    if (it != m_Textures.end()) {
        return it->second.descriptorSet;
    }

    std::filesystem::path resolved = texturePath;
    if (!std::filesystem::exists(resolved)) {
        if (ProjectManager::HasActiveProject()) {
            std::filesystem::path inProj = ProjectManager::GetActiveProjectDir() / texturePath;
            if (std::filesystem::exists(inProj)) {
                resolved = inProj;
            }
        }
    }
    if (!std::filesystem::exists(resolved)) {
        std::filesystem::path inAssets = PlatformPaths::ResolveAssetsDir() / texturePath;
        if (std::filesystem::exists(inAssets)) {
            resolved = inAssets;
        }
    }

    ImageData img;
    if (!ModelLoader::LoadImageRGBA(resolved, img) || img.pixels.empty()) {
        KM_CORE_WARN("Failed to load texture '{}', using white fallback.", texturePath);
        return m_WhiteTexture.descriptorSet;
    }

    GpuTexture uploaded = CreateTextureFromRGBA(img.width, img.height, img.pixels.data());
    auto [inserted, _] = m_Textures.emplace(texturePath, uploaded);
    KM_CORE_INFO("Loaded GPU texture '{}' ({}x{}).", resolved.string(), img.width, img.height);
    return inserted->second.descriptorSet;
}

uint64_t VulkanRenderer::CreateUITextureRGBA(uint32_t width, uint32_t height, const uint8_t* rgbaPixels) {
    if (m_Device == VK_NULL_HANDLE || width == 0 || height == 0 || rgbaPixels == nullptr) {
        return 0;
    }
    GpuTexture tex = CreateTextureFromRGBA(width, height, rgbaPixels);
    const uint64_t handle = reinterpret_cast<uint64_t>(tex.descriptorSet);
    m_UITextures[handle] = tex;
    return handle;
}

uint64_t VulkanRenderer::LoadUITextureFile(const std::string& path, uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = 0;
    outHeight = 0;
    if (m_Device == VK_NULL_HANDLE || path.empty()) {
        return 0;
    }
    std::filesystem::path resolved = path;
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec) && ProjectManager::HasActiveProject()) {
        const std::filesystem::path inProj = ProjectManager::GetActiveProjectDir() / path;
        if (std::filesystem::exists(inProj, ec)) {
            resolved = inProj;
        }
    }
    if (!std::filesystem::exists(resolved, ec)) {
        const std::filesystem::path inAssets = PlatformPaths::ResolveAssetsDir() / path;
        if (std::filesystem::exists(inAssets, ec)) {
            resolved = inAssets;
        }
    }
    ImageData img;
    if (!ModelLoader::LoadImageRGBA(resolved, img) || img.pixels.empty()) {
        return 0;
    }
    outWidth = img.width;
    outHeight = img.height;
    return CreateUITextureRGBA(img.width, img.height, img.pixels.data());
}

void VulkanRenderer::DestroyUITexture(uint64_t handle) {
    const auto it = m_UITextures.find(handle);
    if (it == m_UITextures.end()) {
        return;
    }
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }
    DestroyTexture(it->second);
    m_UITextures.erase(it);
}

void VulkanRenderer::CreateBuiltinMeshes() {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    BuildCubeData(vertices, indices);
    m_CubeMesh = UploadMesh(vertices, indices);

    BuildSphereData(vertices, indices);
    m_SphereMesh = UploadMesh(vertices, indices);

    BuildPlaneData(vertices, indices);
    m_PlaneMesh = UploadMesh(vertices, indices);

    KM_CORE_INFO("Built-in 3D meshes uploaded (Cube, Sphere, Plane).");
}

void VulkanRenderer::CreateCommandsAndSync() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkBufferCreateInfo batchBufferInfo{};
    batchBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    batchBufferInfo.size = static_cast<VkDeviceSize>(MAX_BATCH_SPRITES) * 6 * sizeof(Vertex);
    batchBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    batchBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo batchAllocInfo{};
    batchAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    batchAllocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBufferCreateInfo uboBufferInfo{};
    uboBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboBufferInfo.size = sizeof(SceneLightsUBO);
    uboBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uboBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (auto& frame : m_Frames) {
        CheckVkResult(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &frame.commandPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        CheckVkResult(
            vkAllocateCommandBuffers(m_Device, &allocInfo, &frame.commandBuffer),
            "vkAllocateCommandBuffers"
        );
        CheckVkResult(
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &frame.imageAvailableSemaphore),
            "vkCreateSemaphore (imageAvailable)"
        );
        CheckVkResult(vkCreateFence(m_Device, &fenceInfo, nullptr, &frame.inFlightFence), "vkCreateFence");

        VmaAllocationInfo vmaInfo{};
        CheckVkResult(
            vmaCreateBuffer(
                m_Allocator,
                &batchBufferInfo,
                &batchAllocInfo,
                &frame.spriteBatchBuffer,
                &frame.spriteBatchAllocation,
                &vmaInfo
            ),
            "vmaCreateBuffer (2D sprite batch)"
        );
        frame.spriteBatchMapped = vmaInfo.pMappedData;

        VmaAllocationInfo uboMappedInfo{};
        CheckVkResult(
            vmaCreateBuffer(
                m_Allocator,
                &uboBufferInfo,
                &batchAllocInfo,
                &frame.lightsBuffer,
                &frame.lightsAllocation,
                &uboMappedInfo
            ),
            "vmaCreateBuffer (SceneLightsUBO)"
        );
        frame.lightsMapped = uboMappedInfo.pMappedData;

        VkDescriptorSetAllocateInfo dsAlloc{};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = m_SceneDescPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &m_LightsDescSetLayout;
        CheckVkResult(vkAllocateDescriptorSets(m_Device, &dsAlloc, &frame.lightsDescriptorSet), "vkAllocateDescriptorSets (lights)");

        VkDescriptorBufferInfo bufDescInfo{};
        bufDescInfo.buffer = frame.lightsBuffer;
        bufDescInfo.offset = 0;
        bufDescInfo.range = sizeof(SceneLightsUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.lightsDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufDescInfo;
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }
}

void VulkanRenderer::InitImGui() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 100;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 100;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    CheckVkResult(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_ImGuiDescriptorPool), "vkCreateDescriptorPool (ImGui)");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyKyumistEditorTheme();

    const std::filesystem::path robotoPath = ResolveAssetFile("fonts/Roboto-Medium.ttf");
    const std::filesystem::path cousinePath = ResolveAssetFile("fonts/Cousine-Regular.ttf");

    ImFontConfig fontConfig{};
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;

    if (!robotoPath.empty()) {
        io.Fonts->AddFontFromFileTTF(robotoPath.string().c_str(), 15.5f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    } else {
        io.Fonts->AddFontDefault();
    }

    if (!cousinePath.empty()) {
        io.Fonts->AddFontFromFileTTF(cousinePath.string().c_str(), 14.5f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    } else {
        io.Fonts->AddFontDefault();
    }

    if (!robotoPath.empty()) {
        io.Fonts->AddFontFromFileTTF(robotoPath.string().c_str(), 17.5f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    }

    if (!m_Headless) {
#ifndef __ANDROID__
        ImGui_ImplGlfw_InitForVulkan(m_Window->GetNativeWindow(), true);
#else
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_Extent.width), static_cast<float>(m_Extent.height));
#endif
    }

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = m_Instance;
    initInfo.PhysicalDevice = m_PhysicalDevice;
    initInfo.Device = m_Device;
    initInfo.QueueFamily = m_GraphicsQueueFamily;
    initInfo.Queue = m_GraphicsQueue;
    initInfo.DescriptorPool = m_ImGuiDescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = m_Headless ? 2 : static_cast<uint32_t>(m_SwapchainImages.size());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_ColorFormat;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }

    m_ImGuiInitialized = true;

    m_SceneTextureDescriptor = ImGui_ImplVulkan_AddTexture(
        m_SceneSampler,
        m_SceneImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    KM_CORE_INFO("Dear ImGui initialized (Docking + ImGuizmo + Vulkan Dynamic Rendering).");
}

void VulkanRenderer::ShutdownImGui() {
    if (!m_ImGuiInitialized) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    if (!m_Headless) {
#ifndef __ANDROID__
        ImGui_ImplGlfw_Shutdown();
#endif
    }
    ImGui::DestroyContext();
    m_ImGuiInitialized = false;

    if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_ImGuiDescriptorPool, nullptr);
        m_ImGuiDescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::BeginImGuiFrame() {
    if (m_RequestedSceneExtent.width != m_SceneExtent.width ||
        m_RequestedSceneExtent.height != m_SceneExtent.height) {
        vkDeviceWaitIdle(m_Device);
        DestroySceneTarget();
        CreateSceneTarget(m_RequestedSceneExtent.width, m_RequestedSceneExtent.height);
    }

    ImGui_ImplVulkan_NewFrame();
#ifndef __ANDROID__
    if (!m_Headless) {
        ImGui_ImplGlfw_NewFrame();
    } else {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_Extent.width), static_cast<float>(m_Extent.height));
        io.DeltaTime = 1.0f / 60.0f;
    }
#else
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_Extent.width), static_cast<float>(m_Extent.height));
        io.DeltaTime = 1.0f / 60.0f;
    }
#endif

    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void VulkanRenderer::EndImGuiFrameAndDraw() {
    ImGui::Render();
    DrawFrame();
}

void VulkanRenderer::RecordScenePass(VkCommandBuffer cmd) {
    std::array<VkImageMemoryBarrier2, 2> beginBarriers{};

    beginBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    beginBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    beginBarriers[0].srcAccessMask = 0;
    beginBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    beginBarriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    beginBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    beginBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    beginBarriers[0].image = m_SceneImage;
    beginBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    beginBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    beginBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    beginBarriers[1].srcAccessMask = 0;
    beginBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    beginBarriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    beginBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    beginBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    beginBarriers[1].image = m_SceneDepthImage;
    beginBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo beginDep{};
    beginDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    beginDep.imageMemoryBarrierCount = static_cast<uint32_t>(beginBarriers.size());
    beginDep.pImageMemoryBarriers = beginBarriers.data();
    vkCmdPipelineBarrier2(cmd, &beginDep);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_SceneImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_SceneDepthImageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, m_SceneExtent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    // Inverted viewport height so GLM +Y-up coordinates and ImGuizmo match Vulkan clip space
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(m_SceneExtent.height);
    viewport.width = static_cast<float>(m_SceneExtent.width);
    viewport.height = -static_cast<float>(m_SceneExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SceneExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (m_ActiveScene) {
        const auto& registry = m_ActiveScene->GetRegistry();
        FrameData& frame = m_Frames[m_CurrentFrame];

        SceneLightsUBO ubo{};
        ubo.viewProj = m_ViewProj;

        glm::vec3 camEye(0.0f, 2.2f, 5.5f);
        if (std::abs(glm::determinant(m_ViewProj)) > 1e-6f) {
            const glm::mat4 invVP = glm::inverse(m_ViewProj);
            glm::vec4 nearWorld = invVP * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
            if (std::abs(nearWorld.w) > 1e-5f) {
                camEye = glm::vec3(nearWorld) / nearWorld.w;
            }
        }

        glm::vec3 lightDir = glm::normalize(glm::vec3(0.45f, 0.80f, 0.50f));
        glm::vec3 lightColor(1.0f, 0.98f, 0.92f);
        float lightIntensity = 1.0f;

        auto lightView = registry.view<TransformComponent, DirectionalLightComponent>();
        for (auto entity : lightView) {
            const auto& dl = lightView.get<DirectionalLightComponent>(entity);
            const glm::mat4 world = m_ActiveScene->GetWorldTransform(entity);
            const glm::vec3 forward = glm::mat3(world) * glm::vec3(0.0f, 0.0f, -1.0f);
            if (glm::length(forward) > 0.0001f) {
                lightDir = glm::normalize(-forward);
            }
            lightColor = dl.color;
            lightIntensity = dl.intensity;
            break;
        }
        ubo.dirLightDirIntensity = glm::vec4(lightDir, lightIntensity);

        bool skyEnabled = false;
        auto skyView = registry.view<SkyboxComponent>();
        for (auto entity : skyView) {
            const auto& sky = skyView.get<SkyboxComponent>(entity);
            if (sky.enabled) {
                skyEnabled = true;
                ubo.skyZenith = glm::vec4(sky.zenithColor, 1.0f);
                ubo.skyHorizon = glm::vec4(sky.horizonColor, 1.0f);
                ubo.skyGround = glm::vec4(sky.groundColor, 1.0f);
                break;
            }
        }
        ubo.dirLightColorSky = glm::vec4(lightColor, skyEnabled ? 1.0f : 0.0f);

        int pointCount = 0;
        auto pointView = registry.view<TransformComponent, PointLightComponent>();
        for (auto entity : pointView) {
            if (pointCount >= 8) {
                break;
            }
            const auto& pl = pointView.get<PointLightComponent>(entity);
            const glm::mat4 world = m_ActiveScene->GetWorldTransform(entity);
            const glm::vec3 pos = glm::vec3(world[3]);
            ubo.pointLightPosRange[pointCount] = glm::vec4(pos, std::max(pl.range, 0.1f));
            ubo.pointLightColorInt[pointCount] = glm::vec4(pl.color, std::max(pl.intensity, 0.0f));
            ++pointCount;
        }
        ubo.cameraPosAndPointCount = glm::vec4(camEye, static_cast<float>(pointCount));

        std::memcpy(frame.lightsMapped, &ubo, sizeof(SceneLightsUBO));
        vmaFlushAllocation(m_Allocator, frame.lightsAllocation, 0, sizeof(SceneLightsUBO));

        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_PipelineLayout,
            0,
            1,
            &frame.lightsDescriptorSet,
            0,
            nullptr
        );

        const VkDeviceSize vbOffset = 0;

        // 0. Draw Skybox if enabled
        if (skyEnabled) {
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_PipelineLayout,
                1,
                1,
                &m_WhiteTexture.descriptorSet,
                0,
                nullptr
            );

            DrawPushConstants skyPc{};
            skyPc.model = glm::translate(glm::mat4(1.0f), camEye) * glm::scale(glm::mat4(1.0f), glm::vec3(35.0f));
            skyPc.normalCol0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            skyPc.normalCol1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            skyPc.normalCol2 = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            skyPc.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, -3.0f);

            vkCmdPushConstants(
                cmd,
                m_PipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(DrawPushConstants),
                &skyPc
            );

            vkCmdBindVertexBuffers(cmd, 0, 1, &m_SphereMesh.vertexBuffer, &vbOffset);
            vkCmdBindIndexBuffer(cmd, m_SphereMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, m_SphereMesh.indexCount, 1, 0, 0, 0);
        }

        // 1. Collect & Draw 3D meshes
        struct ShadowCaster {
            const GpuMesh* gpuMesh;
            VkBuffer vertexBuffer;
            glm::mat4 world;
        };
        std::vector<ShadowCaster> shadowCasters;

        auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView) {
            const auto& meshComp = meshView.get<MeshRendererComponent>(entity);
            if (!meshComp.visible) {
                continue;
            }

            const GpuMesh* gpuMesh = &m_CubeMesh;
            switch (meshComp.type) {
                case MeshType::Cube:      gpuMesh = &m_CubeMesh; break;
                case MeshType::Sphere:    gpuMesh = &m_SphereMesh; break;
                case MeshType::Plane:     gpuMesh = &m_PlaneMesh; break;
                case MeshType::CustomOBJ: gpuMesh = GetOrLoadObjMesh(meshComp.objPath); break;
            }

            VkBuffer activeVertexBuffer = gpuMesh->vertexBuffer;
            if (meshComp.type == MeshType::CustomOBJ && !meshComp.objPath.empty()) {
                if (const auto* animComp = registry.try_get<AnimatorComponent>(entity)) {
                    auto modelIt = m_LoadedModels.find(meshComp.objPath);
                    if (modelIt != m_LoadedModels.end() && modelIt->second.hasSkin) {
                        VkBuffer skinnedVb = GetOrUpdateSkinnedVertexBuffer(
                            static_cast<uint32_t>(entity),
                            modelIt->second,
                            *animComp
                        );
                        if (skinnedVb != VK_NULL_HANDLE) {
                            activeVertexBuffer = skinnedVb;
                        }
                    }
                }
            }

            const glm::mat4 model = m_ActiveScene->GetWorldTransform(entity);
            const glm::mat3 linearModel = glm::mat3(model);
            const glm::mat3 normalMat = (std::abs(glm::determinant(linearModel)) > 1e-6f)
                ? glm::transpose(glm::inverse(linearModel))
                : linearModel;

            const float packedRoughness = std::clamp(meshComp.roughness, 0.05f, 1.0f);
            const float packedMetallic = std::clamp(meshComp.metallic, 0.0f, 1.0f);
            const float packedMode = (meshComp.type == MeshType::Plane)
                ? (2.0f + packedRoughness)
                : packedRoughness;

            VkDescriptorSet texDesc = GetOrLoadTextureDescriptor(meshComp.texturePath);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_PipelineLayout,
                1,
                1,
                &texDesc,
                0,
                nullptr
            );

            DrawPushConstants pc{};
            pc.model = model;
            pc.normalCol0 = glm::vec4(normalMat[0], packedMetallic);
            pc.normalCol1 = glm::vec4(normalMat[1], 0.0f);
            pc.normalCol2 = glm::vec4(normalMat[2], 0.0f);
            pc.baseColor = glm::vec4(
                meshComp.color.r + meshComp.emissive.r,
                meshComp.color.g + meshComp.emissive.g,
                meshComp.color.b + meshComp.emissive.b,
                packedMode
            );

            vkCmdPushConstants(
                cmd,
                m_PipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(DrawPushConstants),
                &pc
            );

            vkCmdBindVertexBuffers(cmd, 0, 1, &activeVertexBuffer, &vbOffset);
            vkCmdBindIndexBuffer(cmd, gpuMesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, gpuMesh->indexCount, 1, 0, 0, 0);

            if (meshComp.castShadows && meshComp.type != MeshType::Plane && model[3].y > 0.05f) {
                shadowCasters.push_back({gpuMesh, activeVertexBuffer, model});
            }
        }

        // 1b. Draw Planar Ground Shadows onto y = 0.015f
        if (!shadowCasters.empty() && lightDir.y > 0.08f) {
            const float gY = 0.015f;
            const float sx = -lightDir.x / lightDir.y;
            const float sz = -lightDir.z / lightDir.y;
            glm::mat4 shadowProj(1.0f);
            shadowProj[1][0] = sx;
            shadowProj[1][1] = 0.0f;
            shadowProj[1][2] = sz;
            shadowProj[3][0] = -sx * gY;
            shadowProj[3][1] = gY;
            shadowProj[3][2] = -sz * gY;

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_PipelineLayout,
                1,
                1,
                &m_WhiteTexture.descriptorSet,
                0,
                nullptr
            );

            for (const auto& caster : shadowCasters) {
                DrawPushConstants shadowPc{};
                shadowPc.model = shadowProj * caster.world;
                shadowPc.normalCol0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                shadowPc.normalCol1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
                shadowPc.normalCol2 = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
                shadowPc.baseColor = glm::vec4(0.02f, 0.03f, 0.05f, -4.0f);

                vkCmdPushConstants(
                    cmd,
                    m_PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(DrawPushConstants),
                    &shadowPc
                );

                vkCmdBindVertexBuffers(cmd, 0, 1, &caster.vertexBuffer, &vbOffset);
                vkCmdBindIndexBuffer(cmd, caster.gpuMesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, caster.gpuMesh->indexCount, 1, 0, 0, 0);
            }
        }

        // 2. Batch Render 2D Sprites sorted by sortingOrder (with Spritesheet UVs & Textures)
        struct SpriteItem {
            glm::mat4 transform;
            glm::vec3 color;
            int32_t sortingOrder;
            bool checkerPattern;
            std::string texturePath;
            int32_t hFrames;
            int32_t vFrames;
            int32_t frameIndex;
        };

        std::vector<SpriteItem> sprites;
        auto spriteView = registry.view<TransformComponent, SpriteRendererComponent>();
        for (auto entity : spriteView) {
            const auto& spr = spriteView.get<SpriteRendererComponent>(entity);
            if (!spr.visible) {
                continue;
            }
            sprites.push_back({
                m_ActiveScene->GetWorldTransform(entity),
                glm::vec3(spr.color),
                spr.sortingOrder,
                spr.checkerPattern,
                spr.texturePath,
                std::max(1, spr.hFrames),
                std::max(1, spr.vFrames),
                std::max(0, spr.frameIndex)
            });
        }

        if (!sprites.empty()) {
            std::stable_sort(sprites.begin(), sprites.end(), [](const SpriteItem& a, const SpriteItem& b) {
                return a.sortingOrder < b.sortingOrder;
            });

            auto* mappedVerts = static_cast<Vertex*>(frame.spriteBatchMapped);
            const size_t spriteCount = std::min(sprites.size(), static_cast<size_t>(MAX_BATCH_SPRITES));

            struct BatchRun {
                uint32_t firstVertex;
                uint32_t vertexCount;
                bool checker;
                std::string texturePath;
            };
            std::vector<BatchRun> runs;

            uint32_t totalVertices = 0;
            for (size_t i = 0; i < spriteCount; ++i) {
                const auto& spr = sprites[i];
                const glm::mat3 nMat = glm::mat3(spr.transform);
                const glm::vec3 rawN = nMat * glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec3 normal = glm::length(rawN) > 0.0001f ? glm::normalize(rawN) : glm::vec3(0.0f, 0.0f, 1.0f);

                const int32_t totalFrames = spr.hFrames * spr.vFrames;
                const int32_t fIdx = (totalFrames > 0) ? (spr.frameIndex % totalFrames) : 0;
                const int32_t col = fIdx % spr.hFrames;
                const int32_t row = fIdx / spr.hFrames;

                const float uMin = static_cast<float>(col) / static_cast<float>(spr.hFrames);
                const float uMax = static_cast<float>(col + 1) / static_cast<float>(spr.hFrames);
                const float vTop = static_cast<float>(row) / static_cast<float>(spr.vFrames);
                const float vBot = static_cast<float>(row + 1) / static_cast<float>(spr.vFrames);

                const glm::vec3 p0 = glm::vec3(spr.transform * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f));
                const glm::vec3 p1 = glm::vec3(spr.transform * glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f));
                const glm::vec3 p2 = glm::vec3(spr.transform * glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f));
                const glm::vec3 p3 = glm::vec3(spr.transform * glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f));

                mappedVerts[totalVertices + 0] = {p0, normal, spr.color, {uMin, vBot}};
                mappedVerts[totalVertices + 1] = {p1, normal, spr.color, {uMax, vBot}};
                mappedVerts[totalVertices + 2] = {p2, normal, spr.color, {uMax, vTop}};
                mappedVerts[totalVertices + 3] = {p2, normal, spr.color, {uMax, vTop}};
                mappedVerts[totalVertices + 4] = {p3, normal, spr.color, {uMin, vTop}};
                mappedVerts[totalVertices + 5] = {p0, normal, spr.color, {uMin, vBot}};

                if (runs.empty() || runs.back().checker != spr.checkerPattern || runs.back().texturePath != spr.texturePath) {
                    runs.push_back({totalVertices, 6, spr.checkerPattern, spr.texturePath});
                } else {
                    runs.back().vertexCount += 6;
                }
                totalVertices += 6;
            }

            vmaFlushAllocation(
                m_Allocator,
                frame.spriteBatchAllocation,
                0,
                static_cast<VkDeviceSize>(totalVertices) * sizeof(Vertex)
            );

            vkCmdBindVertexBuffers(cmd, 0, 1, &frame.spriteBatchBuffer, &vbOffset);

            for (const auto& run : runs) {
                VkDescriptorSet texDesc = GetOrLoadTextureDescriptor(run.texturePath);
                vkCmdBindDescriptorSets(
                    cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_PipelineLayout,
                    1,
                    1,
                    &texDesc,
                    0,
                    nullptr
                );

                DrawPushConstants pc{};
                pc.model = glm::mat4(1.0f);
                pc.normalCol0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                pc.normalCol1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
                pc.normalCol2 = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
                pc.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, run.checker ? -2.0f : -1.0f);

                vkCmdPushConstants(
                    cmd,
                    m_PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(DrawPushConstants),
                    &pc
                );

                vkCmdDraw(cmd, run.vertexCount, 1, run.firstVertex, 0);
            }
        }
    }

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toShaderReadBarrier{};
    toShaderReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toShaderReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toShaderReadBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toShaderReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toShaderReadBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toShaderReadBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toShaderReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderReadBarrier.image = m_SceneImage;
    toShaderReadBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo toShaderReadDep{};
    toShaderReadDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toShaderReadDep.imageMemoryBarrierCount = 1;
    toShaderReadDep.pImageMemoryBarriers = &toShaderReadBarrier;
    vkCmdPipelineBarrier2(cmd, &toShaderReadDep);
}

void VulkanRenderer::RecordImGuiPass(
    VkCommandBuffer cmd,
    VkImage targetImage,
    VkImageView targetView,
    VkImageLayout finalLayout
) {
    VkImageMemoryBarrier2 toColorBarrier{};
    toColorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toColorBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColorBarrier.srcAccessMask = 0;
    toColorBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColorBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorBarrier.image = targetImage;
    toColorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo toColorDep{};
    toColorDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toColorDep.imageMemoryBarrierCount = 1;
    toColorDep.pImageMemoryBarriers = &toColorBarrier;
    vkCmdPipelineBarrier2(cmd, &toColorDep);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = targetView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.06f, 0.06f, 0.08f, 1.0f}};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, m_Extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toFinalBarrier{};
    toFinalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toFinalBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toFinalBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toFinalBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    toFinalBarrier.dstAccessMask = 0;
    toFinalBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toFinalBarrier.newLayout = finalLayout;
    toFinalBarrier.image = targetImage;
    toFinalBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo toFinalDep{};
    toFinalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toFinalDep.imageMemoryBarrierCount = 1;
    toFinalDep.pImageMemoryBarriers = &toFinalBarrier;
    vkCmdPipelineBarrier2(cmd, &toFinalDep);
}

void VulkanRenderer::RecordCommandBuffer(
    VkCommandBuffer cmd,
    VkImage targetImage,
    VkImageView targetView,
    VkImageLayout finalLayout
) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");

    RecordScenePass(cmd);
    RecordImGuiPass(cmd, targetImage, targetView, finalLayout);

    CheckVkResult(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
}

void VulkanRenderer::DrawFrame() {
    FrameData& frame = m_Frames[m_CurrentFrame];

    CheckVkResult(
        vkWaitForFences(m_Device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX),
        "vkWaitForFences"
    );

    if (m_Headless) {
        CheckVkResult(vkResetFences(m_Device, 1, &frame.inFlightFence), "vkResetFences");
        CheckVkResult(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer");

        RecordCommandBuffer(frame.commandBuffer, m_HeadlessImage, m_HeadlessImageView, VK_IMAGE_LAYOUT_GENERAL);

        VkCommandBufferSubmitInfo cmdSubmitInfo{};
        cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmitInfo.commandBuffer = frame.commandBuffer;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

        CheckVkResult(vkQueueSubmit2(m_GraphicsQueue, 1, &submitInfo, frame.inFlightFence), "vkQueueSubmit2 (headless)");
        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        return;
    }

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        m_Device,
        m_Swapchain,
        UINT64_MAX,
        frame.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        CheckVkResult(acquireResult, "vkAcquireNextImageKHR");
    }

    CheckVkResult(vkResetFences(m_Device, 1, &frame.inFlightFence), "vkResetFences");
    CheckVkResult(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer");

    RecordCommandBuffer(
        frame.commandBuffer,
        m_SwapchainImages[imageIndex],
        m_SwapchainImageViews[imageIndex],
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = frame.imageAvailableSemaphore;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = m_RenderFinishedSemaphores[imageIndex];
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

    CheckVkResult(vkQueueSubmit2(m_GraphicsQueue, 1, &submitInfo, frame.inFlightFence), "vkQueueSubmit2");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_RenderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        m_Window->WasResized()) {
        m_Window->ResetResizedFlag();
        RecreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        CheckVkResult(presentResult, "vkQueuePresentKHR");
    }

    m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace Kyumist
