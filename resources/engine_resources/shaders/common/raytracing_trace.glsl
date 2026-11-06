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

bool CulledFace(uint triIdx, float det)
{
    return det < 0.0 && (materials[uint(triAttrib[triIdx].uvMatID.w)].flags & MAT_DOUBLE_SIDED_BIT) == 0u;
}

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
    if (t < 1e-4 || t > closestT || CulledFace(triIdx, det))
        return false;

    closestT = t;
    outBary = vec2(u, v);
    return true;
}

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

    // Both faces block a shadow ray: the sun's shadow maps draw both sides (Double-sided Casters).
    float t = dot(e2, q) * invDet;
    return t > 1e-4 && t < maxT;
}

// Fixed stack. Writing past a local array is UB and was seen hanging the driver, so pushes and iterations are capped as a safety net.
const int BVH_STACK_SIZE = 64;

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

bool TraceRay(vec3 ro, vec3 rd, float maxT, out HitInfo hit)
{
    hit.t = maxT;
    hit.triIndex = 0xFFFFFFFFu;

    vec3 invRd = 1.0 / rd;

    if (IntersectAABBDist(ro, invRd, bvhNodes[0].boundsMin, bvhNodes[0].boundsMax, hit.t) < 0.0)
        return false;

    uint  stackNode[BVH_STACK_SIZE];
    float stackDist[BVH_STACK_SIZE];
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
