#include "pcg_fbx_api.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if PCG_FBX_ENABLE_READBACK_TESTS
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>
#endif

namespace {

void append_u32(std::vector<uint8_t>& data, uint32_t value)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    data.insert(data.end(), bytes, bytes + 4);
}

void append_chunk(std::vector<uint8_t>& data, uint32_t id, const void* payload, uint32_t size)
{
    append_u32(data, id);
    append_u32(data, size);
    const auto* bytes = static_cast<const uint8_t*>(payload);
    data.insert(data.end(), bytes, bytes + size);
}

std::vector<uint8_t> make_quad()
{
    std::vector<uint8_t> data;
    append_u32(data, 0x47475043u);
    append_u32(data, 2u);
    append_u32(data, 4u);
    append_u32(data, 1u);
    const float points[] = {-1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1};
    const uint32_t offsets[] = {0};
    const uint32_t indices[] = {0, 1, 2, 3};
    const float uvs[] = {0, 0, 1, 0, 1, 1, 0, 1};
    const float colors[] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1};
    const char material[] = "FallbackMaterial";
    std::vector<uint8_t> face_material;
    append_u32(face_material, 3u);
    face_material.insert(face_material.end(), {'R', 'e', 'd'});
    append_chunk(data, 1u, points, sizeof(points));
    append_chunk(data, 2u, offsets, sizeof(offsets));
    append_chunk(data, 3u, indices, sizeof(indices));
    append_chunk(data, 7u, colors, sizeof(colors));
    append_chunk(data, 8u, uvs, sizeof(uvs));
    append_chunk(data, 9u, material, sizeof(material));
    append_chunk(data, 10u, face_material.data(), static_cast<uint32_t>(face_material.size()));
    return data;
}

} // namespace

int main()
{
    const auto geometry = make_quad();
    const auto path = std::filesystem::temp_directory_path() / "pcg_fbx_exporter_quad.fbx";
    PcgFbxExportOptions options{sizeof(PcgFbxExportOptions), 1.0f, 1};
    char error[1024]{};
    const int result = pcg_fbx_export_v1(
        geometry.data(), static_cast<int>(geometry.size()), path.string().c_str(),
        &options, error, sizeof(error));
    assert(result == PCG_FBX_OK && error[0] == '\0');
    assert(std::filesystem::exists(path));
    assert(std::filesystem::file_size(path) > 256);

    std::ifstream stream(path, std::ios::binary);
    std::string header(18, '\0');
    stream.read(header.data(), static_cast<std::streamsize>(header.size()));
    assert(header.find("Kaydara FBX") != std::string::npos);

#if PCG_FBX_ENABLE_READBACK_TESTS
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.string(), 0u);
    assert(scene != nullptr);
    assert(scene->mNumMeshes == 1);
    assert(scene->mMeshes[0]->mNumFaces == 1);
    assert(scene->mMeshes[0]->mFaces[0].mNumIndices == 4);
    assert(scene->mMeshes[0]->HasTextureCoords(0));
    assert(scene->mMeshes[0]->HasVertexColors(0));
    assert(scene->mNumMaterials == 1);
    aiString material_name;
    assert(scene->mMaterials[0]->Get(AI_MATKEY_NAME, material_name) == aiReturn_SUCCESS);
    assert(std::string(material_name.C_Str()) == "Red");
    double unit_scale = 0.0;
    assert(scene->mMetaData != nullptr);
    assert(scene->mMetaData->Get("UnitScaleFactor", unit_scale));
    assert(unit_scale == 100.0);
#endif

    std::filesystem::remove(path);
    return 0;
}
