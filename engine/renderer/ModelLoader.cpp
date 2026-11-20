#include "renderer/ModelLoader.hpp"

#include "core/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Kyumist {

namespace {

std::string ToLowerExt(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

std::string TrimWhitespace(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string(sv.substr(start, end - start));
}

std::string DecodeUriPath(const std::string& uri) {
    std::string out;
    out.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const auto hexVal = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            const int hi = hexVal(uri[i + 1]);
            const int lo = hexVal(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(uri[i]);
    }
    return out;
}

std::string EncodeBase64(const std::vector<uint8_t>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t b0 = data[i];
        const bool hasB1 = (i + 1 < data.size());
        const bool hasB2 = (i + 2 < data.size());
        const uint32_t b1 = hasB1 ? data[i + 1] : 0u;
        const uint32_t b2 = hasB2 ? data[i + 2] : 0u;

        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(hasB1 ? kAlphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(hasB2 ? kAlphabet[triple & 0x3F] : '=');
    }
    return out;
}

std::vector<uint8_t> DecodeBase64(std::string_view input) {
    int table[256];
    std::fill(std::begin(table), std::end(table), -1);
    const std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(alphabet[static_cast<size_t>(i)])] = i;
    }

    std::vector<uint8_t> out;
    out.reserve((input.size() / 4) * 3);

    uint32_t val = 0;
    int valBits = -8;
    for (char c : input) {
        if (c == '=') {
            break;
        }
        const int d = table[static_cast<unsigned char>(c)];
        if (d == -1) {
            continue;
        }
        val = (val << 6) | static_cast<uint32_t>(d);
        valBits += 6;
        if (valBits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valBits) & 0xFF));
            valBits -= 8;
        }
    }
    return out;
}

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& outBytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    const std::streamsize size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(outBytes.data()), size)) {
        return false;
    }
    return true;
}

bool ParseMtlLibrary(
    const std::filesystem::path& mtlPath,
    std::unordered_map<std::string, PBRMaterialData>& outMaterials,
    std::string* outFirstMatName
) {
    std::ifstream in(mtlPath);
    if (!in.is_open()) {
        return false;
    }

    std::string currentName;
    PBRMaterialData currentMat{};
    bool hasActive = false;
    bool loadedAny = false;

    auto flushCurrent = [&]() {
        if (hasActive && !currentName.empty()) {
            outMaterials[currentName] = currentMat;
            if (outFirstMatName && outFirstMatName->empty()) {
                *outFirstMatName = currentName;
            }
            loadedAny = true;
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if (key.empty() || key[0] == '#') {
            continue;
        }
        if (key == "newmtl") {
            flushCurrent();
            std::string rest;
            std::getline(iss, rest);
            currentName = TrimWhitespace(rest);
            currentMat = PBRMaterialData{};
            currentMat.name = currentName;
            hasActive = !currentName.empty();
        } else if (hasActive && key == "Kd") {
            float r = 0.85f, g = 0.85f, b = 0.85f;
            if (iss >> r >> g >> b) {
                currentMat.baseColor = {r, g, b, currentMat.baseColor.a};
            }
        } else if (hasActive && key == "Ke") {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (iss >> r >> g >> b) {
                currentMat.emissive = {r, g, b};
            }
        } else if (hasActive && (key == "d" || key == "Tr")) {
            float v = 1.0f;
            if (iss >> v) {
                currentMat.baseColor.a = (key == "Tr") ? std::clamp(1.0f - v, 0.0f, 1.0f) : std::clamp(v, 0.0f, 1.0f);
            }
        } else if (hasActive && key == "Pm") {
            float m = 0.0f;
            if (iss >> m) {
                currentMat.metallic = std::clamp(m, 0.0f, 1.0f);
            }
        } else if (hasActive && key == "Pr") {
            float r = 0.5f;
            if (iss >> r) {
                currentMat.roughness = std::clamp(r, 0.05f, 1.0f);
            }
        } else if (hasActive && key == "Ns") {
            float ns = 250.0f;
            if (iss >> ns) {
                const float norm = std::clamp(ns / 1000.0f, 0.0f, 1.0f);
                currentMat.roughness = std::clamp(1.0f - std::sqrt(norm), 0.05f, 1.0f);
            }
        }
    }
    flushCurrent();
    return loadedAny;
}

bool ParseObjFileWithMaterials(const std::filesystem::path& path, LoadedModelData& outData) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    outData.vertices.clear();
    outData.indices.clear();
    outData.hasMaterial = false;
    outData.primaryMaterial = PBRMaterialData{};

    std::unordered_map<std::string, PBRMaterialData> materials;
    std::string firstMtlName;
    const std::filesystem::path autoMtl = path.parent_path() / (path.stem().string() + ".mtl");
    if (std::filesystem::exists(autoMtl)) {
        ParseMtlLibrary(autoMtl, materials, &firstMtlName);
    }

    bool hasActiveMaterial = false;
    PBRMaterialData activeMaterial{};
    bool recordedPrimaryMaterial = false;

    auto resolveIndex = [](int idx, size_t count) -> int {
        if (idx > 0) return idx - 1;
        if (idx < 0) return static_cast<int>(count) + idx;
        return -1;
    };

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        if (prefix.empty() || prefix[0] == '#') {
            continue;
        }

        if (prefix == "mtllib") {
            std::string rest;
            std::getline(iss, rest);
            const std::string mtlFile = TrimWhitespace(rest);
            if (!mtlFile.empty()) {
                ParseMtlLibrary(path.parent_path() / mtlFile, materials, &firstMtlName);
            }
        } else if (prefix == "usemtl") {
            std::string rest;
            std::getline(iss, rest);
            const std::string matName = TrimWhitespace(rest);
            auto it = materials.find(matName);
            if (it != materials.end()) {
                hasActiveMaterial = true;
                activeMaterial = it->second;
                if (!recordedPrimaryMaterial) {
                    outData.primaryMaterial = activeMaterial;
                    outData.hasMaterial = true;
                    recordedPrimaryMaterial = true;
                }
            }
        } else if (prefix == "v") {
            glm::vec3 p{0.0f};
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (prefix == "vn") {
            glm::vec3 n{0.0f, 1.0f, 0.0f};
            iss >> n.x >> n.y >> n.z;
            const float len = glm::length(n);
            normals.push_back(len > 1e-6f ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f));
        } else if (prefix == "vt") {
            glm::vec2 uv{0.0f};
            iss >> uv.x >> uv.y;
            uvs.push_back(uv);
        } else if (prefix == "f") {
            std::vector<Vertex> faceVerts;
            std::string token;
            const glm::vec3 matColor = hasActiveMaterial
                ? glm::clamp(glm::vec3(activeMaterial.baseColor) + activeMaterial.emissive, glm::vec3(0.0f), glm::vec3(2.0f))
                : glm::vec3(1.0f);

            while (iss >> token) {
                int vIdx = 0;
                int vtIdx = 0;
                int vnIdx = 0;
                const size_t s1 = token.find('/');
                if (s1 == std::string::npos) {
                    vIdx = std::stoi(token);
                } else {
                    vIdx = std::stoi(token.substr(0, s1));
                    const size_t s2 = token.find('/', s1 + 1);
                    if (s2 == std::string::npos) {
                        if (s1 + 1 < token.size()) {
                            vtIdx = std::stoi(token.substr(s1 + 1));
                        }
                    } else {
                        if (s2 > s1 + 1) {
                            vtIdx = std::stoi(token.substr(s1 + 1, s2 - s1 - 1));
                        }
                        if (s2 + 1 < token.size()) {
                            vnIdx = std::stoi(token.substr(s2 + 1));
                        }
                    }
                }

                const int rp = resolveIndex(vIdx, positions.size());
                if (rp < 0 || static_cast<size_t>(rp) >= positions.size()) {
                    continue;
                }
                const int rt = resolveIndex(vtIdx, uvs.size());
                const int rn = resolveIndex(vnIdx, normals.size());

                Vertex v{};
                v.position = positions[static_cast<size_t>(rp)];
                v.normal = (rn >= 0 && static_cast<size_t>(rn) < normals.size())
                    ? normals[static_cast<size_t>(rn)]
                    : glm::vec3(0.0f, 1.0f, 0.0f);
                v.color = matColor;
                v.uv = (rt >= 0 && static_cast<size_t>(rt) < uvs.size())
                    ? uvs[static_cast<size_t>(rt)]
                    : glm::vec2(0.0f, 0.0f);
                faceVerts.push_back(v);
            }

            if (faceVerts.size() >= 3) {
                for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                    Vertex v0 = faceVerts[0];
                    Vertex v1 = faceVerts[i];
                    Vertex v2 = faceVerts[i + 1];
                    if (normals.empty()) {
                        const glm::vec3 e1 = v1.position - v0.position;
                        const glm::vec3 e2 = v2.position - v0.position;
                        const glm::vec3 crossN = glm::cross(e1, e2);
                        const glm::vec3 flatN = glm::length(crossN) > 0.00001f
                            ? glm::normalize(crossN)
                            : glm::vec3(0.0f, 1.0f, 0.0f);
                        v0.normal = flatN;
                        v1.normal = flatN;
                        v2.normal = flatN;
                    }
                    const uint32_t base = static_cast<uint32_t>(outData.vertices.size());
                    outData.vertices.push_back(v0);
                    outData.vertices.push_back(v1);
                    outData.vertices.push_back(v2);
                    outData.indices.push_back(base);
                    outData.indices.push_back(base + 1);
                    outData.indices.push_back(base + 2);
                }
            }
        }
    }

    if (!recordedPrimaryMaterial && !firstMtlName.empty()) {
        auto it = materials.find(firstMtlName);
        if (it != materials.end()) {
            outData.primaryMaterial = it->second;
            outData.hasMaterial = true;
        }
    }

    return !outData.vertices.empty() && !outData.indices.empty();
}

int ComponentTypeSizeBytes(int componentType) {
    switch (componentType) {
        case 5120: // BYTE
        case 5121: // UNSIGNED_BYTE
            return 1;
        case 5122: // SHORT
        case 5123: // UNSIGNED_SHORT
            return 2;
        case 5125: // UNSIGNED_INT
        case 5126: // FLOAT
            return 4;
        default:
            return 0;
    }
}

int TypeComponentCount(std::string_view typeStr) {
    if (typeStr == "SCALAR") return 1;
    if (typeStr == "VEC2") return 2;
    if (typeStr == "VEC3") return 3;
    if (typeStr == "VEC4") return 4;
    if (typeStr == "MAT2") return 4;
    if (typeStr == "MAT3") return 9;
    if (typeStr == "MAT4") return 16;
    return 0;
}

float ReadGltfComponentAsFloat(const uint8_t* ptr, int componentType, bool normalized) {
    switch (componentType) {
        case 5126: { // FLOAT
            float f = 0.0f;
            std::memcpy(&f, ptr, sizeof(float));
            return f;
        }
        case 5120: { // BYTE
            int8_t v = 0;
            std::memcpy(&v, ptr, sizeof(int8_t));
            return normalized ? std::max(-1.0f, static_cast<float>(v) / 127.0f) : static_cast<float>(v);
        }
        case 5121: { // UNSIGNED_BYTE
            uint8_t v = *ptr;
            return normalized ? (static_cast<float>(v) / 255.0f) : static_cast<float>(v);
        }
        case 5122: { // SHORT
            int16_t v = 0;
            std::memcpy(&v, ptr, sizeof(int16_t));
            return normalized ? std::max(-1.0f, static_cast<float>(v) / 32767.0f) : static_cast<float>(v);
        }
        case 5123: { // UNSIGNED_SHORT
            uint16_t v = 0;
            std::memcpy(&v, ptr, sizeof(uint16_t));
            return normalized ? (static_cast<float>(v) / 65535.0f) : static_cast<float>(v);
        }
        case 5125: { // UNSIGNED_INT
            uint32_t v = 0;
            std::memcpy(&v, ptr, sizeof(uint32_t));
            return static_cast<float>(v);
        }
        default:
            return 0.0f;
    }
}

uint32_t ReadGltfIndex(const uint8_t* ptr, int componentType) {
    switch (componentType) {
        case 5121:
            return static_cast<uint32_t>(*ptr);
        case 5123: {
            uint16_t v = 0;
            std::memcpy(&v, ptr, sizeof(uint16_t));
            return static_cast<uint32_t>(v);
        }
        case 5125: {
            uint32_t v = 0;
            std::memcpy(&v, ptr, sizeof(uint32_t));
            return v;
        }
        default:
            return 0;
    }
}

struct GltfAccessorView {
    const uint8_t* basePtr = nullptr;
    size_t count = 0;
    size_t stride = 0;
    int componentType = 0;
    int componentCount = 0;
    bool normalized = false;

    bool IsValid() const {
        return basePtr != nullptr && count > 0 && componentCount > 0;
    }

    glm::vec4 GetVec4(size_t index, const glm::vec4& fallback = glm::vec4(0.0f)) const {
        if (!IsValid() || index >= count) {
            return fallback;
        }
        glm::vec4 result = fallback;
        const size_t compSize = static_cast<size_t>(ComponentTypeSizeBytes(componentType));
        const uint8_t* elem = basePtr + index * stride;
        const int readCount = std::min(componentCount, 4);
        for (int c = 0; c < readCount; ++c) {
            result[c] = ReadGltfComponentAsFloat(elem + static_cast<size_t>(c) * compSize, componentType, normalized);
        }
        return result;
    }

    uint32_t GetIndex(size_t index) const {
        if (!IsValid() || index >= count) {
            return 0;
        }
        return ReadGltfIndex(basePtr + index * stride, componentType);
    }

    float GetScalar(size_t index, float fallback = 0.0f) const {
        if (!IsValid() || index >= count) {
            return fallback;
        }
        return ReadGltfComponentAsFloat(basePtr + index * stride, componentType, normalized);
    }

    glm::mat4 GetMat4(size_t index, const glm::mat4& fallback = glm::mat4(1.0f)) const {
        if (!IsValid() || index >= count || componentCount < 16) {
            return fallback;
        }
        glm::mat4 mat(1.0f);
        const size_t compSize = static_cast<size_t>(ComponentTypeSizeBytes(componentType));
        const uint8_t* elem = basePtr + index * stride;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                const int idx = col * 4 + row;
                mat[col][row] = ReadGltfComponentAsFloat(elem + static_cast<size_t>(idx) * compSize, componentType, normalized);
            }
        }
        return mat;
    }
};

GltfAccessorView ResolveGltfAccessor(
    const nlohmann::json& gltf,
    const std::vector<std::vector<uint8_t>>& buffers,
    int accessorIndex
) {
    GltfAccessorView view{};
    if (accessorIndex < 0 || !gltf.contains("accessors") || !gltf["accessors"].is_array()) {
        return view;
    }
    const auto& accessors = gltf["accessors"];
    if (static_cast<size_t>(accessorIndex) >= accessors.size()) {
        return view;
    }

    const auto& acc = accessors[static_cast<size_t>(accessorIndex)];
    const int bvIdx = acc.value("bufferView", -1);
    if (bvIdx < 0 || !gltf.contains("bufferViews") || !gltf["bufferViews"].is_array()) {
        return view;
    }
    const auto& bufferViews = gltf["bufferViews"];
    if (static_cast<size_t>(bvIdx) >= bufferViews.size()) {
        return view;
    }

    const auto& bv = bufferViews[static_cast<size_t>(bvIdx)];
    const int bufIdx = bv.value("buffer", -1);
    if (bufIdx < 0 || static_cast<size_t>(bufIdx) >= buffers.size()) {
        return view;
    }

    const std::vector<uint8_t>& buf = buffers[static_cast<size_t>(bufIdx)];
    const size_t bvOffset = bv.value("byteOffset", size_t{0});
    const size_t accOffset = acc.value("byteOffset", size_t{0});
    const size_t count = acc.value("count", size_t{0});
    const int compType = acc.value("componentType", 0);
    const std::string typeStr = acc.value("type", "");
    const int compCount = TypeComponentCount(typeStr);
    const int compSize = ComponentTypeSizeBytes(compType);
    if (count == 0 || compCount <= 0 || compSize <= 0) {
        return view;
    }

    const size_t tightlyPackedStride = static_cast<size_t>(compCount) * static_cast<size_t>(compSize);
    const size_t stride = bv.contains("byteStride") && bv["byteStride"].get<size_t>() > 0
        ? bv["byteStride"].get<size_t>()
        : tightlyPackedStride;

    const size_t startOffset = bvOffset + accOffset;
    const size_t requiredEnd = startOffset + (count - 1) * stride + tightlyPackedStride;
    if (requiredEnd > buf.size()) {
        return view;
    }

    view.basePtr = buf.data() + startOffset;
    view.count = count;
    view.stride = stride;
    view.componentType = compType;
    view.componentCount = compCount;
    view.normalized = acc.value("normalized", false);
    return view;
}

glm::mat4 ComputeNodeLocalMatrix(const nlohmann::json& node) {
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16) {
        glm::mat4 m(1.0f);
        const auto& arr = node["matrix"];
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                m[col][row] = arr[static_cast<size_t>(col * 4 + row)].get<float>();
            }
        }
        return m;
    }

    glm::vec3 translation(0.0f);
    if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() == 3) {
        translation = {
            node["translation"][0].get<float>(),
            node["translation"][1].get<float>(),
            node["translation"][2].get<float>()
        };
    }

    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() == 4) {
        // glTF quaternion order is [x, y, z, w], glm::quat constructor takes (w, x, y, z)
        rotation = glm::normalize(glm::quat(
            node["rotation"][3].get<float>(),
            node["rotation"][0].get<float>(),
            node["rotation"][1].get<float>(),
            node["rotation"][2].get<float>()
        ));
    }

    glm::vec3 scale(1.0f);
    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() == 3) {
        scale = {
            node["scale"][0].get<float>(),
            node["scale"][1].get<float>(),
            node["scale"][2].get<float>()
        };
    }

    return glm::translate(glm::mat4(1.0f), translation) *
           glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

bool ParseGltfOrGlb(const std::filesystem::path& path, bool isGlb, LoadedModelData& outData) {
    outData.vertices.clear();
    outData.indices.clear();
    outData.hasMaterial = false;
    outData.primaryMaterial = PBRMaterialData{};
    outData.hasSkin = false;
    outData.bindVertices.clear();
    outData.skinWeights.clear();
    outData.skeleton = SkeletonData{};

    nlohmann::json gltf;
    std::vector<uint8_t> glbBinChunk;

    if (isGlb) {
        std::vector<uint8_t> fileBytes;
        if (!ReadBinaryFile(path, fileBytes) || fileBytes.size() < 12) {
            return false;
        }
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t totalLen = 0;
        std::memcpy(&magic, fileBytes.data() + 0, 4);
        std::memcpy(&version, fileBytes.data() + 4, 4);
        std::memcpy(&totalLen, fileBytes.data() + 8, 4);
        if (magic != 0x46546C67u || version != 2u || totalLen > fileBytes.size()) {
            return false;
        }

        bool foundJson = false;
        size_t offset = 12;
        while (offset + 8 <= totalLen) {
            uint32_t chunkLen = 0;
            uint32_t chunkType = 0;
            std::memcpy(&chunkLen, fileBytes.data() + offset, 4);
            std::memcpy(&chunkType, fileBytes.data() + offset + 4, 4);
            offset += 8;
            if (offset + chunkLen > totalLen) {
                break;
            }
            if (chunkType == 0x4E4F534Au && !foundJson) {
                const std::string jsonStr(
                    reinterpret_cast<const char*>(fileBytes.data() + offset),
                    static_cast<size_t>(chunkLen)
                );
                try {
                    gltf = nlohmann::json::parse(jsonStr);
                    foundJson = true;
                } catch (const std::exception&) {
                    return false;
                }
            } else if (chunkType == 0x004E4942u && glbBinChunk.empty()) {
                glbBinChunk.assign(
                    fileBytes.data() + offset,
                    fileBytes.data() + offset + chunkLen
                );
            }
            offset += chunkLen;
        }
        if (!foundJson) {
            return false;
        }
    } else {
        std::ifstream in(path);
        if (!in.is_open()) {
            return false;
        }
        try {
            in >> gltf;
        } catch (const std::exception&) {
            return false;
        }
    }

    // Load buffers
    std::vector<std::vector<uint8_t>> buffers;
    if (gltf.contains("buffers") && gltf["buffers"].is_array()) {
        const auto& bufArr = gltf["buffers"];
        buffers.resize(bufArr.size());
        for (size_t i = 0; i < bufArr.size(); ++i) {
            const auto& bJson = bufArr[i];
            if (bJson.contains("uri") && bJson["uri"].is_string()) {
                const std::string uri = bJson["uri"].get<std::string>();
                if (uri.rfind("data:", 0) == 0) {
                    const size_t b64Marker = uri.find(";base64,");
                    if (b64Marker != std::string::npos) {
                        buffers[i] = DecodeBase64(std::string_view(uri).substr(b64Marker + 8));
                    }
                } else {
                    const std::filesystem::path binPath = path.parent_path() / DecodeUriPath(uri);
                    ReadBinaryFile(binPath, buffers[i]);
                }
            } else if (isGlb && i == 0 && !glbBinChunk.empty()) {
                buffers[i] = std::move(glbBinChunk);
            }
        }
    }

    // Parse materials
    std::vector<PBRMaterialData> materials;
    if (gltf.contains("materials") && gltf["materials"].is_array()) {
        for (const auto& matJson : gltf["materials"]) {
            PBRMaterialData mat;
            mat.name = matJson.value("name", "glTF_Material");
            mat.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            mat.metallic = 0.0f;
            mat.roughness = 0.5f;

            if (matJson.contains("pbrMetallicRoughness")) {
                const auto& pbr = matJson["pbrMetallicRoughness"];
                if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() == 4) {
                    mat.baseColor = {
                        pbr["baseColorFactor"][0].get<float>(),
                        pbr["baseColorFactor"][1].get<float>(),
                        pbr["baseColorFactor"][2].get<float>(),
                        pbr["baseColorFactor"][3].get<float>()
                    };
                }
                mat.metallic = std::clamp(pbr.value("metallicFactor", 0.2f), 0.0f, 1.0f);
                mat.roughness = std::clamp(pbr.value("roughnessFactor", 0.5f), 0.05f, 1.0f);
            }
            if (matJson.contains("emissiveFactor") && matJson["emissiveFactor"].is_array() && matJson["emissiveFactor"].size() == 3) {
                mat.emissive = {
                    matJson["emissiveFactor"][0].get<float>(),
                    matJson["emissiveFactor"][1].get<float>(),
                    matJson["emissiveFactor"][2].get<float>()
                };
            }
            materials.push_back(std::move(mat));
        }
    }

    if (!gltf.contains("meshes") || !gltf["meshes"].is_array()) {
        return false;
    }
    const auto& meshes = gltf["meshes"];

    // Parse skeleton nodes, skin, and animations if present
    if (gltf.contains("nodes") && gltf["nodes"].is_array() && !gltf["nodes"].empty()) {
        const auto& nodesJson = gltf["nodes"];
        outData.skeleton.nodes.resize(nodesJson.size());
        for (size_t i = 0; i < nodesJson.size(); ++i) {
            const auto& n = nodesJson[i];
            auto& sn = outData.skeleton.nodes[i];
            sn.name = n.value("name", "");
            if (n.contains("matrix") && n["matrix"].is_array() && n["matrix"].size() == 16) {
                sn.hasBindMatrix = true;
                sn.bindMatrix = ComputeNodeLocalMatrix(n);
            } else {
                if (n.contains("translation") && n["translation"].is_array() && n["translation"].size() == 3) {
                    sn.bindTranslation = {
                        n["translation"][0].get<float>(),
                        n["translation"][1].get<float>(),
                        n["translation"][2].get<float>()
                    };
                }
                if (n.contains("rotation") && n["rotation"].is_array() && n["rotation"].size() == 4) {
                    sn.bindRotation = glm::normalize(glm::quat(
                        n["rotation"][3].get<float>(),
                        n["rotation"][0].get<float>(),
                        n["rotation"][1].get<float>(),
                        n["rotation"][2].get<float>()
                    ));
                }
                if (n.contains("scale") && n["scale"].is_array() && n["scale"].size() == 3) {
                    sn.bindScale = {
                        n["scale"][0].get<float>(),
                        n["scale"][1].get<float>(),
                        n["scale"][2].get<float>()
                    };
                }
            }
            if (n.contains("children") && n["children"].is_array()) {
                for (const auto& c : n["children"]) {
                    const int cIdx = c.get<int>();
                    if (cIdx >= 0 && static_cast<size_t>(cIdx) < nodesJson.size()) {
                        sn.children.push_back(cIdx);
                    }
                }
            }
        }
        for (size_t i = 0; i < outData.skeleton.nodes.size(); ++i) {
            for (int cIdx : outData.skeleton.nodes[i].children) {
                outData.skeleton.nodes[static_cast<size_t>(cIdx)].parentIndex = static_cast<int>(i);
            }
        }
        if (gltf.contains("scenes") && gltf["scenes"].is_array() && !gltf["scenes"].empty()) {
            const int defaultScene = std::clamp(
                gltf.value("scene", 0),
                0,
                static_cast<int>(gltf["scenes"].size()) - 1
            );
            const auto& sceneJson = gltf["scenes"][static_cast<size_t>(defaultScene)];
            if (sceneJson.contains("nodes") && sceneJson["nodes"].is_array()) {
                for (const auto& n : sceneJson["nodes"]) {
                    const int idx = n.get<int>();
                    if (idx >= 0 && static_cast<size_t>(idx) < outData.skeleton.nodes.size()) {
                        outData.skeleton.rootNodes.push_back(idx);
                    }
                }
            }
        }
        if (outData.skeleton.rootNodes.empty()) {
            for (size_t i = 0; i < outData.skeleton.nodes.size(); ++i) {
                if (outData.skeleton.nodes[i].parentIndex < 0) {
                    outData.skeleton.rootNodes.push_back(static_cast<int>(i));
                }
            }
        }
    }

    if (gltf.contains("skins") && gltf["skins"].is_array() && !gltf["skins"].empty()) {
        const auto& skinJson = gltf["skins"][0];
        if (skinJson.contains("joints") && skinJson["joints"].is_array()) {
            for (const auto& j : skinJson["joints"]) {
                outData.skeleton.jointNodeIndices.push_back(j.get<int>());
            }
        }
        const size_t jointCount = outData.skeleton.jointNodeIndices.size();
        outData.skeleton.inverseBindMatrices.assign(jointCount, glm::mat4(1.0f));
        if (skinJson.contains("inverseBindMatrices") && skinJson["inverseBindMatrices"].is_number_integer()) {
            const GltfAccessorView ibmView = ResolveGltfAccessor(
                gltf, buffers, skinJson["inverseBindMatrices"].get<int>()
            );
            if (ibmView.IsValid()) {
                const size_t copyCount = std::min(jointCount, ibmView.count);
                for (size_t j = 0; j < copyCount; ++j) {
                    outData.skeleton.inverseBindMatrices[j] = ibmView.GetMat4(j);
                }
            }
        }
        outData.hasSkin = !outData.skeleton.jointNodeIndices.empty() && !outData.skeleton.nodes.empty();
    }

    if (outData.hasSkin && gltf.contains("animations") && gltf["animations"].is_array()) {
        const auto& animsJson = gltf["animations"];
        for (size_t aIdx = 0; aIdx < animsJson.size(); ++aIdx) {
            const auto& aJson = animsJson[aIdx];
            if (!aJson.contains("channels") || !aJson["channels"].is_array() ||
                !aJson.contains("samplers") || !aJson["samplers"].is_array()) {
                continue;
            }
            AnimationClipData clip;
            clip.name = aJson.value("name", "Animation_" + std::to_string(aIdx));
            const auto& samplersJson = aJson["samplers"];

            for (const auto& chJson : aJson["channels"]) {
                if (!chJson.contains("target") || !chJson.contains("sampler")) {
                    continue;
                }
                const auto& target = chJson["target"];
                if (!target.contains("node") || !target.contains("path")) {
                    continue;
                }
                const int targetNode = target["node"].get<int>();
                const std::string pathStr = target["path"].get<std::string>();
                AnimationPathType pathType = AnimationPathType::Translation;
                if (pathStr == "translation") {
                    pathType = AnimationPathType::Translation;
                } else if (pathStr == "rotation") {
                    pathType = AnimationPathType::Rotation;
                } else if (pathStr == "scale") {
                    pathType = AnimationPathType::Scale;
                } else {
                    continue;
                }

                const int samplerIdx = chJson["sampler"].get<int>();
                if (samplerIdx < 0 || static_cast<size_t>(samplerIdx) >= samplersJson.size()) {
                    continue;
                }
                const auto& sampJson = samplersJson[static_cast<size_t>(samplerIdx)];
                if (!sampJson.contains("input") || !sampJson.contains("output")) {
                    continue;
                }
                const GltfAccessorView inView = ResolveGltfAccessor(gltf, buffers, sampJson["input"].get<int>());
                const GltfAccessorView outView = ResolveGltfAccessor(gltf, buffers, sampJson["output"].get<int>());
                if (!inView.IsValid() || !outView.IsValid()) {
                    continue;
                }

                const std::string interpStr = sampJson.value("interpolation", "LINEAR");
                const bool isCubic = (interpStr == "CUBICSPLINE");
                AnimationChannelData ch;
                ch.targetNode = targetNode;
                ch.path = pathType;
                ch.interpolation = (interpStr == "STEP") ? AnimationInterpolation::Step : AnimationInterpolation::Linear;

                const size_t keyCount = inView.count;
                ch.times.resize(keyCount);
                ch.values.resize(keyCount);
                for (size_t k = 0; k < keyCount; ++k) {
                    ch.times[k] = inView.GetScalar(k, 0.0f);
                    const size_t outIdx = isCubic ? (k * 3 + 1) : k;
                    ch.values[k] = outView.GetVec4(outIdx, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                }
                if (!ch.times.empty()) {
                    clip.duration = std::max(clip.duration, ch.times.back());
                    clip.channels.push_back(std::move(ch));
                }
            }

            if (!clip.channels.empty()) {
                outData.skeleton.animations.push_back(std::move(clip));
            }
        }
    }

    auto appendMeshWithTransform = [&](int meshIndex, const glm::mat4& worldMatrix, bool isSkinnedNode, int nodeIdx) {
        if (meshIndex < 0 || static_cast<size_t>(meshIndex) >= meshes.size()) {
            return;
        }
        const auto& meshJson = meshes[static_cast<size_t>(meshIndex)];
        if (!meshJson.contains("primitives") || !meshJson["primitives"].is_array()) {
            return;
        }

        const glm::mat3 linearMat(worldMatrix);
        const glm::mat3 normalMat = (std::abs(glm::determinant(linearMat)) > 1e-6f)
            ? glm::transpose(glm::inverse(linearMat))
            : glm::mat3(1.0f);

        for (const auto& prim : meshJson["primitives"]) {
            const int mode = prim.value("mode", 4);
            if (mode != 4 && mode != 5 && mode != 6) {
                continue;
            }
            if (!prim.contains("attributes") || !prim["attributes"].is_object()) {
                continue;
            }
            const auto& attrs = prim["attributes"];
            if (!attrs.contains("POSITION")) {
                continue;
            }

            const GltfAccessorView posView = ResolveGltfAccessor(gltf, buffers, attrs["POSITION"].get<int>());
            if (!posView.IsValid()) {
                continue;
            }

            const GltfAccessorView normView = attrs.contains("NORMAL")
                ? ResolveGltfAccessor(gltf, buffers, attrs["NORMAL"].get<int>())
                : GltfAccessorView{};
            const GltfAccessorView uvView = attrs.contains("TEXCOORD_0")
                ? ResolveGltfAccessor(gltf, buffers, attrs["TEXCOORD_0"].get<int>())
                : GltfAccessorView{};
            const GltfAccessorView colView = attrs.contains("COLOR_0")
                ? ResolveGltfAccessor(gltf, buffers, attrs["COLOR_0"].get<int>())
                : GltfAccessorView{};
            const GltfAccessorView jointsView = (outData.hasSkin && isSkinnedNode && attrs.contains("JOINTS_0"))
                ? ResolveGltfAccessor(gltf, buffers, attrs["JOINTS_0"].get<int>())
                : GltfAccessorView{};
            const GltfAccessorView weightsView = (outData.hasSkin && isSkinnedNode && attrs.contains("WEIGHTS_0"))
                ? ResolveGltfAccessor(gltf, buffers, attrs["WEIGHTS_0"].get<int>())
                : GltfAccessorView{};
            const bool primSkinned = jointsView.IsValid() && weightsView.IsValid();

            PBRMaterialData mat{};
            mat.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            const int matIndex = prim.value("material", -1);
            if (matIndex >= 0 && static_cast<size_t>(matIndex) < materials.size()) {
                mat = materials[static_cast<size_t>(matIndex)];
                if (!outData.hasMaterial) {
                    outData.primaryMaterial = mat;
                    outData.hasMaterial = true;
                }
            }
            const glm::vec3 matTint = glm::clamp(glm::vec3(mat.baseColor) + mat.emissive, glm::vec3(0.0f), glm::vec3(2.0f));

            std::vector<Vertex> primVerts(posView.count);
            std::vector<Vertex> bindVerts(outData.hasSkin ? posView.count : 0);
            std::vector<SkinVertexWeight> primWeights(outData.hasSkin ? posView.count : 0);
            for (size_t i = 0; i < posView.count; ++i) {
                const glm::vec4 p4 = posView.GetVec4(i);
                const glm::vec3 worldPos = glm::vec3(worldMatrix * glm::vec4(p4.x, p4.y, p4.z, 1.0f));

                glm::vec3 rawN(0.0f, 1.0f, 0.0f);
                glm::vec3 worldN(0.0f, 1.0f, 0.0f);
                if (normView.IsValid()) {
                    const glm::vec4 n4 = normView.GetVec4(i, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
                    const float rawLen = glm::length(glm::vec3(n4.x, n4.y, n4.z));
                    if (rawLen > 1e-6f) {
                        rawN = glm::vec3(n4.x, n4.y, n4.z) / rawLen;
                    }
                    const glm::vec3 transformed = normalMat * glm::vec3(n4.x, n4.y, n4.z);
                    const float len = glm::length(transformed);
                    if (len > 1e-6f) {
                        worldN = transformed / len;
                    }
                }

                glm::vec2 uv(0.0f, 0.0f);
                if (uvView.IsValid()) {
                    const glm::vec4 uv4 = uvView.GetVec4(i);
                    uv = {uv4.x, uv4.y};
                }

                glm::vec3 vertColor = matTint;
                if (colView.IsValid()) {
                    const glm::vec4 c4 = colView.GetVec4(i, glm::vec4(1.0f));
                    vertColor *= glm::vec3(c4.x, c4.y, c4.z);
                }

                primVerts[i] = {worldPos, worldN, vertColor, uv};
                if (outData.hasSkin) {
                    if (primSkinned) {
                        bindVerts[i] = {glm::vec3(p4.x, p4.y, p4.z), rawN, vertColor, uv};
                        const glm::vec4 j4 = jointsView.GetVec4(i, glm::vec4(0.0f));
                        glm::vec4 w4 = weightsView.GetVec4(i, glm::vec4(0.0f));
                        const float wSum = w4.x + w4.y + w4.z + w4.w;
                        if (wSum > 1e-5f) {
                            w4 /= wSum;
                        } else {
                            w4 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                        }
                        primWeights[i].joints = {
                            static_cast<int>(std::lround(j4.x)),
                            static_cast<int>(std::lround(j4.y)),
                            static_cast<int>(std::lround(j4.z)),
                            static_cast<int>(std::lround(j4.w))
                        };
                        primWeights[i].weights = w4;
                    } else {
                        bindVerts[i] = {glm::vec3(p4.x, p4.y, p4.z), rawN, vertColor, uv};
                        primWeights[i] = SkinVertexWeight{};
                        primWeights[i].attachedNode = nodeIdx;
                    }
                }
            }

            std::vector<uint32_t> rawIndices;
            if (prim.contains("indices")) {
                const GltfAccessorView idxView = ResolveGltfAccessor(gltf, buffers, prim["indices"].get<int>());
                if (idxView.IsValid()) {
                    rawIndices.resize(idxView.count);
                    for (size_t i = 0; i < idxView.count; ++i) {
                        rawIndices[i] = idxView.GetIndex(i);
                    }
                }
            }
            if (rawIndices.empty()) {
                rawIndices.resize(primVerts.size());
                for (size_t i = 0; i < primVerts.size(); ++i) {
                    rawIndices[i] = static_cast<uint32_t>(i);
                }
            }

            // Convert mode to triangles list
            std::vector<uint32_t> triIndices;
            if (mode == 4) {
                triIndices = std::move(rawIndices);
            } else if (mode == 5 && rawIndices.size() >= 3) { // TRIANGLE_STRIP
                for (size_t i = 0; i + 2 < rawIndices.size(); ++i) {
                    if ((i % 2) == 0) {
                        triIndices.push_back(rawIndices[i]);
                        triIndices.push_back(rawIndices[i + 1]);
                        triIndices.push_back(rawIndices[i + 2]);
                    } else {
                        triIndices.push_back(rawIndices[i + 1]);
                        triIndices.push_back(rawIndices[i]);
                        triIndices.push_back(rawIndices[i + 2]);
                    }
                }
            } else if (mode == 6 && rawIndices.size() >= 3) { // TRIANGLE_FAN
                for (size_t i = 1; i + 1 < rawIndices.size(); ++i) {
                    triIndices.push_back(rawIndices[0]);
                    triIndices.push_back(rawIndices[i]);
                    triIndices.push_back(rawIndices[i + 1]);
                }
            }

            if (normView.IsValid()) {
                const uint32_t vertexBase = static_cast<uint32_t>(outData.vertices.size());
                outData.vertices.insert(outData.vertices.end(), primVerts.begin(), primVerts.end());
                if (outData.hasSkin) {
                    outData.bindVertices.insert(outData.bindVertices.end(), bindVerts.begin(), bindVerts.end());
                    outData.skinWeights.insert(outData.skinWeights.end(), primWeights.begin(), primWeights.end());
                }
                for (uint32_t idx : triIndices) {
                    if (idx < primVerts.size()) {
                        outData.indices.push_back(vertexBase + idx);
                    }
                }
            } else {
                // Compute flat geometric normals per triangle
                for (size_t i = 0; i + 2 < triIndices.size(); i += 3) {
                    const uint32_t i0 = triIndices[i];
                    const uint32_t i1 = triIndices[i + 1];
                    const uint32_t i2 = triIndices[i + 2];
                    if (i0 >= primVerts.size() || i1 >= primVerts.size() || i2 >= primVerts.size()) {
                        continue;
                    }
                    Vertex v0 = primVerts[i0];
                    Vertex v1 = primVerts[i1];
                    Vertex v2 = primVerts[i2];
                    const glm::vec3 crossN = glm::cross(v1.position - v0.position, v2.position - v0.position);
                    const glm::vec3 flatN = glm::length(crossN) > 1e-5f
                        ? glm::normalize(crossN)
                        : glm::vec3(0.0f, 1.0f, 0.0f);
                    v0.normal = v1.normal = v2.normal = flatN;
                    const uint32_t base = static_cast<uint32_t>(outData.vertices.size());
                    outData.vertices.push_back(v0);
                    outData.vertices.push_back(v1);
                    outData.vertices.push_back(v2);
                    if (outData.hasSkin) {
                        Vertex bv0 = bindVerts[i0];
                        Vertex bv1 = bindVerts[i1];
                        Vertex bv2 = bindVerts[i2];
                        const glm::vec3 bCross = glm::cross(bv1.position - bv0.position, bv2.position - bv0.position);
                        const glm::vec3 bFlat = glm::length(bCross) > 1e-5f
                            ? glm::normalize(bCross)
                            : glm::vec3(0.0f, 1.0f, 0.0f);
                        bv0.normal = bv1.normal = bv2.normal = bFlat;
                        outData.bindVertices.push_back(bv0);
                        outData.bindVertices.push_back(bv1);
                        outData.bindVertices.push_back(bv2);
                        outData.skinWeights.push_back(primWeights[i0]);
                        outData.skinWeights.push_back(primWeights[i1]);
                        outData.skinWeights.push_back(primWeights[i2]);
                    }
                    outData.indices.push_back(base);
                    outData.indices.push_back(base + 1);
                    outData.indices.push_back(base + 2);
                }
            }
        }
    };

    if (gltf.contains("nodes") && gltf["nodes"].is_array() && !gltf["nodes"].empty()) {
        const auto& nodes = gltf["nodes"];
        std::vector<int> rootNodes;

        if (gltf.contains("scenes") && gltf["scenes"].is_array() && !gltf["scenes"].empty()) {
            const int defaultScene = std::clamp(
                gltf.value("scene", 0),
                0,
                static_cast<int>(gltf["scenes"].size()) - 1
            );
            const auto& sceneJson = gltf["scenes"][static_cast<size_t>(defaultScene)];
            if (sceneJson.contains("nodes") && sceneJson["nodes"].is_array()) {
                for (const auto& n : sceneJson["nodes"]) {
                    rootNodes.push_back(n.get<int>());
                }
            }
        }

        if (rootNodes.empty()) {
            std::vector<bool> hasParent(nodes.size(), false);
            for (const auto& n : nodes) {
                if (n.contains("children") && n["children"].is_array()) {
                    for (const auto& child : n["children"]) {
                        const int cIdx = child.get<int>();
                        if (cIdx >= 0 && static_cast<size_t>(cIdx) < hasParent.size()) {
                            hasParent[static_cast<size_t>(cIdx)] = true;
                        }
                    }
                }
            }
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (!hasParent[i]) {
                    rootNodes.push_back(static_cast<int>(i));
                }
            }
        }

        auto traverseNode = [&](auto& self, int nodeIdx, const glm::mat4& parentTransform) -> void {
            if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size()) {
                return;
            }
            const auto& node = nodes[static_cast<size_t>(nodeIdx)];
            const glm::mat4 worldTransform = parentTransform * ComputeNodeLocalMatrix(node);
            if (node.contains("mesh") && node["mesh"].is_number_integer()) {
                const bool nodeHasSkin = node.contains("skin") && node["skin"].is_number_integer();
                appendMeshWithTransform(node["mesh"].get<int>(), worldTransform, nodeHasSkin, nodeIdx);
            }
            if (node.contains("children") && node["children"].is_array()) {
                for (const auto& childIdx : node["children"]) {
                    self(self, childIdx.get<int>(), worldTransform);
                }
            }
        };

        for (int rootIdx : rootNodes) {
            traverseNode(traverseNode, rootIdx, glm::mat4(1.0f));
        }
    } else {
        for (size_t m = 0; m < meshes.size(); ++m) {
            appendMeshWithTransform(static_cast<int>(m), glm::mat4(1.0f), false, -1);
        }
    }

    if (!outData.hasMaterial && !materials.empty()) {
        outData.primaryMaterial = materials.front();
        outData.hasMaterial = true;
    }

    return !outData.vertices.empty() && !outData.indices.empty();
}

void AppendBoxGeometry(
    std::vector<uint8_t>& outBin,
    std::vector<nlohmann::json>& outBufferViews,
    std::vector<nlohmann::json>& outAccessors,
    int& outPosAccessor,
    int& outIdxAccessor,
    float hx,
    float hy,
    float hz
) {
    const float corners[8][3] = {
        {-hx, -hy,  hz},
        { hx, -hy,  hz},
        { hx,  hy,  hz},
        {-hx,  hy,  hz},
        {-hx, -hy, -hz},
        { hx, -hy, -hz},
        { hx,  hy, -hz},
        {-hx,  hy, -hz}
    };
    const uint16_t indices[] = {
        0, 1, 2, 2, 3, 0,
        1, 5, 6, 6, 2, 1,
        5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4,
        3, 2, 6, 6, 7, 3,
        4, 5, 1, 1, 0, 4
    };

    const size_t posOffset = outBin.size();
    outBin.resize(posOffset + sizeof(corners));
    std::memcpy(outBin.data() + posOffset, corners, sizeof(corners));

    const int posView = static_cast<int>(outBufferViews.size());
    outBufferViews.push_back({
        {"buffer", 0},
        {"byteOffset", posOffset},
        {"byteLength", sizeof(corners)},
        {"target", 34962}
    });

    outPosAccessor = static_cast<int>(outAccessors.size());
    outAccessors.push_back({
        {"bufferView", posView},
        {"componentType", 5126},
        {"count", 8},
        {"type", "VEC3"},
        {"min", {-hx, -hy, -hz}},
        {"max", {hx, hy, hz}}
    });

    const size_t idxOffset = outBin.size();
    outBin.resize(idxOffset + sizeof(indices));
    std::memcpy(outBin.data() + idxOffset, indices, sizeof(indices));
    while ((outBin.size() % 4) != 0) {
        outBin.push_back(0);
    }

    const int idxView = static_cast<int>(outBufferViews.size());
    outBufferViews.push_back({
        {"buffer", 0},
        {"byteOffset", idxOffset},
        {"byteLength", sizeof(indices)},
        {"target", 34963}
    });

    outIdxAccessor = static_cast<int>(outAccessors.size());
    outAccessors.push_back({
        {"bufferView", idxView},
        {"componentType", 5123},
        {"count", 36},
        {"type", "SCALAR"}
    });
}

void AppendOctahedronGeometry(
    std::vector<uint8_t>& outBin,
    std::vector<nlohmann::json>& outBufferViews,
    std::vector<nlohmann::json>& outAccessors,
    int& outPosAccessor,
    int& outIdxAccessor,
    float radius,
    float height
) {
    const float verts[6][3] = {
        { 0.0f,    height, 0.0f},
        { radius,  0.0f,   0.0f},
        { 0.0f,    0.0f,   radius},
        {-radius,  0.0f,   0.0f},
        { 0.0f,    0.0f,  -radius},
        { 0.0f,   -height, 0.0f}
    };
    const uint16_t indices[] = {
        0, 2, 1,
        0, 3, 2,
        0, 4, 3,
        0, 1, 4,
        5, 1, 2,
        5, 2, 3,
        5, 3, 4,
        5, 4, 1
    };

    const size_t posOffset = outBin.size();
    outBin.resize(posOffset + sizeof(verts));
    std::memcpy(outBin.data() + posOffset, verts, sizeof(verts));

    const int posView = static_cast<int>(outBufferViews.size());
    outBufferViews.push_back({
        {"buffer", 0},
        {"byteOffset", posOffset},
        {"byteLength", sizeof(verts)},
        {"target", 34962}
    });

    outPosAccessor = static_cast<int>(outAccessors.size());
    outAccessors.push_back({
        {"bufferView", posView},
        {"componentType", 5126},
        {"count", 6},
        {"type", "VEC3"},
        {"min", {-radius, -height, -radius}},
        {"max", {radius, height, radius}}
    });

    const size_t idxOffset = outBin.size();
    outBin.resize(idxOffset + sizeof(indices));
    std::memcpy(outBin.data() + idxOffset, indices, sizeof(indices));
    while ((outBin.size() % 4) != 0) {
        outBin.push_back(0);
    }

    const int idxView = static_cast<int>(outBufferViews.size());
    outBufferViews.push_back({
        {"buffer", 0},
        {"byteOffset", idxOffset},
        {"byteLength", sizeof(indices)},
        {"target", 34963}
    });

    outIdxAccessor = static_cast<int>(outAccessors.size());
    outAccessors.push_back({
        {"bufferView", idxView},
        {"componentType", 5123},
        {"count", 24},
        {"type", "SCALAR"}
    });
}

void WriteCrystalTotemGltf(const std::filesystem::path& outputPath) {
    std::vector<uint8_t> bin;
    std::vector<nlohmann::json> bufferViews;
    std::vector<nlohmann::json> accessors;

    int pedPos = 0, pedIdx = 0;
    AppendBoxGeometry(bin, bufferViews, accessors, pedPos, pedIdx, 0.45f, 0.14f, 0.45f);

    int cryPos = 0, cryIdx = 0;
    AppendOctahedronGeometry(bin, bufferViews, accessors, cryPos, cryIdx, 0.38f, 0.62f);

    nlohmann::json gltf;
    gltf["asset"] = {{"version", "2.0"}, {"generator", "Kyumist Engine Exporter"}};
    gltf["scene"] = 0;
    gltf["scenes"] = nlohmann::json::array({{{"name", "CrystalTotemScene"}, {"nodes", {0}}}});
    gltf["nodes"] = nlohmann::json::array({
        {
            {"name", "Pedestal"},
            {"mesh", 0},
            {"translation", {0.0f, 0.14f, 0.0f}},
            {"children", {1}}
        },
        {
            {"name", "FloatingCrystal"},
            {"mesh", 1},
            {"translation", {0.0f, 0.85f, 0.0f}}
        }
    });
    gltf["materials"] = nlohmann::json::array({
        {
            {"name", "CyanCrystalPBR"},
            {"pbrMetallicRoughness", {
                {"baseColorFactor", {0.22f, 0.86f, 0.96f, 1.0f}},
                {"metallicFactor", 0.72f},
                {"roughnessFactor", 0.18f}
            }},
            {"emissiveFactor", {0.05f, 0.18f, 0.24f}}
        },
        {
            {"name", "BrushedGoldPedestal"},
            {"pbrMetallicRoughness", {
                {"baseColorFactor", {0.92f, 0.72f, 0.24f, 1.0f}},
                {"metallicFactor", 0.85f},
                {"roughnessFactor", 0.30f}
            }}
        }
    });
    gltf["meshes"] = nlohmann::json::array({
        {
            {"name", "PedestalMesh"},
            {"primitives", nlohmann::json::array({
                {
                    {"attributes", {{"POSITION", pedPos}}},
                    {"indices", pedIdx},
                    {"material", 1}
                }
            })}
        },
        {
            {"name", "CrystalMesh"},
            {"primitives", nlohmann::json::array({
                {
                    {"attributes", {{"POSITION", cryPos}}},
                    {"indices", cryIdx},
                    {"material", 0}
                }
            })}
        }
    });
    gltf["buffers"] = nlohmann::json::array({
        {
            {"uri", "data:application/octet-stream;base64," + EncodeBase64(bin)},
            {"byteLength", bin.size()}
        }
    });
    gltf["bufferViews"] = bufferViews;
    gltf["accessors"] = accessors;

    std::ofstream out(outputPath);
    if (out.is_open()) {
        out << gltf.dump(2) << '\n';
    }
}

} // namespace

bool ModelLoader::LoadModel(const std::filesystem::path& path, LoadedModelData& outData) {
    if (!std::filesystem::exists(path)) {
        return false;
    }

    const std::string ext = ToLowerExt(path);
    if (ext == ".gltf") {
        return ParseGltfOrGlb(path, false, outData);
    }
    if (ext == ".glb") {
        return ParseGltfOrGlb(path, true, outData);
    }
    if (ext == ".obj") {
        return ParseObjFileWithMaterials(path, outData);
    }
    return false;
}

void ModelLoader::EvaluateSkinnedVertices(
    const LoadedModelData& model,
    const std::string& animationName,
    float timeSeconds,
    bool loop,
    bool autoPlayFirst,
    std::vector<Vertex>& outVertices,
    bool* outFinished
) {
    if (outFinished) {
        *outFinished = false;
    }
    if (!model.hasSkin || model.bindVertices.empty() || model.skinWeights.size() != model.bindVertices.size()) {
        outVertices = model.vertices;
        return;
    }

    const auto& skel = model.skeleton;
    const AnimationClipData* clip = nullptr;
    if (!animationName.empty()) {
        for (const auto& c : skel.animations) {
            if (c.name == animationName) {
                clip = &c;
                break;
            }
        }
    }
    if (!clip && autoPlayFirst && !skel.animations.empty()) {
        clip = &skel.animations.front();
    }
    if (!clip || skel.nodes.empty() || skel.jointNodeIndices.empty()) {
        outVertices = model.vertices;
        return;
    }

    float evalTime = timeSeconds;
    if (clip->duration > 1e-5f) {
        if (loop) {
            evalTime = std::fmod(timeSeconds, clip->duration);
            if (evalTime < 0.0f) {
                evalTime += clip->duration;
            }
        } else {
            if (timeSeconds >= clip->duration) {
                evalTime = clip->duration;
                if (outFinished) {
                    *outFinished = true;
                }
            } else if (timeSeconds < 0.0f) {
                evalTime = 0.0f;
            }
        }
    } else {
        evalTime = 0.0f;
    }

    const size_t nodeCount = skel.nodes.size();
    std::vector<glm::vec3> localT(nodeCount);
    std::vector<glm::quat> localR(nodeCount);
    std::vector<glm::vec3> localS(nodeCount);
    std::vector<bool> useBindMatrix(nodeCount, false);
    for (size_t i = 0; i < nodeCount; ++i) {
        localT[i] = skel.nodes[i].bindTranslation;
        localR[i] = skel.nodes[i].bindRotation;
        localS[i] = skel.nodes[i].bindScale;
        useBindMatrix[i] = skel.nodes[i].hasBindMatrix;
    }

    for (const auto& ch : clip->channels) {
        if (ch.targetNode < 0 || static_cast<size_t>(ch.targetNode) >= nodeCount || ch.times.empty() || ch.values.empty()) {
            continue;
        }
        const size_t nIdx = static_cast<size_t>(ch.targetNode);
        useBindMatrix[nIdx] = false;

        glm::vec4 sampled(0.0f);
        if (ch.times.size() == 1 || evalTime <= ch.times.front()) {
            sampled = ch.values.front();
        } else if (evalTime >= ch.times.back()) {
            sampled = ch.values.back();
        } else {
            auto it = std::upper_bound(ch.times.begin(), ch.times.end(), evalTime);
            size_t k1 = static_cast<size_t>(std::distance(ch.times.begin(), it));
            if (k1 == 0) k1 = 1;
            if (k1 >= ch.times.size()) k1 = ch.times.size() - 1;
            const size_t k0 = k1 - 1;
            const float t0 = ch.times[k0];
            const float t1 = ch.times[k1];
            const float dt = t1 - t0;
            const float alpha = (ch.interpolation == AnimationInterpolation::Step || dt <= 1e-6f)
                ? 0.0f
                : std::clamp((evalTime - t0) / dt, 0.0f, 1.0f);

            if (ch.path == AnimationPathType::Rotation) {
                // glTF rotation values are stored as [x, y, z, w]
                const glm::vec4& a = ch.values[k0];
                const glm::vec4& b = ch.values[k1];
                glm::quat q0 = glm::normalize(glm::quat(a.w, a.x, a.y, a.z));
                glm::quat q1 = glm::normalize(glm::quat(b.w, b.x, b.y, b.z));
                if (glm::dot(q0, q1) < 0.0f) {
                    q1 = -q1;
                }
                const glm::quat q = glm::normalize(glm::slerp(q0, q1, alpha));
                sampled = glm::vec4(q.x, q.y, q.z, q.w);
            } else {
                sampled = glm::mix(ch.values[k0], ch.values[k1], alpha);
            }
        }

        switch (ch.path) {
            case AnimationPathType::Translation:
                localT[nIdx] = glm::vec3(sampled.x, sampled.y, sampled.z);
                break;
            case AnimationPathType::Rotation:
                localR[nIdx] = glm::normalize(glm::quat(sampled.w, sampled.x, sampled.y, sampled.z));
                break;
            case AnimationPathType::Scale:
                localS[nIdx] = glm::vec3(sampled.x, sampled.y, sampled.z);
                break;
        }
    }

    std::vector<glm::mat4> globalTransforms(nodeCount, glm::mat4(1.0f));
    std::vector<bool> visited(nodeCount, false);
    auto computeGlobal = [&](auto& self, int nodeIdx, const glm::mat4& parentGlobal) -> void {
        if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodeCount) {
            return;
        }
        const size_t idx = static_cast<size_t>(nodeIdx);
        if (visited[idx]) {
            return;
        }
        visited[idx] = true;
        const glm::mat4 localMat = useBindMatrix[idx]
            ? skel.nodes[idx].bindMatrix
            : (glm::translate(glm::mat4(1.0f), localT[idx]) *
               glm::mat4_cast(localR[idx]) *
               glm::scale(glm::mat4(1.0f), localS[idx]));
        globalTransforms[idx] = parentGlobal * localMat;
        for (int childIdx : skel.nodes[idx].children) {
            self(self, childIdx, globalTransforms[idx]);
        }
    };

    for (int rootIdx : skel.rootNodes) {
        computeGlobal(computeGlobal, rootIdx, glm::mat4(1.0f));
    }
    for (size_t i = 0; i < nodeCount; ++i) {
        if (!visited[i]) {
            computeGlobal(computeGlobal, static_cast<int>(i), glm::mat4(1.0f));
        }
    }

    const size_t jointCount = skel.jointNodeIndices.size();
    std::vector<glm::mat4> jointMatrices(jointCount, glm::mat4(1.0f));
    for (size_t j = 0; j < jointCount; ++j) {
        const int jNode = skel.jointNodeIndices[j];
        if (jNode >= 0 && static_cast<size_t>(jNode) < nodeCount) {
            jointMatrices[j] = globalTransforms[static_cast<size_t>(jNode)] * skel.inverseBindMatrices[j];
        }
    }

    const size_t vertCount = model.bindVertices.size();
    outVertices.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i) {
        const Vertex& src = model.bindVertices[i];
        const SkinVertexWeight& sw = model.skinWeights[i];
        const float wSum = sw.weights.x + sw.weights.y + sw.weights.z + sw.weights.w;
        if (sw.joints.x < 0 || wSum <= 1e-5f) {
            if (sw.attachedNode >= 0 && static_cast<size_t>(sw.attachedNode) < nodeCount) {
                const glm::mat4& nodeMat = globalTransforms[static_cast<size_t>(sw.attachedNode)];
                Vertex dst = src;
                dst.position = glm::vec3(nodeMat * glm::vec4(src.position, 1.0f));
                const glm::vec3 n = glm::mat3(nodeMat) * src.normal;
                const float nLen = glm::length(n);
                dst.normal = (nLen > 1e-6f) ? (n / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);
                outVertices[i] = dst;
            } else {
                outVertices[i] = src;
            }
            continue;
        }

        glm::mat4 skinMat(0.0f);
        for (int k = 0; k < 4; ++k) {
            const float w = sw.weights[k];
            const int j = sw.joints[k];
            if (w > 0.0f && j >= 0 && static_cast<size_t>(j) < jointCount) {
                skinMat += jointMatrices[static_cast<size_t>(j)] * w;
            }
        }

        Vertex dst = src;
        dst.position = glm::vec3(skinMat * glm::vec4(src.position, 1.0f));
        const glm::vec3 n = glm::mat3(skinMat) * src.normal;
        const float nLen = glm::length(n);
        dst.normal = (nLen > 1e-6f) ? (n / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);
        outVertices[i] = dst;
    }
}

bool ModelLoader::LoadMaterialFile(const std::filesystem::path& path, PBRMaterialData& outMat) {
    if (!std::filesystem::exists(path)) {
        return false;
    }

    const std::string ext = ToLowerExt(path);
    if (ext == ".kmmat") {
        std::ifstream in(path);
        if (!in.is_open()) {
            return false;
        }
        try {
            nlohmann::json j;
            in >> j;
            outMat.name = j.value("name", path.stem().string());
            if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() == 4) {
                outMat.baseColor = {
                    j["baseColor"][0].get<float>(),
                    j["baseColor"][1].get<float>(),
                    j["baseColor"][2].get<float>(),
                    j["baseColor"][3].get<float>()
                };
            }
            outMat.metallic = std::clamp(j.value("metallic", 0.1f), 0.0f, 1.0f);
            outMat.roughness = std::clamp(j.value("roughness", 0.5f), 0.05f, 1.0f);
            if (j.contains("emissive") && j["emissive"].is_array() && j["emissive"].size() == 3) {
                outMat.emissive = {
                    j["emissive"][0].get<float>(),
                    j["emissive"][1].get<float>(),
                    j["emissive"][2].get<float>()
                };
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    if (ext == ".mtl") {
        std::unordered_map<std::string, PBRMaterialData> materials;
        std::string firstName;
        if (!ParseMtlLibrary(path, materials, &firstName) || firstName.empty()) {
            return false;
        }
        outMat = materials[firstName];
        return true;
    }

    return false;
}

namespace {

uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

void WriteBE32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void AppendPngChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len) {
    WriteBE32(out, static_cast<uint32_t>(len));
    out.insert(out.end(), type, type + 4);
    if (len > 0 && data) {
        out.insert(out.end(), data, data + len);
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    if (len > 0 && data) {
        crc = crc32(crc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(len));
    }
    WriteBE32(out, static_cast<uint32_t>(crc));
}

uint8_t PaethPredictor(uint8_t a, uint8_t b, uint8_t c) {
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = std::abs(p - static_cast<int>(a));
    const int pb = std::abs(p - static_cast<int>(b));
    const int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

bool DecodePngRGBA(const std::vector<uint8_t>& bytes, ImageData& outImage) {
    static constexpr uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSig, 8) != 0) {
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bitDepth = 0;
    uint8_t colorType = 0;
    bool hasIhdr = false;
    std::vector<uint8_t> idat;
    std::vector<uint8_t> plte;
    std::vector<uint8_t> trns;

    size_t pos = 8;
    while (pos + 12 <= bytes.size()) {
        const uint32_t len = ReadBE32(bytes.data() + pos);
        const char* type = reinterpret_cast<const char*>(bytes.data() + pos + 4);
        pos += 8;
        if (pos + len + 4 > bytes.size()) {
            break;
        }
        const uint8_t* payload = bytes.data() + pos;
        if (std::memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            width = ReadBE32(payload);
            height = ReadBE32(payload + 4);
            bitDepth = payload[8];
            colorType = payload[9];
            const uint8_t compression = payload[10];
            const uint8_t filter = payload[11];
            const uint8_t interlace = payload[12];
            if (width == 0 || height == 0 || bitDepth != 8 || compression != 0 || filter != 0 || interlace != 0) {
                return false;
            }
            hasIhdr = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            plte.assign(payload, payload + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(payload, payload + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), payload, payload + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += len + 4;
    }

    if (!hasIhdr || idat.empty()) {
        return false;
    }

    int bpp = 0;
    switch (colorType) {
        case 0: bpp = 1; break; // Gray
        case 2: bpp = 3; break; // RGB
        case 3: bpp = 1; break; // Indexed
        case 4: bpp = 2; break; // Gray + Alpha
        case 6: bpp = 4; break; // RGBA
        default: return false;
    }

    const size_t stride = static_cast<size_t>(width) * static_cast<size_t>(bpp);
    const size_t expectedRawSize = static_cast<size_t>(height) * (1 + stride);
    std::vector<uint8_t> raw(expectedRawSize);

    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        return false;
    }
    zs.next_in = idat.data();
    zs.avail_in = static_cast<uInt>(idat.size());
    zs.next_out = raw.data();
    zs.avail_out = static_cast<uInt>(raw.size());
    const int infRes = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (infRes != Z_STREAM_END && infRes != Z_OK) {
        return false;
    }

    std::vector<uint8_t> prevRow(stride, 0);
    std::vector<uint8_t> currRow(stride, 0);
    outImage.width = width;
    outImage.height = height;
    outImage.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    for (uint32_t y = 0; y < height; ++y) {
        const size_t rowOffset = static_cast<size_t>(y) * (1 + stride);
        const uint8_t filterType = raw[rowOffset];
        const uint8_t* srcRow = raw.data() + rowOffset + 1;

        for (size_t x = 0; x < stride; ++x) {
            const uint8_t rawByte = srcRow[x];
            const uint8_t a = (x >= static_cast<size_t>(bpp)) ? currRow[x - static_cast<size_t>(bpp)] : 0;
            const uint8_t b = prevRow[x];
            const uint8_t c = (x >= static_cast<size_t>(bpp)) ? prevRow[x - static_cast<size_t>(bpp)] : 0;

            uint8_t recon = rawByte;
            switch (filterType) {
                case 0: break;
                case 1: recon = static_cast<uint8_t>(rawByte + a); break;
                case 2: recon = static_cast<uint8_t>(rawByte + b); break;
                case 3: recon = static_cast<uint8_t>(rawByte + ((static_cast<int>(a) + static_cast<int>(b)) >> 1)); break;
                case 4: recon = static_cast<uint8_t>(rawByte + PaethPredictor(a, b, c)); break;
                default: return false;
            }
            currRow[x] = recon;
        }

        for (uint32_t x = 0; x < width; ++x) {
            const size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            const size_t src = static_cast<size_t>(x) * static_cast<size_t>(bpp);
            if (colorType == 6) {
                outImage.pixels[dst + 0] = currRow[src + 0];
                outImage.pixels[dst + 1] = currRow[src + 1];
                outImage.pixels[dst + 2] = currRow[src + 2];
                outImage.pixels[dst + 3] = currRow[src + 3];
            } else if (colorType == 2) {
                outImage.pixels[dst + 0] = currRow[src + 0];
                outImage.pixels[dst + 1] = currRow[src + 1];
                outImage.pixels[dst + 2] = currRow[src + 2];
                outImage.pixels[dst + 3] = 255;
            } else if (colorType == 0) {
                const uint8_t g = currRow[src];
                outImage.pixels[dst + 0] = g;
                outImage.pixels[dst + 1] = g;
                outImage.pixels[dst + 2] = g;
                outImage.pixels[dst + 3] = 255;
            } else if (colorType == 4) {
                const uint8_t g = currRow[src];
                outImage.pixels[dst + 0] = g;
                outImage.pixels[dst + 1] = g;
                outImage.pixels[dst + 2] = g;
                outImage.pixels[dst + 3] = currRow[src + 1];
            } else if (colorType == 3) {
                const size_t idx = currRow[src];
                if (idx * 3 + 2 < plte.size()) {
                    outImage.pixels[dst + 0] = plte[idx * 3 + 0];
                    outImage.pixels[dst + 1] = plte[idx * 3 + 1];
                    outImage.pixels[dst + 2] = plte[idx * 3 + 2];
                } else {
                    outImage.pixels[dst + 0] = outImage.pixels[dst + 1] = outImage.pixels[dst + 2] = 255;
                }
                outImage.pixels[dst + 3] = (idx < trns.size()) ? trns[idx] : 255;
            }
        }
        std::swap(prevRow, currRow);
    }

    return true;
}

bool DecodePpmRGBA(const std::filesystem::path& path, ImageData& outImage) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::string magic;
    in >> magic;
    if (magic != "P6" && magic != "P3") {
        return false;
    }
    auto skipComments = [&]() {
        while (in >> std::ws && in.peek() == '#') {
            std::string dummy;
            std::getline(in, dummy);
        }
    };
    skipComments();
    uint32_t w = 0, h = 0, maxVal = 255;
    in >> w;
    skipComments();
    in >> h;
    skipComments();
    in >> maxVal;
    if (w == 0 || h == 0 || maxVal == 0) {
        return false;
    }
    in.get(); // consume single whitespace

    outImage.width = w;
    outImage.height = h;
    outImage.pixels.resize(static_cast<size_t>(w) * h * 4);
    if (magic == "P6") {
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!in) return false;
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            outImage.pixels[i * 4 + 0] = rgb[i * 3 + 0];
            outImage.pixels[i * 4 + 1] = rgb[i * 3 + 1];
            outImage.pixels[i * 4 + 2] = rgb[i * 3 + 2];
            outImage.pixels[i * 4 + 3] = 255;
        }
    } else {
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            int r = 0, g = 0, b = 0;
            if (!(in >> r >> g >> b)) return false;
            outImage.pixels[i * 4 + 0] = static_cast<uint8_t>(std::clamp(r * 255 / static_cast<int>(maxVal), 0, 255));
            outImage.pixels[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g * 255 / static_cast<int>(maxVal), 0, 255));
            outImage.pixels[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b * 255 / static_cast<int>(maxVal), 0, 255));
            outImage.pixels[i * 4 + 3] = 255;
        }
    }
    return true;
}

} // namespace

bool ModelLoader::LoadImageRGBA(const std::filesystem::path& path, ImageData& outImage) {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    const std::string ext = ToLowerExt(path);
    if (ext == ".png") {
        std::vector<uint8_t> bytes;
        if (!ReadBinaryFile(path, bytes)) {
            return false;
        }
        return DecodePngRGBA(bytes, outImage);
    }
    if (ext == ".ppm") {
        return DecodePpmRGBA(path, outImage);
    }
    return false;
}

bool ModelLoader::SavePngRGBA(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const uint8_t* rgbaPixels
) {
    if (width == 0 || height == 0 || !rgbaPixels) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw(static_cast<size_t>(height) * (1 + rowBytes));
    for (uint32_t y = 0; y < height; ++y) {
        const size_t dstRow = static_cast<size_t>(y) * (1 + rowBytes);
        raw[dstRow] = 0; // None filter
        std::memcpy(raw.data() + dstRow + 1, rgbaPixels + static_cast<size_t>(y) * rowBytes, rowBytes);
    }

    uLongf compLen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compLen);
    if (compress2(compressed.data(), &compLen, raw.data(), static_cast<uLong>(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }

    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    WriteBE32(ihdr, width);
    WriteBE32(ihdr, height);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    AppendPngChunk(png, "IHDR", ihdr.data(), ihdr.size());
    AppendPngChunk(png, "IDAT", compressed.data(), compLen);
    AppendPngChunk(png, "IEND", nullptr, 0);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

bool ModelLoader::SaveMaterialFile(const std::filesystem::path& path, const PBRMaterialData& mat) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    nlohmann::json j;
    j["name"] = mat.name.empty() ? path.stem().string() : mat.name;
    j["baseColor"] = nlohmann::json::array({mat.baseColor.r, mat.baseColor.g, mat.baseColor.b, mat.baseColor.a});
    j["metallic"] = std::clamp(mat.metallic, 0.0f, 1.0f);
    j["roughness"] = std::clamp(mat.roughness, 0.05f, 1.0f);
    j["emissive"] = nlohmann::json::array({mat.emissive.r, mat.emissive.g, mat.emissive.b});

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << j.dump(2) << '\n';
    return true;
}

std::filesystem::path ModelLoader::EnsureDefaultSampleAssets(const std::filesystem::path& projectOrAssetsDir) {
    std::error_code ec;
    const std::filesystem::path modelsDir = projectOrAssetsDir / "models";
    const std::filesystem::path materialsDir = projectOrAssetsDir / "materials";
    std::filesystem::create_directories(modelsDir, ec);
    std::filesystem::create_directories(materialsDir, ec);

    const std::filesystem::path sampleGltf = modelsDir / "crystal_totem.gltf";
    if (!std::filesystem::exists(sampleGltf, ec)) {
        WriteCrystalTotemGltf(sampleGltf);
    }

    const std::filesystem::path pyramidMtl = modelsDir / "pyramid.mtl";
    if (!std::filesystem::exists(pyramidMtl, ec)) {
        std::ofstream out(pyramidMtl);
        if (out.is_open()) {
            out << "# Material de ejemplo Wavefront MTL\n"
                << "newmtl PyramidGold\n"
                << "Kd 0.94 0.74 0.25\n"
                << "Ke 0.02 0.01 0.00\n"
                << "Pm 0.80\n"
                << "Pr 0.28\n";
        }
    }

    const std::filesystem::path goldMat = materialsDir / "gold_pbr.kmmat";
    if (!std::filesystem::exists(goldMat, ec)) {
        PBRMaterialData gold;
        gold.name = "Oro Cepillado PBR";
        gold.baseColor = {0.95f, 0.75f, 0.28f, 1.0f};
        gold.metallic = 0.90f;
        gold.roughness = 0.24f;
        gold.emissive = {0.0f, 0.0f, 0.0f};
        SaveMaterialFile(goldMat, gold);
    }

    const std::filesystem::path neonMat = materialsDir / "neon_cyan.kmmat";
    if (!std::filesystem::exists(neonMat, ec)) {
        PBRMaterialData cyan;
        cyan.name = "Cristal Cian Emisivo";
        cyan.baseColor = {0.18f, 0.85f, 0.98f, 1.0f};
        cyan.metallic = 0.65f;
        cyan.roughness = 0.16f;
        cyan.emissive = {0.06f, 0.22f, 0.30f};
        SaveMaterialFile(neonMat, cyan);
    }

    const std::filesystem::path texturesDir = projectOrAssetsDir / "textures";
    std::filesystem::create_directories(texturesDir, ec);

    const std::filesystem::path checkerPng = texturesDir / "checker_albedo.png";
    if (!std::filesystem::exists(checkerPng, ec)) {
        constexpr uint32_t W = 64;
        constexpr uint32_t H = 64;
        std::vector<uint8_t> pixels(W * H * 4);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                const bool border = (x % 32 == 0) || (y % 32 == 0);
                const bool alt = ((x / 16) + (y / 16)) % 2 == 0;
                uint8_t r = alt ? 215 : 152;
                uint8_t g = alt ? 222 : 162;
                uint8_t b = alt ? 235 : 178;
                if (border) {
                    r = 68;
                    g = 76;
                    b = 92;
                }
                const size_t i = (static_cast<size_t>(y) * W + x) * 4;
                pixels[i + 0] = r;
                pixels[i + 1] = g;
                pixels[i + 2] = b;
                pixels[i + 3] = 255;
            }
        }
        SavePngRGBA(checkerPng, W, H, pixels.data());
    }

    const std::filesystem::path coinPng = texturesDir / "coin_spritesheet.png";
    if (!std::filesystem::exists(coinPng, ec)) {
        constexpr uint32_t W = 128; // 4 frames de 32x32
        constexpr uint32_t H = 32;
        std::vector<uint8_t> pixels(W * H * 4, 0);
        const float scales[4] = {1.0f, 0.65f, 0.28f, 0.65f};
        for (uint32_t f = 0; f < 4; ++f) {
            const float sx = scales[f];
            for (uint32_t y = 0; y < 32; ++y) {
                for (uint32_t x = 0; x < 32; ++x) {
                    const float nx = (static_cast<float>(x) - 15.5f) / (11.5f * sx);
                    const float ny = (static_cast<float>(y) - 15.5f) / 11.5f;
                    const float d2 = nx * nx + ny * ny;
                    if (d2 <= 1.0f) {
                        const bool rim = (d2 > 0.68f);
                        const bool glint = (nx < -0.15f && ny < -0.15f && d2 < 0.45f);
                        const uint32_t px = f * 32 + x;
                        const size_t i = (static_cast<size_t>(y) * W + px) * 4;
                        pixels[i + 0] = glint ? 255 : (rim ? 218 : 250);
                        pixels[i + 1] = glint ? 245 : (rim ? 162 : 204);
                        pixels[i + 2] = glint ? 180 : (rim ? 34 : 58);
                        pixels[i + 3] = 255;
                    }
                }
            }
        }
        SavePngRGBA(coinPng, W, H, pixels.data());
    }

    return sampleGltf;
}

bool ModelLoader::ImportExternalAssetToDir(
    const std::filesystem::path& sourceFile,
    const std::filesystem::path& targetDir,
    std::filesystem::path* outImportedPath,
    std::string* outError
) {
    auto setErr = [&](const std::string& msg) {
        if (outError) {
            *outError = msg;
        }
        KM_CORE_ERROR("Asset import failed: {}", msg);
        return false;
    };

    std::error_code ec;
    if (sourceFile.empty() || !std::filesystem::exists(sourceFile, ec) || !std::filesystem::is_regular_file(sourceFile, ec)) {
        return setErr("El archivo origen no existe o no es un fichero regular.");
    }

    std::filesystem::create_directories(targetDir, ec);
    if (ec) {
        return setErr("No se pudo crear la carpeta destino: " + ec.message());
    }

    const std::filesystem::path destFile = targetDir / sourceFile.filename();
    const std::filesystem::path canonSrc = std::filesystem::weakly_canonical(sourceFile, ec);
    const std::filesystem::path canonDst = std::filesystem::weakly_canonical(destFile, ec);
    if (canonSrc != canonDst) {
        std::filesystem::copy_file(sourceFile, destFile, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return setErr("Error copiando el asset: " + ec.message());
        }
    }

    const std::string ext = ToLowerExt(sourceFile);
    if (ext == ".gltf") {
        // Copy external .bin buffers referenced by the glTF JSON
        std::ifstream in(sourceFile);
        if (in.is_open()) {
            try {
                nlohmann::json gltf;
                in >> gltf;
                if (gltf.contains("buffers") && gltf["buffers"].is_array()) {
                    for (const auto& buf : gltf["buffers"]) {
                        if (buf.contains("uri") && buf["uri"].is_string()) {
                            const std::string uri = buf["uri"].get<std::string>();
                            if (uri.rfind("data:", 0) != 0) {
                                const std::string relStr = DecodeUriPath(uri);
                                const std::filesystem::path srcBin = sourceFile.parent_path() / relStr;
                                const std::filesystem::path dstBin = targetDir / relStr;
                                if (std::filesystem::exists(srcBin, ec)) {
                                    if (dstBin.has_parent_path()) {
                                        std::filesystem::create_directories(dstBin.parent_path(), ec);
                                    }
                                    if (std::filesystem::weakly_canonical(srcBin, ec) != std::filesystem::weakly_canonical(dstBin, ec)) {
                                        std::filesystem::copy_file(srcBin, dstBin, std::filesystem::copy_options::overwrite_existing, ec);
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (const std::exception&) {
            }
        }
    } else if (ext == ".obj") {
        // Copy referenced .mtl files or matching <stem>.mtl
        std::ifstream in(sourceFile);
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                std::istringstream iss(line);
                std::string key;
                iss >> key;
                if (key == "mtllib") {
                    std::string rest;
                    std::getline(iss, rest);
                    const std::string mtlName = TrimWhitespace(rest);
                    if (!mtlName.empty()) {
                        const std::filesystem::path srcMtl = sourceFile.parent_path() / mtlName;
                        const std::filesystem::path dstMtl = targetDir / mtlName;
                        if (std::filesystem::exists(srcMtl, ec)) {
                            if (dstMtl.has_parent_path()) {
                                std::filesystem::create_directories(dstMtl.parent_path(), ec);
                            }
                            if (std::filesystem::weakly_canonical(srcMtl, ec) != std::filesystem::weakly_canonical(dstMtl, ec)) {
                                std::filesystem::copy_file(srcMtl, dstMtl, std::filesystem::copy_options::overwrite_existing, ec);
                            }
                        }
                    }
                }
            }
        }
        const std::filesystem::path siblingMtl = sourceFile.parent_path() / (sourceFile.stem().string() + ".mtl");
        if (std::filesystem::exists(siblingMtl, ec)) {
            const std::filesystem::path dstSibling = targetDir / siblingMtl.filename();
            if (std::filesystem::weakly_canonical(siblingMtl, ec) != std::filesystem::weakly_canonical(dstSibling, ec)) {
                std::filesystem::copy_file(siblingMtl, dstSibling, std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    if (outImportedPath) {
        *outImportedPath = destFile;
    }
    KM_INFO("Asset importado en '{}'.", destFile.string());
    return true;
}

} // namespace Kyumist

