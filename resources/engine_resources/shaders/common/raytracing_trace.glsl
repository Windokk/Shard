// Traversal layer of the shared BVH ray-tracing code - see raytracing_types.glsl for the struct types
// these operate on and the required include order (this file must come AFTER the including shader
// declares its own bvhNodes[]/triPos[] SSBOs, since these functions reference those global array names
// directly rather than taking buffers as parameters).

// Slab test returning the ray's ENTRY distance into the box (clamped to 0 when the ray starts inside),
// or -1.0 when the box is missed entirely / lies beyond maxT. Returning the distance rather than just a
// bool is what lets the traversal below descend into the nearer child first and skip the farther one
// outright once a closer hit has been found - see TraceRay.
float IntersectAABBDist(vec3 ro, vec3 invRd, vec3 boundsMin, vec3 boundsMax, float maxT)
{
    vec3 t0 = (boundsMin - ro) * invRd;
    vec3 t1 = (boundsMax - ro) * invRd;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);
    if (tFar < max(tNear, 0.0) || tNear >= maxT)
        return -1.0;
    return max(tNear, 0.0);
}

bool IntersectAABB(vec3 ro, vec3 invRd, vec3 boundsMin, vec3 boundsMax, float maxT)
{
    return IntersectAABBDist(ro, invRd, boundsMin, boundsMax, maxT) >= 0.0;
}

// Moller-Trumbore. Shrinks `closestT` (the caller's current best hit distance) in place so the BVH
// walk below can use it to reject farther AABBs/triangles as it goes.
bool IntersectTriangle(vec3 ro, vec3 rd, uint triIdx, inout float closestT, out vec2 outBary)
{
    TrianglePos tri = triPos[triIdx];
    vec3 e1 = tri.v1.xyz - tri.v0.xyz;
    vec3 e2 = tri.v2.xyz - tri.v0.xyz;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-8)
        return false;

    float invDet = 1.0 / det;
    vec3 tvec = ro - tri.v0.xyz;
    float u = dot(tvec, p) * invDet;
    if (u < 0.0 || u > 1.0)
        return false;

    vec3 q = cross(tvec, e1);
    float v = dot(rd, q) * invDet;
    if (v < 0.0 || u + v > 1.0)
        return false;

    float t = dot(e2, q) * invDet;
    if (t < 1e-4 || t > closestT)
        return false;

    closestT = t;
    outBary = vec2(u, v);
    return true;
}

// Same Moller-Trumbore test as IntersectTriangle, but for shadow/occlusion rays that only need to know
// IF something blocks the ray, not the closest hit or its barycentrics.
bool IntersectTriangleAny(vec3 ro, vec3 rd, uint triIdx, float maxT)
{
    TrianglePos tri = triPos[triIdx];
    vec3 e1 = tri.v1.xyz - tri.v0.xyz;
    vec3 e2 = tri.v2.xyz - tri.v0.xyz;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-8)
        return false;

    float invDet = 1.0 / det;
    vec3 tvec = ro - tri.v0.xyz;
    float u = dot(tvec, p) * invDet;
    if (u < 0.0 || u > 1.0)
        return false;

    vec3 q = cross(tvec, e1);
    float v = dot(rd, q) * invDet;
    if (v < 0.0 || u + v > 1.0)
        return false;

    float t = dot(e2, q) * invDet;
    return t > 1e-4 && t < maxT;
}

// 64 stack slots comfortably covers even million-triangle scenes for a well-balanced tree (depth
// ~log2(N)), but a pathologically unbalanced BVH (or corrupted node data) could in principle push past
// that. Writing past the end of a local array in GLSL is undefined behavior - not a clean crash - and
// has been observed to manifest as an effectively endless loop (garbage `sp`/node data keeps the loop
// going) that hangs the driver instead of failing safely. Both the stack-depth checks on push and the
// hard `iterations` caps below exist purely as a safety net against that : a correct traversal never
// gets close to either limit.
const int BVH_STACK_SIZE = 64;

// Any-hit BVH traversal for shadow rays : returns as soon as a single occluder is found instead of
// searching for the closest one - roughly halves the ray-tracing cost of NEE compared to reusing the
// closest-hit TraceRay for occlusion tests too.
//
// Descends into a current node and only pushes a child whose AABB the ray actually hits (rather than
// pushing both unconditionally and re-testing on pop, as this used to) : same number of slab tests, but
// the stack stays shallow and every loop iteration does real work instead of popping nodes that are
// immediately discarded.
bool TraceRayAny(vec3 ro, vec3 rd, float maxT)
{
    vec3 invRd = 1.0 / rd;

    if (!IntersectAABB(ro, invRd, bvhNodes[0].boundsMin, bvhNodes[0].boundsMax, maxT))
        return false;

    uint stack[BVH_STACK_SIZE];
    int sp = 0;
    uint nodeIdx = 0u;

    int iterations = 0;
    while (iterations++ < 4096)
    {
        BVHNode node = bvhNodes[nodeIdx];

        if (node.triCount > 0u)
        {
            for (uint i = 0u; i < node.triCount; i++)
            {
                if (IntersectTriangleAny(ro, rd, node.leftFirst + i, maxT))
                    return true;
            }
        }
        else
        {
            uint c0 = node.leftFirst;
            uint c1 = node.leftFirst + 1u;

            bool hit0 = IntersectAABB(ro, invRd, bvhNodes[c0].boundsMin, bvhNodes[c0].boundsMax, maxT);
            bool hit1 = IntersectAABB(ro, invRd, bvhNodes[c1].boundsMin, bvhNodes[c1].boundsMax, maxT);

            if (hit0)
            {
                if (hit1 && sp < BVH_STACK_SIZE)
                    stack[sp++] = c1;
                nodeIdx = c0;
                continue;
            }
            if (hit1)
            {
                nodeIdx = c1;
                continue;
            }
        }

        if (sp == 0)
            break;
        nodeIdx = stack[--sp];
    }

    return false;
}

// Iterative, ORDERED closest-hit BVH traversal (GLSL has no recursion) - see BVH_STACK_SIZE above for
// the stack-depth/iteration safety nets.
//
// "Ordered" is what makes this much cheaper than the plain depth-first walk it replaces : at each
// internal node both children are slab-tested, the ray descends into whichever one it enters FIRST, and
// the other is pushed along with its own entry distance. Because hit.t only ever shrinks, a pushed
// subtree whose entry distance has since fallen behind the best hit can be discarded outright when it
// is popped, without loading a single one of its nodes. Visiting children in arbitrary order (what the
// previous version did, pushing both unconditionally) instead lets a far subtree be fully traversed
// before the near one has had a chance to tighten hit.t, so most of that work is wasted - the effect is
// largest for exactly the kind of long, incoherent rays the DDGI probe tracer fires.
bool TraceRay(vec3 ro, vec3 rd, float maxT, out HitInfo hit)
{
    hit.t = maxT;
    hit.triIndex = 0xFFFFFFFFu;

    vec3 invRd = 1.0 / rd;

    if (IntersectAABBDist(ro, invRd, bvhNodes[0].boundsMin, bvhNodes[0].boundsMax, hit.t) < 0.0)
        return false;

    uint  stackNode[BVH_STACK_SIZE];
    float stackDist[BVH_STACK_SIZE]; // entry distance of the pushed subtree, see the pop loop below
    int sp = 0;
    uint nodeIdx = 0u;

    int iterations = 0;
    while (iterations++ < 4096)
    {
        BVHNode node = bvhNodes[nodeIdx];

        if (node.triCount > 0u)
        {
            for (uint i = 0u; i < node.triCount; i++)
            {
                uint triIdx = node.leftFirst + i;
                vec2 bary;
                if (IntersectTriangle(ro, rd, triIdx, hit.t, bary))
                {
                    hit.triIndex = triIdx;
                    hit.bary = bary;
                }
            }
        }
        else
        {
            uint c0 = node.leftFirst;
            uint c1 = node.leftFirst + 1u;

            float d0 = IntersectAABBDist(ro, invRd, bvhNodes[c0].boundsMin, bvhNodes[c0].boundsMax, hit.t);
            float d1 = IntersectAABBDist(ro, invRd, bvhNodes[c1].boundsMin, bvhNodes[c1].boundsMax, hit.t);

            // Make (c0, d0) the child to descend into : the nearer of the two when both are hit, or
            // whichever one is hit at all when only one is.
            if (d1 >= 0.0 && (d0 < 0.0 || d1 < d0))
            {
                uint  swapIdx = c0; c0 = c1; c1 = swapIdx;
                float swapDist = d0; d0 = d1; d1 = swapDist;
            }

            if (d0 >= 0.0)
            {
                if (d1 >= 0.0 && sp < BVH_STACK_SIZE)
                {
                    stackNode[sp] = c1;
                    stackDist[sp] = d1;
                    sp++;
                }
                nodeIdx = c0;
                continue;
            }
        }

        // Leaf done, or both children missed : pop the next subtree still worth visiting. Anything
        // pushed back when hit.t was larger than its own entry distance is now provably behind the
        // closest hit and is dropped here for free.
        bool popped = false;
        while (sp > 0)
        {
            sp--;
            if (stackDist[sp] < hit.t)
            {
                nodeIdx = stackNode[sp];
                popped = true;
                break;
            }
        }
        if (!popped)
            break;
    }

    return hit.triIndex != 0xFFFFFFFFu;
}
