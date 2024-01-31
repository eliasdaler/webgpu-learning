#include <Graphics/SkeletonAnimator.h>

#include <Graphics/SkeletalAnimation.h>
#include <Graphics/Skeleton.h>

#include <glm/gtx/compatibility.hpp> // lerp for vec3

namespace
{
static const int ANIMATION_FPS = 30;
}

void SkeletonAnimator::init(const Skeleton& skeleton)
{
    boneAnimationMats = std::vector<glm::mat4>(skeleton.joints.size(), glm::mat4{1.f});
}

void SkeletonAnimator::setAnimation(const Skeleton& skeleton, const SkeletalAnimation& animation)
{
    if (this->animation != nullptr && this->animation->name == animation.name) {
        return; // TODO: allow to reset animation
    }
    time = 0.f;
    frameChanged = false; // ideally should be "true", but update will override it
    time = static_cast<float>(animation.startFrame) / ANIMATION_FPS;
    animationFinished = false;
    this->animation = &animation;
    animate(skeleton);
}

void SkeletonAnimator::update(float dt)
{
    if (!animation) {
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
    std::size_t prevKey{0}, nextKey{0};
    const int max = static_cast<int>(keys.size()) - 1;
    for (int i = 0; i < max; ++i) {
        prevKey = static_cast<std::size_t>(i);
        nextKey = static_cast<std::size_t>(i + 1);
        if (keys[nextKey].time >= time) {
            break;
        }
    }
    return {prevKey, nextKey};
}

template<typename KeyType, typename InterpolationFuncType>
glm::mat4 getKeyTransform(const std::vector<KeyType>& keys, float time, InterpolationFuncType f)
{
    if (keys.empty()) {
        return glm::mat4{1.f};
    }

    const auto [prevKey, nextKey] = findPrevNextKeys(keys, time);
    if (prevKey == nextKey) { // reached animation's end
        return glm::mat4{1.f};
    }

    const auto totalTime = keys[nextKey].time - keys[prevKey].time;
    const auto t = (time - keys[prevKey].time) / totalTime;
    return f(prevKey, nextKey, t);
}

glm::mat4 getBoneLocalTransform(float time, const SkeletalAnimation& anim, JointId jointId)
{
    // translation
    const auto& posKeys = anim.positionKeys[jointId];
    const auto boneT = getKeyTransform(
        posKeys, time, [&posKeys](std::size_t prevKey, std::size_t nextKey, float t) {
            const auto& vi = posKeys[prevKey].pos;
            const auto& vf = posKeys[nextKey].pos;
            const auto lerped = glm::lerp(vi, vf, t);
            return glm::translate(glm::mat4{1.f}, lerped);
        });

    // rotation
    const auto& rotKeys = anim.rotationKeys[jointId];
    const auto boneR = getKeyTransform(
        rotKeys, time, [&rotKeys](std::size_t prevKey, std::size_t nextKey, float t) {
            const auto& qi = rotKeys[prevKey].quat;
            const auto& qf = rotKeys[nextKey].quat;
            const auto slerped = glm::slerp(qi, qf, t);
            return glm::mat4_cast(slerped);
        });

    // scaling
    const auto& scalingKeys = anim.scalingKeys[jointId];
    if (scalingKeys.empty()) {
        return boneT * boneR;
    }

    const auto boneS = getKeyTransform(
        scalingKeys, time, [&scalingKeys](std::size_t prevKey, std::size_t nextKey, float t) {
            const auto& vi = scalingKeys[prevKey].scale;
            const auto& vf = scalingKeys[nextKey].scale;
            const auto lerped = glm::lerp(vi, vf, t);
            return glm::scale(glm::mat4{1.f}, lerped);
        });

    return boneT * boneR * boneS;
}

void skeletonAnimate(
    float time,
    const Skeleton& skeleton,
    const SkeletalAnimation& anim,
    JointId jointId,
    const glm::mat4& parentMat,
    std::vector<glm::mat4>& jointMatrices)
{
    const auto localJointTransform = getBoneLocalTransform(time, anim, jointId);
    const auto globalJointTransform = parentMat * localJointTransform;

    const auto& inverseBindMatrix = skeleton.joints[jointId].inverseBindMatrix;
    jointMatrices[jointId] = globalJointTransform * inverseBindMatrix;

    // recurse
    for (const auto childIdx : skeleton.hierarchy[jointId].children) {
        skeletonAnimate(time, skeleton, anim, childIdx, globalJointTransform, jointMatrices);
    }
}
} // end of anonymous namespace

void SkeletonAnimator::animate(const Skeleton& skeleton)
{
    if (!animation) {
        return;
    }
    assert(!boneAnimationMats.empty()); // forgot to call init?

    skeletonAnimate(time, skeleton, *animation, 0, glm::mat4{1.f}, boneAnimationMats);
}
