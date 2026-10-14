#version 430 core

// The vertex stage of the deferred lighting pass (see mesh/lit.frag, permutation SHARD_DEFERRED_LIGHTING) : a quad that covers the screen, drawn as a
// mesh so that the lights, the shadows and the probes get bound to the draw like they are for any mesh.
layout (location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
