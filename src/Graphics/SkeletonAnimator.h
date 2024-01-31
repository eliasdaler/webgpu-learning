#pragma once

#include <string>

#include <glm/mat4x4.hpp>

struct SkeletalAnimation;
struct Skeleton;

class SkeletonAnimator {
public:
    void setAnimation(Skeleton& skeleton, const SkeletalAnimation& animation);

    void update(Skeleton& skeleton, float dt);

    const SkeletalAnimation* getAnimation() const { return animation; }
    const std::string& getCurrentAnimationName() const;

    bool isAnimationFinished() const { return animationFinished; }

    float getProgress() const { return time; }

private:
    void updateTransforms(Skeleton& skeleton);

    float time{0}; // current animation time
    // TODO: use shared_ptr here? (so that we can see which anims are still in use?)
    const SkeletalAnimation* animation{nullptr};

    bool animationFinished{false};

    bool firstFrame = true;
    bool frameChanged{false};
};
