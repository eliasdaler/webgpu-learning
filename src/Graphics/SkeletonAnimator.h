#pragma once

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

struct SkeletalAnimation;
struct Skeleton;

class SkeletonAnimator {
public:
    void init(const Skeleton& skeleton);
    void setAnimation(const Skeleton& skeleton, const SkeletalAnimation& animation);

    void update(float dt);
    void animate(const Skeleton& mesh);

    const SkeletalAnimation* getAnimation() const { return animation; }
    const std::string& getCurrentAnimationName() const;

    const std::vector<glm::mat4>& getJointMatrices() const { return boneAnimationMats; }

    bool isAnimationFinished() const { return animationFinished; }

    float getProgress() const { return time; }

private:
    float time{0}; // current animation time
    // TODO: use shared_ptr here? (so that we can see which anims are still in use?)
    const SkeletalAnimation* animation{nullptr};

    std::vector<glm::mat4> boneAnimationMats;
    bool animationFinished{false};

    bool firstFrame = true;
    bool frameChanged{false};
};
