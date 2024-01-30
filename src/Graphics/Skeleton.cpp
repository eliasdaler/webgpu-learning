#include <Graphics/Skeleton.h>

void Skeleton::updateTransforms()
{
    updateTransforms(0, glm::mat4{1.f});
}

void Skeleton::updateTransforms(JointId id, const glm::mat4& parentGlobalTransform)
{
    auto& joint = joints[id];
    joint.globalJointTransform = joint.localTransform.asMatrix() * parentGlobalTransform;
    jointMatrices[id] = glm::inverse(joint.globalJointTransform) * joint.globalJointTransform *
                        joint.inverseBindMatrix;

    for (const auto childJointId : hierarchy[id].children) {
        updateTransforms(childJointId, joint.globalJointTransform);
    }
}
