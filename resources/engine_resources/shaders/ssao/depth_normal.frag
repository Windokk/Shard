#version 430 core

// See depth_normal.vert. Depth is written automatically by the standard depth test against this pass's
// framebuffer (see SSAOManager::Init() - hasDepth = true) ; this fragment shader's only job is the
// second attachment, view-space normal (RGB16F - needs signed range, not [0,1]-encoded).
in vec3 vViewNormal;

out vec3 gNormal;

void main()
{
    gNormal = normalize(vViewNormal);
}
