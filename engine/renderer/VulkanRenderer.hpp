#pragma once

#include "renderer/ModelLoader.hpp"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kyumist {

class Scene;
class Window;

struct DrawPushConstants {
    glm::mat4 model{1.0f};
    glm::vec4 normalCol0{1.0f, 0.0f, 0.0f, 0.1f}; // xyz = col0, w = metallic
    glm::vec4 normalCol1{0.0f, 1.0f, 0.0f, 0.0f}; // xyz = col1
    glm::vec4 normalCol2{0.0f, 0.0f, 1.0f, 0.0f}; // xyz = col2
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 0.5f};  // rgb = albedo+emissive, a = mode/roughness
};
static_assert(sizeof(DrawPushConstants) == 128, "DrawPushConstants must fit in 128 bytes");

struct SceneLightsUBO {
    glm::mat4 viewProj{1.0f};
    glm::vec4 dirLightDirIntensity{0.45f, 0.80f, 0.50f, 1.0f};
    glm::vec4 dirLightColorSky{1.0f, 0.98f, 0.92f, 0.0f};
    glm::vec4 skyZenith{0.07f, 0.11f, 0.22f, 1.0f};
    glm::vec4 skyHorizon{0.22f, 0.32f, 0.48f, 1.0f};
    glm::vec4 skyGround{0.10f, 0.11f, 0.14f, 1.0f};
    glm::vec4 cameraPosAndPointCount{0.0f, 2.0f, 5.0f, 0.0f};
    glm::vec4 pointLightPosRange[8]{};
    glm::vec4 pointLightColorInt[8]{};
};

class VulkanRenderer {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr uint32_t MAX_BATCH_SPRITES = 2048;

    explicit VulkanRenderer(Window* window, bool headless = false);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void BeginImGuiFrame();
    void EndImGuiFrameAndDraw();
    void DrawFrame();

    void SetActiveScene(const Scene* scene, const glm::mat4& viewProj);
    void RequestSceneResize(uint32_t width, uint32_t height);

    VkDescriptorSet GetSceneTextureDescriptor() const { return m_SceneTextureDescriptor; }
    VkExtent2D GetSceneExtent() const { return m_SceneExtent; }
    const std::string& GetDeviceName() const { return m_DeviceName; }

    glm::vec4 GetClearColor() const { return m_ClearColor; }
    void SetClearColor(const glm::vec4& color) { m_ClearColor = color; }
    void ClearModelCache();

    uint64_t CreateUITextureRGBA(uint32_t width, uint32_t height, const uint8_t* rgbaPixels);
    uint64_t LoadUITextureFile(const std::string& path, uint32_t& outWidth, uint32_t& outHeight);
    void DestroyUITexture(uint64_t handle);

private:
    struct GpuMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    struct GpuTexture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    struct FrameData {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
        VkBuffer spriteBatchBuffer = VK_NULL_HANDLE;
        VmaAllocation spriteBatchAllocation = VK_NULL_HANDLE;
        void* spriteBatchMapped = nullptr;
        VkBuffer lightsBuffer = VK_NULL_HANDLE;
        VmaAllocation lightsAllocation = VK_NULL_HANDLE;
        void* lightsMapped = nullptr;
        VkDescriptorSet lightsDescriptorSet = VK_NULL_HANDLE;
    };

    void InitVulkan();
    void CreateSwapchain();
    void DestroySwapchain();
    void RecreateSwapchain();
    void CreateHeadlessTarget();
    void DestroyHeadlessTarget();
    void CreateSceneTarget(uint32_t width, uint32_t height);
    void DestroySceneTarget();
    void CreateDescriptorResources();
    void CreateGraphicsPipeline();
    void CreateBuiltinMeshes();
    void DestroyMesh(GpuMesh& mesh);
    GpuMesh UploadMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    const GpuMesh* GetOrLoadObjMesh(const std::string& path);

    void CreateDefaultTextures();
    void DestroyTexture(GpuTexture& tex);
    GpuTexture CreateTextureFromRGBA(uint32_t width, uint32_t height, const uint8_t* rgbaPixels);
    VkDescriptorSet GetOrLoadTextureDescriptor(const std::string& texturePath);

    void CreateCommandsAndSync();
    void InitImGui();
    void ShutdownImGui();

    void RecordScenePass(VkCommandBuffer cmd);
    void RecordImGuiPass(
        VkCommandBuffer cmd,
        VkImage targetImage,
        VkImageView targetView,
        VkImageLayout finalLayout
    );
    void RecordCommandBuffer(
        VkCommandBuffer cmd,
        VkImage targetImage,
        VkImageView targetView,
        VkImageLayout finalLayout
    );

    Window* m_Window = nullptr;
    bool m_Headless = false;
    std::string m_DeviceName;
    glm::vec4 m_ClearColor{0.22f, 0.27f, 0.35f, 1.0f};

    const Scene* m_ActiveScene = nullptr;
    glm::mat4 m_ViewProj{1.0f};

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamily = 0;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_Extent{1280, 720};
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainImageViews;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;

    VkImage m_HeadlessImage = VK_NULL_HANDLE;
    VmaAllocation m_HeadlessAllocation = VK_NULL_HANDLE;
    VkImageView m_HeadlessImageView = VK_NULL_HANDLE;

    VkFormat m_SceneFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat m_SceneDepthFormat = VK_FORMAT_D32_SFLOAT;
    VkExtent2D m_SceneExtent{1280, 720};
    VkExtent2D m_RequestedSceneExtent{1280, 720};
    VkImage m_SceneImage = VK_NULL_HANDLE;
    VmaAllocation m_SceneAllocation = VK_NULL_HANDLE;
    VkImageView m_SceneImageView = VK_NULL_HANDLE;
    VkImage m_SceneDepthImage = VK_NULL_HANDLE;
    VmaAllocation m_SceneDepthAllocation = VK_NULL_HANDLE;
    VkImageView m_SceneDepthImageView = VK_NULL_HANDLE;
    VkSampler m_SceneSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_SceneTextureDescriptor = VK_NULL_HANDLE;

    VkDescriptorPool m_SceneDescPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_LightsDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_TextureDescSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

    GpuMesh m_CubeMesh;
    GpuMesh m_SphereMesh;
    GpuMesh m_PlaneMesh;
    std::unordered_map<std::string, GpuMesh> m_ObjMeshes;

    GpuTexture m_WhiteTexture;
    std::unordered_map<std::string, GpuTexture> m_Textures;
    std::unordered_map<uint64_t, GpuTexture> m_UITextures;

    VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;
    bool m_ImGuiInitialized = false;

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_Frames{};
    uint32_t m_CurrentFrame = 0;
};

} // namespace Kyumist
