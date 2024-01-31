#include <Graphics/SkeletonAnimator.h>

#include <Graphics/SkeletalAnimation.h>
#include <Graphics/Skeleton.h>

#include <glm/gtx/compatibility.hpp> // lerp for vec3

#include <iostream>

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
    frameChanged = false; // ideally should be "true", but update will override it
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
// prevKey's time <= time && nextKey's time >= time
template<typename KeyType>
std::pair<std::size_t, std::size_t> findPrevNextKeys(const std::vector<KeyType>& keys, float time)
{
    assert(!keys.empty());
    std::size_t prevKey{0}, nextKey{0};
    for (std::size_t i = 0; i < keys.size() - 1; ++i) {
        prevKey = i;
        nextKey = i + 1;
        if (keys[nextKey].time >= time) {
            break;
        }
    }
    return {prevKey, nextKey};
}

template<typename KeyType, typename InterpolationFuncType>
void updateJointTransform(
    Transform& transform,
    const std::vector<KeyType>& keys,
    float time,
    InterpolationFuncType f)
{
    if (keys.empty()) {
        return;
    }

    const auto [prevKey, nextKey] = findPrevNextKeys(keys, time);
    if (prevKey == nextKey) { // reached animation's end
        return;
    }

    const auto totalTime = keys[nextKey].time - keys[prevKey].time;
    const auto t = (time - keys[prevKey].time) / totalTime;
    return f(transform, prevKey, nextKey, t);
}

std::tuple<std::size_t, std::size_t, float> findPrevNextKeys2(
    std::size_t numKeys,
    float time,
    float animationDuration)
{
    std::size_t prevKey = std::min((std::size_t)std::floor(time * ANIMATION_FPS), numKeys - 1);
    std::size_t nextKey = std::min(prevKey + 1, numKeys - 1);
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
                findPrevNextKeys2(tc.translations.size(), time, animation.duration);
            transform.position = glm::lerp(tc.translations[p], tc.translations[n], t);
        }
    }

    {
        const auto& rc = animation.rotationChannels[jointId];
        if (!rc.rotations.empty()) {
            const auto [p, n, t] = findPrevNextKeys2(rc.rotations.size(), time, animation.duration);
            transform.heading = glm::slerp(rc.rotations[p], rc.rotations[n], t);
        }
    }

    {
        const auto& sc = animation.scaleChannels[jointId];
        if (!sc.scales.empty()) {
            const auto [p, n, t] = findPrevNextKeys2(sc.scales.size(), time, animation.duration);
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
