#include <Graphics/Skeleton.h>

void Skeleton::updateTransforms()
{
    updateTransforms(0, joints[0].localTransform.asMatrix());
}

void Skeleton::updateTransforms(JointId id, const glm::mat4& parentGlobalTransform)
{
    const auto& joint = joints[id];
    const auto globalJointTransform = parentGlobalTransform * joint.localTransform.asMatrix();
    jointMatrices[id] = globalJointTransform * joint.inverseBindMatrix;
    for (const auto childJointId : hierarchy[id].children) {
        updateTransforms(childJointId, globalJointTransform);
    }
}
