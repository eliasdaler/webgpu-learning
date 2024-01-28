#include "MeshCache.h"

MeshId MeshCache::addMesh(GPUMesh mesh)
{
    const auto id = meshes.size();
    meshes.push_back(std::move(mesh));
    return id;
}

const GPUMesh& MeshCache::getMesh(MeshId id) const
{
    return meshes.at(id);
}
