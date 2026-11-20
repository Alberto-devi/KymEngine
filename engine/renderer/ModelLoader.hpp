#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kyumist {

#ifndef KYUMIST_VERTEX_DEFINED
#define KYUMIST_VERTEX_DEFINED
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};
#endif

struct PBRMaterialData {
    std::string name = "DefaultMaterial";
    glm::vec4 baseColor{0.85f, 0.85f, 0.85f, 1.0f};
    float metallic = 0.1f;
    float roughness = 0.5f;
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};
};

struct SkinVertexWeight {
    glm::ivec4 joints{-1, 0, 0, 0};
    glm::vec4 weights{0.0f, 0.0f, 0.0f, 0.0f};
    int attachedNode = -1;
};

struct SkeletonNode {
    std::string name;
    int parentIndex = -1;
    std::vector<int> children;
    glm::vec3 bindTranslation{0.0f, 0.0f, 0.0f};
    glm::quat bindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bindScale{1.0f, 1.0f, 1.0f};
    bool hasBindMatrix = false;
    glm::mat4 bindMatrix{1.0f};
};

enum class AnimationPathType {
    Translation = 0,
    Rotation = 1,
    Scale = 2
};

enum class AnimationInterpolation {
    Linear = 0,
    Step = 1
};

struct AnimationChannelData {
    int targetNode = -1;
    AnimationPathType path = AnimationPathType::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;
    std::vector<glm::vec4> values;
};

struct AnimationClipData {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannelData> channels;
};

struct SkeletonData {
    std::vector<SkeletonNode> nodes;
    std::vector<int> rootNodes;
    std::vector<int> jointNodeIndices;
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<AnimationClipData> animations;
};

struct LoadedModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    PBRMaterialData primaryMaterial;
    bool hasMaterial = false;

    bool hasSkin = false;
    std::vector<Vertex> bindVertices;
    std::vector<SkinVertexWeight> skinWeights;
    SkeletonData skeleton;
};

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8
};

class ModelLoader {
public:
    static bool LoadModel(const std::filesystem::path& path, LoadedModelData& outData);
    static void EvaluateSkinnedVertices(
        const LoadedModelData& model,
        const std::string& animationName,
        float timeSeconds,
        bool loop,
        bool autoPlayFirst,
        std::vector<Vertex>& outVertices,
        bool* outFinished = nullptr
    );
    static bool LoadMaterialFile(const std::filesystem::path& path, PBRMaterialData& outMat);
    static bool SaveMaterialFile(const std::filesystem::path& path, const PBRMaterialData& mat);

    static bool LoadImageRGBA(const std::filesystem::path& path, ImageData& outImage);
    static bool SavePngRGBA(
        const std::filesystem::path& path,
        uint32_t width,
        uint32_t height,
        const uint8_t* rgbaPixels
    );

    static std::filesystem::path EnsureDefaultSampleAssets(const std::filesystem::path& projectOrAssetsDir);

    static bool ImportExternalAssetToDir(
        const std::filesystem::path& sourceFile,
        const std::filesystem::path& targetDir,
        std::filesystem::path* outImportedPath = nullptr,
        std::string* outError = nullptr
    );
};

} // namespace Kyumist

