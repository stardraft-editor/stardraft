// Whole-map terrain in one draw: a full-screen triangle; each pixel finds its map tile, reads the
// tile's megatile index from a 16-bit texture and samples that megatile from the atlas.
// Register spaces follow SDL3 GPU's D3D12 conventions (fragment: textures/samplers space2, uniforms space3).

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}

cbuffer TerrainUniforms : register(b0, space3) {
    float2 camera;     // map pixel at the screen's top-left
    float zoom;        // screen pixels per map pixel
    float maxLod;      // the atlas's last mip level that keeps each megatile apart (0 without mips)
    float2 viewport;   // screen size in pixels
    float2 mapTiles;   // map size in tiles
    float2 atlasSize;  // atlas size in texels
    float tileTexels;  // texels per megatile in the atlas
    float atlasCols;   // megatiles per atlas row
};

Texture2D<uint> tileIndex : register(t0, space2);
SamplerState tileSampler : register(s0, space2);
Texture2D atlas : register(t1, space2);
SamplerState atlasSampler : register(s1, space2);

float4 PSMain(VSOut i) : SV_Target {
    float2 world = camera + i.uv * viewport / zoom;
    float2 tile = floor(world / 32.0);
    if (any(tile < 0) || any(tile >= mapTiles)) return float4(0.07, 0.07, 0.08, 1);
    uint mega = tileIndex.Load(int3(tile, 0));
    if (mega == 0xFFFF) return float4(0.5, 0, 0.5, 1);
    // Mip level from the on-screen footprint (only the 32 px atlases have mips; HD clamps to level 0).
    float lod = clamp(log2(max(tileTexels / (32.0 * zoom), 1e-4)), 0, maxLod);
    // Stay half a texel of the coarser mip level that is read inside the megatile, so filtering never
    // reads a neighbour in the atlas. With half a level-0 texel only, zoomed-out views showed the tile grid.
    float margin = 0.5 * exp2(ceil(lod)) / tileTexels;
    float2 local = clamp(world / 32.0 - tile, margin, 1.0 - margin);
    uint cols = (uint)atlasCols;
    float2 cell = float2(mega % cols, mega / cols);
    float2 uv = (cell + local) * tileTexels / atlasSize;
    return float4(atlas.SampleLevel(atlasSampler, uv, lod).rgb, 1);
}
