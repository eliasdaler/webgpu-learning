#include "GltfLoader.h"

#include <cassert>
#include <span>

#include <Graphics/Model.h>

#include "WebGPUUtil.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

namespace
{
bool LoadImageData(
    tinygltf::Image* image,
    const int image_idx,
    std::string* err,
    std::string* warn,
    int req_width,
    int req_height,
    const unsigned char* bytes,
    int size,
    void*)
{
    return true;
};

bool WriteImageData(
    const std::string* basepath,
    const std::string* filename,
    const tinygltf::Image* image,
    bool embedImages,
    const tinygltf::URICallbacks* uri_cb,
    std::string* out_uri,
    void*)
{
    return true;
};

} // end of namespace tinygltf

namespace
{
static const std::string GLTF_POSITIONS_ACCESSOR{"POSITION"};
static const std::string GLTF_NORMALS_ACCESSOR{"NORMAL"};
static const std::string GLTF_TANGENTS_ACCESSOR{"TANGENT"};
static const std::string GLTF_UVS_ACCESSOR{"TEXCOORD_0"};

static const std::string GLTF_SAMPLER_PATH_TRANSLATION{"translation"};
static const std::string GLTF_SAMPLER_PATH_ROTATION{"rotation"};
static const std::string GLTF_SAMPLER_PATH_SCALE{"scale"};

glm::vec3 tg2glm(const std::vector<double>& vec)
{
    return {vec[0], vec[1], vec[2]};
}

glm::quat tg2glmQuat(const std::vector<double>& vec)
{
    return {
        static_cast<float>(vec[3]), // w
        static_cast<float>(vec[0]), // x
        static_cast<float>(vec[1]), // y
        static_cast<float>(vec[2]), // z
    };
}

template<typename T>
std::span<const T> getPackedBufferSpan(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor)
{
    const auto& bv = model.bufferViews[accessor.bufferView];
    const int bs = accessor.ByteStride(bv);
    assert(bs == sizeof(T)); // check that it's packed
    const auto& buf = model.buffers[bv.buffer];
    const auto* data =
        reinterpret_cast<const T*>(&buf.data.at(0) + bv.byteOffset + accessor.byteOffset);
    return std::span{data, accessor.count};
}

int findAttributeAccessor(const tinygltf::Primitive& primitive, const std::string& attributeName)
{
    for (const auto& [accessorName, accessorID] : primitive.attributes) {
        if (accessorName == attributeName) {
            return accessorID;
        }
    }
    return -1;
}

template<typename T>
std::span<const T> getPackedBufferSpan(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    const std::string& attributeName)
{
    const auto accessorIndex = findAttributeAccessor(primitive, attributeName);
    assert(accessorIndex != -1 && "Accessor not found");
    const auto& accessor = model.accessors[accessorIndex];
    return getPackedBufferSpan<T>(model, accessor);
}

bool hasAccessor(const tinygltf::Primitive& primitive, const std::string& attributeName)
{
    const auto accessorIndex = findAttributeAccessor(primitive, attributeName);
    return accessorIndex != -1;
}

std::filesystem::path getDiffuseTexturePath(
    const tinygltf::Model& model,
    const tinygltf::Material& material)
{
    const auto textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
    const auto& textureId = model.textures[textureIndex];
    const auto& image = model.images[textureId.source];
    return image.uri;
}

Mesh loadMesh(
    const tinygltf::Model& model,
    const std::string& meshName,
    const tinygltf::Primitive& primitive)
{
    Mesh mesh;
    mesh.name = meshName;

    if (primitive.material != -1) {
        mesh.diffuseTexturePath = getDiffuseTexturePath(model, model.materials[primitive.material]);
    }

    if (primitive.indices != -1) { // load indices
        const auto& indexAccessor = model.accessors[primitive.indices];
        const auto indices = getPackedBufferSpan<std::uint16_t>(model, indexAccessor);
        mesh.indices.assign(indices.begin(), indices.end());
        util::padBufferToFourBytes(mesh.indices);
    }

    // resize vertices based on the first accessor size
    for (const auto& [accessorName, accessorID] : primitive.attributes) {
        const auto& accessor = model.accessors[accessorID];
        mesh.vertices.resize(accessor.count);
        break;
    }

    // load positions
    const auto positions =
        getPackedBufferSpan<glm::vec3>(model, primitive, GLTF_POSITIONS_ACCESSOR);
    assert(positions.size() == mesh.vertices.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        mesh.vertices[i].pos = positions[i];
    }

    // load normals
    if (hasAccessor(primitive, GLTF_NORMALS_ACCESSOR)) {
        const auto normals =
            getPackedBufferSpan<glm::vec3>(model, primitive, GLTF_NORMALS_ACCESSOR);
        assert(normals.size() == mesh.vertices.size());
        for (std::size_t i = 0; i < normals.size(); ++i) {
            mesh.vertices[i].normal = normals[i];
        }
    }

    // load tangents
    if (hasAccessor(primitive, GLTF_TANGENTS_ACCESSOR)) {
        const auto tangents =
            getPackedBufferSpan<glm::vec4>(model, primitive, GLTF_TANGENTS_ACCESSOR);
        assert(tangents.size() == mesh.vertices.size());
        for (std::size_t i = 0; i < tangents.size(); ++i) {
            mesh.vertices[i].tangent = tangents[i];
        }
    }

    // load uvs
    if (hasAccessor(primitive, GLTF_UVS_ACCESSOR)) {
        const auto uvs = getPackedBufferSpan<glm::vec2>(model, primitive, GLTF_UVS_ACCESSOR);
        assert(uvs.size() == mesh.vertices.size());
        for (std::size_t i = 0; i < uvs.size(); ++i) {
            mesh.vertices[i].uv = uvs[i];
        }
    }

    return mesh;
}

}

namespace util
{

Model loadModel(const std::filesystem::path& path)
{
    Model model;

    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(::LoadImageData, nullptr);
    loader.SetImageWriter(::WriteImageData, nullptr);

    tinygltf::Model gltfModel;
    std::string err;
    std::string warn;

    bool res = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path.string());
    if (!warn.empty()) {
        printf("WARN: %s\n", warn.c_str());
    }
    if (!res) {
        printf("Failed to load glTF scene: %s\n", path.c_str());
        if (!err.empty()) {
            printf("ERR: %s\n", err.c_str());
        }
        assert(false);
    }

    auto& scene = gltfModel.scenes[gltfModel.defaultScene];
    auto& gltfNode = gltfModel.nodes[scene.nodes[0]];
    auto& mesh = gltfModel.meshes[gltfNode.mesh];
    if (!gltfNode.translation.empty()) {
        model.position = tg2glm(gltfNode.translation);
    }
    if (!gltfNode.scale.empty()) {
        model.scale = tg2glm(gltfNode.scale);
    }
    if (!gltfNode.rotation.empty()) {
        model.rotation = tg2glmQuat(gltfNode.rotation);
    }

    for (const auto& p : mesh.primitives) {
        Mesh mesh = loadMesh(gltfModel, mesh.name, p);
        model.meshes.push_back(std::move(mesh));
    }

    return model;
}
}
