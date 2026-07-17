#include "pcg_fbx_api.h"

#include "geometry_binary_reader.hpp"

#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using pcg::fbx::Geometry;

void set_error(char* buffer, int size, const std::string& message)
{
    if (!buffer || size <= 0)
        return;
    const size_t count = std::min(message.size(), static_cast<size_t>(size - 1));
    std::memcpy(buffer, message.data(), count);
    buffer[count] = '\0';
}

std::vector<aiVector3D> compute_normals(const Geometry& geometry)
{
    std::vector<aiVector3D> normals(geometry.points.size());
    for (const auto& face : geometry.faces) {
        aiVector3D face_normal;
        for (size_t i = 0; i < face.size(); ++i) {
            const auto& current = geometry.points[face[i]];
            const auto& next = geometry.points[face[(i + 1) % face.size()]];
            face_normal.x += (current[1] - next[1]) * (current[2] + next[2]);
            face_normal.y += (current[2] - next[2]) * (current[0] + next[0]);
            face_normal.z += (current[0] - next[0]) * (current[1] + next[1]);
        }
        for (uint32_t index : face)
            normals[index] += face_normal;
    }
    for (auto& normal : normals) {
        const ai_real length = std::sqrt(normal.SquareLength());
        if (length > static_cast<ai_real>(1e-12))
            normal /= length;
        else
            normal = aiVector3D(0, 1, 0);
    }
    return normals;
}

std::unique_ptr<aiScene> build_scene(
    const Geometry& geometry,
    float scale,
    bool generate_normals)
{
    auto scene = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode("PcgFbxExport");
    scene->mMetaData = aiMetadata::Alloc(10);
    scene->mMetaData->Set(0, "UpAxis", 1);
    scene->mMetaData->Set(1, "UpAxisSign", 1);
    scene->mMetaData->Set(2, "FrontAxis", 2);
    scene->mMetaData->Set(3, "FrontAxisSign", 1);
    scene->mMetaData->Set(4, "CoordAxis", 0);
    scene->mMetaData->Set(5, "CoordAxisSign", 1);
    scene->mMetaData->Set(6, "OriginalUpAxis", 1);
    scene->mMetaData->Set(7, "OriginalUpAxisSign", 1);
    scene->mMetaData->Set(8, "UnitScaleFactor", 100.0);
    scene->mMetaData->Set(9, "OriginalUnitScaleFactor", 100.0);

    std::map<std::string, std::vector<size_t>> faces_by_material;
    for (size_t i = 0; i < geometry.faces.size(); ++i) {
        std::string material = geometry.material.empty() ? "DefaultMaterial" : geometry.material;
        if (i < geometry.face_materials.size() && !geometry.face_materials[i].empty())
            material = geometry.face_materials[i];
        faces_by_material[material].push_back(i);
    }

    scene->mNumMaterials = static_cast<unsigned int>(faces_by_material.size());
    scene->mMaterials = new aiMaterial*[scene->mNumMaterials];
    scene->mNumMeshes = static_cast<unsigned int>(faces_by_material.size());
    scene->mMeshes = new aiMesh*[scene->mNumMeshes];
    scene->mRootNode->mNumMeshes = scene->mNumMeshes;
    scene->mRootNode->mMeshes = new unsigned int[scene->mNumMeshes];

    const auto normals = generate_normals ? compute_normals(geometry) : std::vector<aiVector3D>();
    unsigned int mesh_index = 0;
    for (const auto& [material_name, face_ids] : faces_by_material) {
        auto* material = new aiMaterial();
        const aiString assimp_material_name(material_name);
        material->AddProperty(&assimp_material_name, AI_MATKEY_NAME);
        scene->mMaterials[mesh_index] = material;

        auto* mesh = new aiMesh();
        mesh->mName = aiString(material_name + "_Geometry");
        mesh->mMaterialIndex = mesh_index;
        mesh->mNumVertices = static_cast<unsigned int>(geometry.points.size());
        mesh->mVertices = new aiVector3D[mesh->mNumVertices];
        if (generate_normals)
            mesh->mNormals = new aiVector3D[mesh->mNumVertices];
        if (!geometry.colors.empty())
            mesh->mColors[0] = new aiColor4D[mesh->mNumVertices];
        if (!geometry.uvs.empty()) {
            mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
            mesh->mNumUVComponents[0] = 2;
        }
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const auto& point = geometry.points[i];
            mesh->mVertices[i] = aiVector3D(point[0] * scale, point[1] * scale, point[2] * scale);
            if (generate_normals)
                mesh->mNormals[i] = normals[i];
            if (mesh->mColors[0]) {
                const auto& color = geometry.colors[i];
                mesh->mColors[0][i] = aiColor4D(color[0], color[1], color[2], color[3]);
            }
            if (mesh->mTextureCoords[0]) {
                const auto& uv = geometry.uvs[i];
                mesh->mTextureCoords[0][i] = aiVector3D(uv[0], uv[1], 0);
            }
        }

        mesh->mNumFaces = static_cast<unsigned int>(face_ids.size());
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        mesh->mPrimitiveTypes = 0;
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            const auto& polygon = geometry.faces[face_ids[i]];
            auto& face = mesh->mFaces[i];
            face.mNumIndices = static_cast<unsigned int>(polygon.size());
            face.mIndices = new unsigned int[face.mNumIndices];
            std::copy(polygon.begin(), polygon.end(), face.mIndices);
            mesh->mPrimitiveTypes |= polygon.size() == 3 ? aiPrimitiveType_TRIANGLE
                                                        : aiPrimitiveType_POLYGON;
        }

        scene->mMeshes[mesh_index] = mesh;
        scene->mRootNode->mMeshes[mesh_index] = mesh_index;
        ++mesh_index;
    }
    return scene;
}

} // namespace

extern "C" PCG_FBX_API int pcg_fbx_export_v1(
    const void* geometry_binary,
    int geometry_binary_size,
    const char* output_path_utf8,
    const PcgFbxExportOptions* options,
    char* error_buffer,
    int error_buffer_size)
{
    set_error(error_buffer, error_buffer_size, "");
    if (!geometry_binary || geometry_binary_size <= 0 || !output_path_utf8 || !*output_path_utf8) {
        set_error(error_buffer, error_buffer_size, "Geometry and output path are required.");
        return PCG_FBX_INVALID_ARGUMENT;
    }

    float scale = 1.0f;
    bool generate_normals = true;
    if (options) {
        if (options->struct_size < sizeof(PcgFbxExportOptions)) {
            set_error(error_buffer, error_buffer_size, "Export options struct has an incompatible size.");
            return PCG_FBX_INVALID_ARGUMENT;
        }
        scale = options->scale;
        generate_normals = options->generate_normals != 0;
    }
    if (!std::isfinite(scale) || scale <= 0.0f) {
        set_error(error_buffer, error_buffer_size, "Export scale must be finite and greater than zero.");
        return PCG_FBX_INVALID_ARGUMENT;
    }

    Geometry geometry;
    std::string parse_error;
    if (!pcg::fbx::read_geometry_binary(
            geometry_binary, geometry_binary_size, geometry, parse_error)) {
        set_error(error_buffer, error_buffer_size, parse_error);
        return PCG_FBX_INVALID_GEOMETRY;
    }
    if (geometry.faces.empty()) {
        set_error(error_buffer, error_buffer_size, "FBX export requires polygon geometry.");
        return PCG_FBX_INVALID_GEOMETRY;
    }

    try {
        const std::filesystem::path output_path = std::filesystem::u8path(output_path_utf8);
        const auto parent = output_path.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        auto scene = build_scene(geometry, scale, generate_normals);
        Assimp::Exporter exporter;
        const aiReturn result = exporter.Export(scene.get(), "fbx", output_path_utf8, 0u);
        if (result != aiReturn_SUCCESS) {
            set_error(error_buffer, error_buffer_size, exporter.GetErrorString());
            return PCG_FBX_EXPORT_FAILED;
        }
    } catch (const std::exception& exception) {
        set_error(error_buffer, error_buffer_size, exception.what());
        return PCG_FBX_EXPORT_FAILED;
    }
    return PCG_FBX_OK;
}

extern "C" PCG_FBX_API const char* pcg_fbx_get_version(void)
{
    return "PcgFbxExporter/1 Assimp/6.0.5";
}
