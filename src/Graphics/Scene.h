#pragma once

#include <memory>
#include <vector>

#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Math/Transform.h>

struct MeshPrimitive {
    GPUMesh mesh;
    std::size_t materialIndex;
};

struct SceneNode {
    std::string name;
    Transform transform;
    std::size_t meshIndex;

    SceneNode* parent{nullptr};
    std::vector<std::unique_ptr<SceneNode>> children;
};

struct SceneMesh {
    std::vector<MeshPrimitive> primitives;
};

struct Scene {
    std::vector<SceneMesh> meshes;
    std::vector<Material> materials;

    std::vector<std::unique_ptr<SceneNode>> nodes;
};
