#pragma once

#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

struct SkeletalAnimation {
    struct PositionKey {
        glm::vec3 pos;
        float time;
    };

    struct RotationKey {
        glm::quat quat;
        float time;
    };

    struct ScalingKey {
        glm::vec3 scale;
        float time;
    };

    std::vector<std::vector<PositionKey>> positionKeys;
    std::vector<std::vector<RotationKey>> rotationKeys;
    std::vector<std::vector<ScalingKey>> scalingKeys;

    std::string name;
    float duration{0.f}; // in seconds
    bool looped{true};
};
