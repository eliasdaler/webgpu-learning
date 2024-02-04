#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Graphics/Skeleton.h>

struct Mesh {
    static constexpr std::size_t MAX_BONE_INFLUENCE = 4;

    struct Vertex {
        glm::vec3 pos;
        float uv_x;

        glm::vec3 normal;
        float uv_y;

        glm::vec4 tangent;

        std::array<JointId, MAX_BONE_INFLUENCE>
            jointIds{NULL_JOINT_ID, NULL_JOINT_ID, NULL_JOINT_ID, NULL_JOINT_ID};
        std::array<float, MAX_BONE_INFLUENCE> weights{};
    };
    static_assert(sizeof(Mesh::Vertex) % 4 == 0);
    static_assert(alignof(glm::vec3) == 4);

    std::vector<Vertex> vertices;

    std::vector<glm::vec4> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> normals;
    std::vector<glm::vec4> tangents;
    std::vector<glm::vec<4, JointId>> jointIds;
    std::vector<glm::vec4> weights;

    std::vector<std::uint16_t> indices;

    std::string name;
};
