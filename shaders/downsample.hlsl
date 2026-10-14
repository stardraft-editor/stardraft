// Box-filter a (block-compressed) atlas down by an integer factor into an RGBA target, so HD
// terrain can be mipmapped and shown at any zoom without aliasing.

struct VSOut {
    float4 pos : SV_Position;
};

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

cbuffer DownsampleUniforms : register(b0, space3) {
    float factor;  // source texels per target texel (2 or 4)
    float3 pad;
};

Texture2D source : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);

// Each source texel at its center with the point sampler (the same as Load): Metal cannot read() a compressed texture.
float4 PSMain(VSOut i) : SV_Target {
    int n = (int)factor;
    int2 base = int2(i.pos.xy) * n;
    uint w, h;
    source.GetDimensions(w, h);
    float2 texel = 1.0 / float2(w, h);
    float4 sum = 0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) sum += source.SampleLevel(sourceSampler, (float2(base + int2(x, y)) + 0.5) * texel, 0);
    return sum / (n * n);
}
