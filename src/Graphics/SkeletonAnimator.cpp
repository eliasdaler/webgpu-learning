#include <Graphics/SkeletonAnimator.h>

#include <Graphics/SkeletalAnimation.h>
#include <Graphics/Skeleton.h>

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

void updateJointLocalTransform(
    JointId jointId,
    Transform& transform,
    const SkeletalAnimation& animation,
    float time)
{
    // translation
    const auto& posKeys = animation.positionKeys[jointId];
    updateJointTransform(
        transform,
        posKeys,
        time,
        [&posKeys](Transform& transform, std::size_t prevKey, std::size_t nextKey, float t) {
            const auto& prevPos = posKeys[prevKey].pos;
            const auto& nextPos = posKeys[nextKey].pos;
            transform.position = glm::lerp(prevPos, nextPos, t);
        });

    // rotation
    const auto& rotKeys = animation.rotationKeys[jointId];
    updateJointTransform(
        transform,
        rotKeys,
        time,
        [&rotKeys](Transform& transform, std::size_t prevKey, std::size_t nextKey, float t) {
            const auto& prevHeading = rotKeys[prevKey].quat;
            const auto& nextHeading = rotKeys[nextKey].quat;
            transform.heading = glm::slerp(prevHeading, nextHeading, t);
        });

    // scaling
    const auto& scalingKeys = animation.scalingKeys[jointId];
    if (scalingKeys.empty()) {
        return;
    }

    updateJointTransform(
        transform,
        scalingKeys,
        time,
        [&scalingKeys](Transform& transform, std::size_t prevKey, std::size_t nextKey, float t) {
            const auto& prevScale = scalingKeys[prevKey].scale;
            const auto& nextScale = scalingKeys[nextKey].scale;
            transform.scale = glm::lerp(prevScale, nextScale, t);
        });
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
