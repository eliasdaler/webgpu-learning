#include <Graphics/SkeletonAnimator.h>

#include <Graphics/SkeletalAnimation.h>
#include <Graphics/Skeleton.h>

#include <tuple>

#include <glm/gtx/compatibility.hpp> // lerp for vec3

namespace
{
static const int ANIMATION_FPS = 30;
}

void SkeletonAnimator::setAnimation(Skeleton& skeleton, const SkeletalAnimation& animation)
{
    if (this->animation != nullptr && this->animation->name == animation.name) {
        return; // TODO: allow to reset animation
    }
    time = 0.f;
    animationFinished = false;
    this->animation = &animation;
    updateTransforms(skeleton);
}

void SkeletonAnimator::update(Skeleton& skeleton, float dt)
{
    if (!animation || animationFinished) {
        return;
    }

    time += dt;
    if (time > animation->duration) { // loop
        if (animation->looped) {
            time -= animation->duration;
        } else {
            time = animation->duration;
            animationFinished = true;
        }
    }
    updateTransforms(skeleton);
}

const std::string& SkeletonAnimator::getCurrentAnimationName() const
{
    static const std::string nullAnimationName{};
    return animation ? animation->name : nullAnimationName;
}

namespace
{

std::tuple<std::size_t, std::size_t, float> findPrevNextKeys(
    std::size_t numKeys,
    float time,
    float animationDuration)
{
    // keys are sampled by ANIMATION_FPS, so finding prev/next key is easy
    const std::size_t prevKey =
        std::min((std::size_t)std::floor(time * ANIMATION_FPS), numKeys - 1);
    const std::size_t nextKey = std::min(prevKey + 1, numKeys - 1);

    float t = 0.0f;
    if (prevKey != nextKey) {
        t = time * ANIMATION_FPS - (float)prevKey;
    }

    return {prevKey, nextKey, t};
}

void updateJointLocalTransform(
    JointId jointId,
    Transform& transform,
    const SkeletalAnimation& animation,
    float time)
{
    {
        const auto& tc = animation.translationChannels[jointId];
        if (!tc.translations.empty()) {
            const auto [p, n, t] =
                findPrevNextKeys(tc.translations.size(), time, animation.duration);
            transform.position = glm::lerp(tc.translations[p], tc.translations[n], t);
        }
    }

    {
        const auto& rc = animation.rotationChannels[jointId];
        if (!rc.rotations.empty()) {
            const auto [p, n, t] = findPrevNextKeys(rc.rotations.size(), time, animation.duration);
            transform.heading = glm::slerp(rc.rotations[p], rc.rotations[n], t);
        }
    }

    {
        const auto& sc = animation.scaleChannels[jointId];
        if (!sc.scales.empty()) {
            const auto [p, n, t] = findPrevNextKeys(sc.scales.size(), time, animation.duration);
            transform.scale = glm::lerp(sc.scales[p], sc.scales[n], t);
        }
    }

    return;
}

void skeletonAnimate(
    Skeleton& skeleton,
    JointId jointId,
    const SkeletalAnimation& animation,
    float time)
{
    auto& joint = skeleton.joints[jointId];
    updateJointLocalTransform(jointId, joint.localTransform, animation, time);
    for (const auto childIdx : skeleton.hierarchy[jointId].children) {
        skeletonAnimate(skeleton, childIdx, animation, time);
    }
}
} // end of anonymous namespace

void SkeletonAnimator::updateTransforms(Skeleton& skeleton)
{
    skeletonAnimate(skeleton, ROOT_JOINT_ID, *animation, time);
    skeleton.updateTransforms();
}

void SkeletonAnimator::setNormalizedProgress(float t)
{
    assert(t >= 0.f && t <= 1.f);
    time = t * animation->duration;
}

float SkeletonAnimator::getNormalizedProgress() const
{
    if (!animation) {
        return 0.f;
    }
    return time / animation->duration;
}
