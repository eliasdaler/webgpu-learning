#include <Graphics/SkeletonAnimator.h>

#include <Graphics/SkeletalAnimation.h>
#include <Graphics/Skeleton.h>

#include <tuple>

#include <glm/gtx/compatibility.hpp> // lerp for vec3

namespace
{
static const int ANIMATION_FPS = 30;
}

void SkeletonAnimator::setAnimation(const Skeleton& skeleton, const SkeletalAnimation& animation)
{
    if (this->animation != nullptr && this->animation->name == animation.name) {
        return; // TODO: allow to reset animation
    }

    jointMatrices.resize(skeleton.joints.size());

    time = 0.f;
    animationFinished = false;
    this->animation = &animation;
    calculateJointMatrices(skeleton);
}

void SkeletonAnimator::update(const Skeleton& skeleton, float dt)
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

    static const glm::mat4 I{1.f};
    calculateJointMatrix(skeleton, ROOT_JOINT_ID, *animation, time, I);
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

Transform sampleAnimation(const SkeletalAnimation& animation, JointId jointId, float time)
{
    Transform transform{};

    { // translation
        const auto& tc = animation.translationChannels[jointId];
        if (!tc.translations.empty()) {
            const auto [p, n, t] =
                findPrevNextKeys(tc.translations.size(), time, animation.duration);
            transform.position = glm::lerp(tc.translations[p], tc.translations[n], t);
        }
    }

    { // rotation
        const auto& rc = animation.rotationChannels[jointId];
        if (!rc.rotations.empty()) {
            const auto [p, n, t] = findPrevNextKeys(rc.rotations.size(), time, animation.duration);
            // slerp is slower, lerp is good enough
            transform.heading = glm::lerp(rc.rotations[p], rc.rotations[n], t);
        }
    }

    { // scale
        const auto& sc = animation.scaleChannels[jointId];
        if (!sc.scales.empty()) {
            const auto [p, n, t] = findPrevNextKeys(sc.scales.size(), time, animation.duration);
            transform.scale = glm::lerp(sc.scales[p], sc.scales[n], t);
        }
    }

    return transform;
}

} // end of anonymous namespace

void SkeletonAnimator::calculateJointMatrices(const Skeleton& skeleton)
{
    static const glm::mat4 I{1.f};
    calculateJointMatrix(skeleton, ROOT_JOINT_ID, *animation, time, I);
}

void SkeletonAnimator::calculateJointMatrix(
    const Skeleton& skeleton,
    JointId jointId,
    const SkeletalAnimation& animation,
    float time,
    const glm::mat4& parentTransform)
{
    const auto localTransform = sampleAnimation(animation, jointId, time);
    const auto globalJointTransform = parentTransform * localTransform.asMatrix();
    jointMatrices[jointId] = globalJointTransform * skeleton.inverseBindMatrices[jointId];

    for (const auto childIdx : skeleton.hierarchy[jointId].children) {
        calculateJointMatrix(skeleton, childIdx, animation, time, globalJointTransform);
    }
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
