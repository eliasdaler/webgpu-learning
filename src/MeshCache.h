#pragma once

#include <vector>

#include <Graphics/GPUMesh.h>

class MeshCache {
public:
    MeshId addMesh(GPUMesh mesh);

    const GPUMesh& getMesh(MeshId id) const;

private:
    std::vector<GPUMesh> meshes;
};
