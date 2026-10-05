#include "octahedral.glsl"

// Octahedral-atlas addressing, shared by every DDGI pass that lays out one probe per stride x stride
// tile (tileSize x tileSize interior texels + a 1-texel border on each side, atlasProbesPerRow tiles per
// atlas row) : compute/probes/probe_trace.comp, probe_classify.comp, probe_relocate.comp,
// probe_border_fixup.comp, probe_irradiance_convolve.comp, and mesh/lit.frag's forward-pass sampling.
// This formula used to be hand-repeated (and easy to accidentally drift) in each of those files.

// Integer top-left corner of probe `probeIndex`'s tile, INCLUDING its border (i.e. NOT the interior
// origin - callers that want the first interior texel add ivec2(1, 1) themselves, same as every
// existing call site already did before this was extracted).
ivec2 DDGI_TileCorner(int probeIndex, int tileSize, int atlasProbesPerRow)
{
    int stride = tileSize + 2;
    int col = probeIndex % atlasProbesPerRow;
    int row = probeIndex / atlasProbesPerRow;
    return ivec2(col * stride, row * stride);
}

// Atlas UV for probe `probeIndex`'s tile in octahedral direction `dir`, given that atlas's full
// tileSize/atlasProbesPerRow/atlasSize (atlasSize = the atlas texture's width/height in texels, both
// equal since it's laid out as a square-ish grid of tiles).
vec2 DDGI_AtlasUV(int probeIndex, vec3 dir, int tileSize, int atlasProbesPerRow, int atlasSize)
{
    vec2 oct = OctEncode(dir);
    vec2 texelInTile = (oct * 0.5 + 0.5) * float(tileSize);

    vec2 atlasTexel = vec2(DDGI_TileCorner(probeIndex, tileSize, atlasProbesPerRow)) + vec2(1.0) + texelInTile;
    return atlasTexel / float(atlasSize);
}
