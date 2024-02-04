#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Graphics/Skeleton.h>

struct Mesh {
    std::vector<std::uint16_t> indices;

    std::vector<glm::vec4> positions;
    std::vector<glm::vec4> normals;
    std::vector<glm::vec4> tangents;
    std::vector<glm::vec2> uvs;

    // skinned meshes only
    std::vector<glm::vec<4, std::uint32_t>> jointIds;
    std::vector<glm::vec4> weights;

    bool hasSkeleton{false};

    std::string name;
};
