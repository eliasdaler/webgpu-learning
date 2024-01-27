#include "GltfLoader.h"

#include <cassert>
#include <iostream>
#include <span>

#include <Graphics/GPUMesh.h>
#include <Graphics/Scene.h>

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

bool hasDiffuseTexture(const tinygltf::Material& material)
{
    const auto textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
    return textureIndex != -1;
}

glm::vec4 getDiffuseColor(const tinygltf::Material& material)
{
    const auto c = material.pbrMetallicRoughness.baseColorFactor;
    assert(c.size() == 4);
    return {(float)c[0], (float)c[1], (float)c[2], (float)c[3]};
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
    const tinygltf::Material& material,
    const std::filesystem::path& fileDir)
{
    const auto textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
    const auto& textureId = model.textures[textureIndex];
    const auto& image = model.images[textureId.source];
    return fileDir / image.uri;
}

void loadPrimitive(
    const tinygltf::Model& model,
    const std::string& meshName,
    const tinygltf::Primitive& primitive,
    Mesh& mesh)
{
    mesh.name = meshName;

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
}

void loadFile(tinygltf::Model& gltfModel, const std::filesystem::path& path)
{
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(::LoadImageData, nullptr);
    loader.SetImageWriter(::WriteImageData, nullptr);

    std::string err;
    std::string warn;

    bool res = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path.string());
    if (!warn.empty()) {
        std::cout << "WARNING: " << warn << std::endl;
    }
    if (!res) {
        std::cout << "Failed to load glTF scene " << path << std::endl;
        if (!err.empty()) {
            std::cout << "ERROR: " << err << std::endl;
        }
        assert(false);
    }
}

void loadMaterial(
    const util::LoadContext& ctx,
    Material& material,
    const std::filesystem::path& diffusePath)
{
    material.diffuseTexture =
        util::loadTexture(ctx.device, ctx.queue, diffusePath, wgpu::TextureFormat::RGBA8UnormSrgb);

    { // material data

        { // data buffer
            const auto bufferDesc = wgpu::BufferDescriptor{
                .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
                .size = sizeof(MaterialData),
            };

            material.dataBuffer = ctx.device.CreateBuffer(&bufferDesc);

            const auto md = MaterialData{
                .baseColor = material.baseColor,
            };
            ctx.queue.WriteBuffer(material.dataBuffer, 0, &md, sizeof(MaterialData));
        }

        const auto textureViewDesc = wgpu::TextureViewDescriptor{
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::All,
        };
        const auto textureView = material.diffuseTexture.CreateView(&textureViewDesc);

        const std::array<wgpu::BindGroupEntry, 3> bindings{{
            {
                .binding = 0,
                .buffer = material.dataBuffer,
            },
            {
                .binding = 1,
                .textureView = textureView,
            },
            {
                .binding = 2,
                .sampler = ctx.defaultSampler,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = ctx.materialLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        material.bindGroup = ctx.device.CreateBindGroup(&bindGroupDesc);
    }
}

void loadGPUMesh(const util::LoadContext ctx, const Mesh& cpuMesh, GPUMesh& gpuMesh)
{
    // copy-pasted from Graphics/Util.cpp, but become a lot more complex soon

    { // vertex buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            .size = cpuMesh.vertices.size() * sizeof(Mesh::Vertex),
        };

        gpuMesh.vertexBuffer = ctx.device.CreateBuffer(&bufferDesc);
        ctx.queue.WriteBuffer(gpuMesh.vertexBuffer, 0, cpuMesh.vertices.data(), bufferDesc.size);
    }

    { // index buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
            .size = cpuMesh.indices.size() * sizeof(std::uint16_t),
        };

        gpuMesh.indexBuffer = ctx.device.CreateBuffer(&bufferDesc);
        ctx.queue.WriteBuffer(gpuMesh.indexBuffer, 0, cpuMesh.indices.data(), bufferDesc.size);
    }
}

bool shouldSkipNode(const tinygltf::Node& node)
{
    if (node.mesh == -1) {
        return true;
    }

    if (node.light != -1) {
        return true;
    }

    if (node.camera != -1) {
        return true;
    }

    if (node.name.starts_with("Collision") || node.name.starts_with("Trigger") ||
        node.name.starts_with("PlayerSpawn") || node.name.starts_with("Interact")) {
        return true;
    }

    return false;
}

Transform loadTransform(const tinygltf::Node& gltfNode)
{
    Transform transform;
    if (!gltfNode.translation.empty()) {
        transform.position = tg2glm(gltfNode.translation);
    }
    if (!gltfNode.scale.empty()) {
        transform.scale = tg2glm(gltfNode.scale);
    }
    if (!gltfNode.rotation.empty()) {
        transform.heading = tg2glmQuat(gltfNode.rotation);
    }
    return transform;
}

void loadNode(SceneNode& node, const tinygltf::Node& gltfNode, const tinygltf::Model& model)
{
    node.name = gltfNode.name;
    node.transform = loadTransform(gltfNode);
    assert(gltfNode.mesh != -1);
    node.meshIndex = static_cast<std::size_t>(gltfNode.mesh);

    // load children
    node.children.resize(gltfNode.children.size());
    for (std::size_t childIdx = 0; childIdx < gltfNode.children.size(); ++childIdx) {
        const auto& childNode = model.nodes[gltfNode.children[childIdx]];
        if (shouldSkipNode(childNode)) {
            continue;
        }

        auto& childPtr = node.children[childIdx];
        childPtr = std::make_unique<SceneNode>();
        auto& child = *childPtr;
        loadNode(child, childNode, model);
    }
}

}

namespace util
{
void loadScene(const LoadContext& ctx, Scene& scene, const std::filesystem::path& path)
{
    const auto& device = ctx.device;
    const auto& queue = ctx.queue;
    const auto& materialLayout = ctx.materialLayout;
    const auto& sampler = ctx.defaultSampler;

    const auto fileDir = path.parent_path();

    tinygltf::Model gltfModel;
    loadFile(gltfModel, path);

    const auto& gltfScene = gltfModel.scenes[gltfModel.defaultScene];

    // load materials
    scene.materials.resize(gltfModel.materials.size());
    for (std::size_t materialIdx = 0; materialIdx < gltfModel.materials.size(); ++materialIdx) {
        const auto& gltfMaterial = gltfModel.materials[materialIdx];
        auto& material = scene.materials[materialIdx];
        if (hasDiffuseTexture(gltfMaterial)) {
            // load diffuse
            const auto texturePath = getDiffuseTexturePath(gltfModel, gltfMaterial, fileDir);
            loadMaterial(ctx, material, texturePath);
            //  TODO: generate mip maps here
        } else {
            material.baseColor = getDiffuseColor(gltfMaterial);
            // TODO: bind group not created, but should be!
        }
        material.name = gltfMaterial.name;
    }

    // load meshes
    scene.meshes.resize(gltfModel.meshes.size());
    for (std::size_t meshIdx = 0; meshIdx < gltfModel.meshes.size(); ++meshIdx) {
        const auto& gltfMesh = gltfModel.meshes[meshIdx];
        auto& mesh = scene.meshes[meshIdx];
        mesh.primitives.resize(gltfMesh.primitives.size());
        for (std::size_t primitiveIdx = 0; primitiveIdx < gltfMesh.primitives.size();
             ++primitiveIdx) {
            const auto& gltfPrimitive = gltfMesh.primitives[primitiveIdx];
            auto& primitive = mesh.primitives[primitiveIdx];
            // TODO: handle case when primitive doesn't have a material
            primitive.materialIndex = static_cast<std::size_t>(gltfPrimitive.material);

            Mesh cpuMesh;
            loadPrimitive(gltfModel, gltfMesh.name, gltfPrimitive, cpuMesh);

            loadGPUMesh(ctx, cpuMesh, primitive.mesh);
        }
    }

    // load nodes
    scene.nodes.resize(gltfScene.nodes.size());
    for (std::size_t nodeIdx = 0; nodeIdx < gltfScene.nodes.size(); ++nodeIdx) {
        const auto& gltfNode = gltfModel.nodes[gltfScene.nodes[nodeIdx]];
        if (shouldSkipNode(gltfNode)) {
            continue;
        }

        auto& nodePtr = scene.nodes[nodeIdx];
        nodePtr = std::make_unique<SceneNode>();
        auto& node = *nodePtr;
        loadNode(node, gltfNode, gltfModel);
    }
}

}
