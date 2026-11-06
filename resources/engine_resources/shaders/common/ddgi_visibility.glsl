float DDGI_VisibilityWeight(vec2 meanMean2, float distToPoint)
{
    float mean = meanMean2.x;
    if (distToPoint <= mean)
        return 1.0;

    float variance = abs(meanMean2.y - mean * mean);
    float d = distToPoint - mean;
    float chebyshev = variance / (variance + d * d);
    return max(chebyshev * chebyshev * chebyshev, 0.0);
}
