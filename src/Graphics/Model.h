#pragma once

#include "Mesh.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

struct Model {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;

    std::vector<Mesh> meshes;
};
