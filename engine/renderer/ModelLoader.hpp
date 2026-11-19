#pragma once

#include <glm/glm.hpp>

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

struct LoadedModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    PBRMaterialData primaryMaterial;
    bool hasMaterial = false;
};

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8
};

class ModelLoader {
public:
    static bool LoadModel(const std::filesystem::path& path, LoadedModelData& outData);
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

