#include "GltfLoader.h"

#include <cassert>
#include <iostream>
#include <span>

#include <Graphics/GPUMesh.h>
#include <Graphics/Scene.h>
#include <Graphics/Skeleton.h>

#include <util/WebGPUUtil.h>

#include <MaterialCache.h>
#include <MeshCache.h>

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
static const std::string GLTF_JOINTS_ACCESSOR{"JOINTS_0"};
static const std::string GLTF_WEIGHTS_ACCESSOR{"WEIGHTS_0"};

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

    // load weights
    if (hasAccessor(primitive, GLTF_JOINTS_ACCESSOR)) {
        // assume that less that 256 bones for now (TODO: fix)
        const auto joints =
            getPackedBufferSpan<std::uint8_t[4]>(model, primitive, GLTF_JOINTS_ACCESSOR);
        const auto weights = getPackedBufferSpan<float[4]>(model, primitive, GLTF_WEIGHTS_ACCESSOR);

        const auto numVertices = mesh.vertices.size();
        assert(joints.size() == numVertices);
        assert(weights.size() == numVertices);

        for (std::size_t i = 0; i < joints.size(); ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                // NOTE: this works because jointId == joint index in skin
                // (see how skeletons are loaded)
                mesh.vertices[i].jointIds[j] = joints[i][j];
            }
        }

        for (std::size_t i = 0; i < weights.size(); ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                mesh.vertices[i].weights[j] = weights[i][j];
            }
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
    auto texFormat = wgpu::TextureFormat::RGBA8Unorm;
    if (!diffusePath.empty()) {
        texFormat = wgpu::TextureFormat::RGBA8UnormSrgb;
        material.diffuseTexture = util::loadTexture(ctx.device, ctx.queue, diffusePath, texFormat);
    } else {
        material.diffuseTexture = ctx.whiteTexture;
    }

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
            .format = texFormat,
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
        gpuMesh.indexBufferSize = cpuMesh.indices.size();
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

    node.skinId = gltfNode.skin;

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

Skeleton loadSkeleton(
    std::unordered_map<int, JointId>& gltfNodeIdxToJointId,
    const tinygltf::Model& model,
    const tinygltf::Skin& skin)
{
    // load inverse bind matrices
    const auto& ibAccessor = model.accessors[skin.inverseBindMatrices];
    const auto ibs = getPackedBufferSpan<glm::mat4>(model, ibAccessor);
    std::vector<glm::mat4> ibMatrices(ibAccessor.count);
    ibMatrices.assign(ibs.begin(), ibs.end());

    const auto numJoints = skin.joints.size();
    Skeleton skeleton;
    skeleton.joints.reserve(numJoints);
    skeleton.jointMatrices.reserve(numJoints);
    skeleton.jointNames.reserve(numJoints);

    gltfNodeIdxToJointId.reserve(numJoints);
    { // load joints
        JointId jointId{0};
        for (const auto& nodeIdx : skin.joints) {
            gltfNodeIdxToJointId.emplace(nodeIdx, jointId);

            const auto& jointNode = model.nodes[nodeIdx];
            skeleton.jointNames.emplace(jointId, jointNode.name);

            skeleton.joints.push_back(Joint{
                .id = jointId,
                .inverseBindMatrix = ibMatrices[jointId],
                .localTransform = loadTransform(jointNode),
            });
            skeleton.jointMatrices.push_back(glm::mat4{1.f});

            ++jointId;
        }
    }

    { // build hierarchy
        skeleton.hierarchy.resize(numJoints);
        for (JointId jointId = 0; jointId < skeleton.joints.size(); ++jointId) {
            const auto& jointNode = model.nodes[skin.joints[jointId]];
            for (const auto& childIdx : jointNode.children) {
                const auto childJointId = gltfNodeIdxToJointId.at(childIdx);
                skeleton.hierarchy[jointId].children.push_back(childJointId);
            }
        }
    }

    skeleton.updateTransforms();

    return skeleton;
}

std::unordered_map<std::string, SkeletalAnimation> loadAnimations(
    const Skeleton& skeleton,
    const std::unordered_map<int, JointId>& gltfNodeIdxToJointId,
    const tinygltf::Model& gltfModel)
{
    std::unordered_map<std::string, SkeletalAnimation> animations(gltfModel.animations.size());
    for (const auto& gltfAnimation : gltfModel.animations) {
        auto& animation = animations[gltfAnimation.name];
        animation.name = gltfAnimation.name;

        const auto numJoints = skeleton.joints.size();

        animation.positionKeys.resize(numJoints);
        animation.rotationKeys.resize(numJoints);
        animation.scalingKeys.resize(numJoints);

        for (const auto& channel : gltfAnimation.channels) {
            const auto& sampler = gltfAnimation.samplers[channel.sampler];

            const auto& timesAccessor = gltfModel.accessors[sampler.input];
            const auto times = getPackedBufferSpan<float>(gltfModel, timesAccessor);

            animation.duration =
                static_cast<float>(timesAccessor.maxValues[0] - timesAccessor.minValues[0]);
            if (animation.duration == 0) {
                continue; // skip empty animations (e.g. keying sets)
            }

            if (channel.target_path == "weights") {
                // FIXME: find out why this channel exists
                // no idea what this is, but sometimes breaks stuff
                continue;
            }

            const auto nodeId = channel.target_node;
            const auto jointId = gltfNodeIdxToJointId.at(nodeId);

            const auto& outputAccessor = gltfModel.accessors[sampler.output];
            assert(outputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            if (channel.target_path == GLTF_SAMPLER_PATH_TRANSLATION) {
                const auto translationKeys =
                    getPackedBufferSpan<glm::vec3>(gltfModel, outputAccessor);
                auto& pk = animation.positionKeys[jointId];
                pk.reserve(translationKeys.size());
                assert(translationKeys.size() == times.size());
                for (std::size_t i = 0; i < translationKeys.size(); ++i) {
                    pk.push_back({translationKeys[i], times[i]});
                }
            } else if (channel.target_path == GLTF_SAMPLER_PATH_ROTATION) {
                const auto rotationKeys = getPackedBufferSpan<glm::vec4>(gltfModel, outputAccessor);
                auto& rk = animation.rotationKeys[jointId];
                rk.reserve(rotationKeys.size());
                assert(rotationKeys.size() == times.size());
                for (std::size_t i = 0; i < rotationKeys.size(); ++i) {
                    const auto& qv = rotationKeys[i];
                    const glm::quat quat{qv.w, qv.x, qv.y, qv.z};
                    rk.push_back({quat, times[i]});
                }
            } else if (channel.target_path == GLTF_SAMPLER_PATH_SCALE) {
                const auto scaleKeys =
                    getPackedBufferSpan<const glm::vec3>(gltfModel, outputAccessor);
                auto& sk = animation.scalingKeys[jointId];
                sk.reserve(scaleKeys.size());
                assert(scaleKeys.size() == times.size());
                for (std::size_t i = 0; i < scaleKeys.size(); ++i) {
                    sk.push_back({scaleKeys[i], times[i]});
                }
            } else {
                assert(false && "unexpected target_path");
            }
        }
    }

    return animations;
}

}

namespace util
{
void SceneLoader::loadScene(const LoadContext& ctx, Scene& scene, const std::filesystem::path& path)
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
    for (std::size_t materialIdx = 0; materialIdx < gltfModel.materials.size(); ++materialIdx) {
        const auto& gltfMaterial = gltfModel.materials[materialIdx];
        Material material{
            .name = gltfMaterial.name,
            .baseColor = getDiffuseColor(gltfMaterial),
        };

        std::filesystem::path diffusePath;
        if (hasDiffuseTexture(gltfMaterial)) {
            diffusePath = getDiffuseTexturePath(gltfModel, gltfMaterial, fileDir);
        }

        loadMaterial(ctx, material, diffusePath);
        auto materialId = ctx.materialCache.addMaterial(std::move(material));
        materialMapping.emplace(materialIdx, materialId);
    }

    // load meshes
    scene.meshes.reserve(gltfModel.meshes.size());
    for (const auto& gltfMesh : gltfModel.meshes) {
        SceneMesh mesh;
        mesh.primitives.resize(gltfMesh.primitives.size());
        for (std::size_t primitiveIdx = 0; primitiveIdx < gltfMesh.primitives.size();
             ++primitiveIdx) {
            const auto& gltfPrimitive = gltfMesh.primitives[primitiveIdx];

            // load on CPU
            Mesh cpuMesh;
            loadPrimitive(gltfModel, gltfMesh.name, gltfPrimitive, cpuMesh);

            // load to GPU
            GPUMesh gpuMesh;
            if (gltfPrimitive.material != -1) {
                gpuMesh.materialId = materialMapping.at(gltfPrimitive.material);
            }
            loadGPUMesh(ctx, cpuMesh, gpuMesh);

            const auto meshId = ctx.meshCache.addMesh(std::move(gpuMesh));
            mesh.primitives[primitiveIdx] = meshId;
        }
        scene.meshes.push_back(std::move(mesh));
    }

    scene.skeletons.reserve(gltfModel.skins.size());
    for (const auto& skin : gltfModel.skins) {
        scene.skeletons.push_back(loadSkeleton(gltfNodeIdxToJointId, gltfModel, skin));
    }

    // load animations
    if (!gltfModel.skins.empty()) {
        assert(gltfModel.skins.size() == 1); // for now only one skeleton supported
        scene.animations = loadAnimations(scene.skeletons[0], gltfNodeIdxToJointId, gltfModel);
    }

    // load nodes
    scene.nodes.resize(gltfScene.nodes.size());
    for (std::size_t nodeIdx = 0; nodeIdx < gltfScene.nodes.size(); ++nodeIdx) {
        const auto& gltfNode = gltfModel.nodes[gltfScene.nodes[nodeIdx]];

        // HACK: load mesh with skin (for now only one assumed)
        if (gltfNode.children.size() == 2) {
            const auto& c1 = gltfModel.nodes[gltfNode.children[0]];
            const auto& c2 = gltfModel.nodes[gltfNode.children[1]];
            if ((c1.mesh != -1 && c1.skin != -1) || (c2.mesh != -1 && c2.skin != -1)) {
                const auto& meshNode = (c1.mesh != -1) ? c1 : c2;

                auto& nodePtr = scene.nodes[nodeIdx];
                nodePtr = std::make_unique<SceneNode>();
                auto& node = *nodePtr;
                loadNode(node, meshNode, gltfModel);

                continue;
            }
        }

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
