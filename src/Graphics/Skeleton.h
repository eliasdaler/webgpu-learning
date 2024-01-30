#pragma once

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

using JointId = std::uint32_t;
static const std::uint32_t NULL_JOINT_ID = std::numeric_limits<std::uint32_t>::max();

struct Joint {
    JointId id{NULL_JOINT_ID};
    glm::mat4 inverseBindMatrix{1.f};
};

struct Skeleton {
    std::unordered_map<JointId, std::string> jointNames;
    std::vector<Joint> joints;

    struct JointNode {
        JointId parent{NULL_JOINT_ID};
        std::vector<JointId> children;
    };
    std::vector<JointNode> hierarchy;
};
