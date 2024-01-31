#pragma once

#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

struct TranslationChannel {
    std::vector<glm::vec3> translations;
};

struct RotationChannel {
    std::vector<glm::quat> rotations;
};

struct ScaleChannel {
    std::vector<glm::vec3> scales;
};

struct SkeletalAnimation {
    std::vector<TranslationChannel> translationChannels;
    std::vector<RotationChannel> rotationChannels;
    std::vector<ScaleChannel> scaleChannels;

    std::string name;
    float duration{0.f}; // in seconds
    bool looped{true};
};
