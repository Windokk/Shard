// GPU skinning. The engine compiles a variant of a geometry shader with SKINNED defined for a skinned mesh that is posed.
//
// The vertices carry their 4 joints and weights (locations 6 and 7) ; the bone matrices of every posed object of the frame sit in one buffer (binding 28),
// and `uBoneOffset` is where the matrices of the object being drawn start. A bone matrix is the bone's transform in the mesh's space times its inverse
// bind pose : the weighted sum of the ones a vertex follows moves it from the bind pose to the pose.
//
// A shader of your own gets the same : `#include "..."` this file is not possible outside the engine's resources, so copy these lines under `#ifdef SKINNED`.
#ifdef SKINNED
layout(location = 6) in vec4 aJoints;
layout(location = 7) in vec4 aWeights;

layout(std430, binding = 28) readonly buffer SkinBones { mat4 skinBones[]; };
uniform int uBoneOffset;

mat4 SkinMatrix()
{
    // The joints come as floats (whole numbers) so every backend reads them alike
    return aWeights.x * skinBones[uBoneOffset + int(aJoints.x + 0.5)]
         + aWeights.y * skinBones[uBoneOffset + int(aJoints.y + 0.5)]
         + aWeights.z * skinBones[uBoneOffset + int(aJoints.z + 0.5)]
         + aWeights.w * skinBones[uBoneOffset + int(aJoints.w + 0.5)];
}
#endif
