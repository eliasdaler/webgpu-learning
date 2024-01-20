#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct Mesh {
    struct Vertex {
        glm::vec3 pos;
        glm::vec2 uv;
        glm::vec3 normal;
        glm::vec4 tangent;
    };

    ~Mesh();

    void initGeometry();

    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;

    std::string diffuseTexturePath;
    std::string name;
};
