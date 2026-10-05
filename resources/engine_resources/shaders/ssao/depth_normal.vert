#version 430 core

// SSAO depth+normal prepass (see SSAOManager) - the engine is a pure forward renderer with no G-buffer,
// so this is a dedicated geometry pass whose only job is to leave a view-space normal (this file) plus
// depth (the standard depth test/write, no extra work needed) behind for ssao.frag to sample. Rendered
// through every mesh's real vertex data via RenderPass::overridePipeline, same mechanism
// ShadowManager uses to render shadow-casters through shadow_dir.vert instead of each mesh's own
// material shader - see SSAOManager::Init().
//
// Only aPos/aNormal are declared (not aTexCoord/aColor/aTangent at locations 1/3/4) - every mesh's VAO
// already has all 5 attributes bound at their fixed locations (see GLMesh::GenerateGLBuffers, driven by
// the mesh's own stored layout, not whatever pipeline happens to be bound), a shader is free to just not
// declare the ones it doesn't need.
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vViewNormal;

void main()
{
    // Same "ignore non-uniform scale" shortcut lit.vert already uses for its own (world-space) normal
    // transform (mat3(model) * aNormal, no inverse-transpose) - consistent precision, not a regression.
    mat3 viewModel = mat3(view * model);
    vViewNormal = normalize(viewModel * aNormal);

    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
