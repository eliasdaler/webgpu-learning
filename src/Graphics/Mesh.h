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
        glm::vec2 uv;
        glm::vec3 normal;
        glm::vec4 tangent;

        std::array<JointId, MAX_BONE_INFLUENCE>
            jointIds{NULL_JOINT_ID, NULL_JOINT_ID, NULL_JOINT_ID, NULL_JOINT_ID};
        std::array<float, MAX_BONE_INFLUENCE> weights{};
    };
    static_assert(sizeof(Mesh::Vertex) % 4 == 0);

    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;

    std::string name;
};
