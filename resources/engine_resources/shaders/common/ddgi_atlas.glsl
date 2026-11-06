#include "octahedral.glsl"

ivec2 DDGI_TileCorner(int probeIndex, int tileSize, int atlasProbesPerRow)
{
    int stride = tileSize + 2;
    int col = probeIndex % atlasProbesPerRow;
    int row = probeIndex / atlasProbesPerRow;
    return ivec2(col * stride, row * stride);
}

vec2 DDGI_AtlasUV(int probeIndex, vec3 dir, int tileSize, int atlasProbesPerRow, int atlasSize)
{
    vec2 oct = OctEncode(dir);
    vec2 texelInTile = (oct * 0.5 + 0.5) * float(tileSize);

    vec2 atlasTexel = vec2(DDGI_TileCorner(probeIndex, tileSize, atlasProbesPerRow)) + vec2(1.0) + texelInTile;
    return atlasTexel / float(atlasSize);
}
