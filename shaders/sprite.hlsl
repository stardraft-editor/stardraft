// Instanced sprites (units, doodad overlays). One instance = one textured quad; draw order comes
// from the depth value (derived from y on the CPU), so instances need no sorting.

struct Instance {
    float2 pos;    // map pixels, quad top-left
    float2 size;   // map pixels
    float4 uv;     // u0, v0, u1, v1 (u0 > u1 mirrors)
    float4 color;  // player colour (rgb) and opacity (a)
    float depth;
    float flags;  // 1 = shadow (draw as translucent black), 2 = red tint (invalid placement),
                  // 4 = colourise with `color` (selection circles: white art drawn green, as in the game)
    float2 pad;
};

// SDL binds storage buffers as raw views (ByteAddressBuffer). A StructuredBuffer over a raw view is not
// defined in D3D12: some drivers read it as one float per field (GTX 1080: no sprites at all).
ByteAddressBuffer instances : register(t0, space0);

Instance loadInstance(uint i) {
    const uint at = i * 64;  // sizeof(GpuInstance)
    Instance n;
    const float4 a = asfloat(instances.Load4(at)), d = asfloat(instances.Load4(at + 48));
    n.pos = a.xy, n.size = a.zw;
    n.uv = asfloat(instances.Load4(at + 16));
    n.color = asfloat(instances.Load4(at + 32));
    n.depth = d.x, n.flags = d.y, n.pad = d.zw;
    return n;
}

cbuffer SpriteUniforms : register(b0, space1) {
    float2 camera;
    float zoom;
    float firstInstance;  // offset of this batch in the instance buffer
    float2 viewport;
    float2 pad1;
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    nointerpolation float flags : TEXCOORD2;
};

VSOut VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    Instance inst = loadInstance(iid + (uint)firstInstance);
    // Two triangles: corners (0,0) (1,0) (0,1) (0,1) (1,0) (1,1)
    static const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1)};
    float2 c = corners[vid % 6];
    float2 screen = (inst.pos + c * inst.size - camera) * zoom;
    VSOut o;
    o.pos = float4(screen / viewport * float2(2, -2) + float2(-1, 1), inst.depth, 1);
    o.uv = lerp(inst.uv.xy, inst.uv.zw, c);
    o.color = inst.color;
    o.flags = inst.flags;
    return o;
}

Texture2D diffuse : register(t0, space2);
SamplerState diffuseSampler : register(s0, space2);
Texture2D teamMask : register(t1, space2);
SamplerState maskSampler : register(s1, space2);

float4 PSMain(VSOut i) : SV_Target {
    float4 c = diffuse.Sample(diffuseSampler, i.uv);
    uint flags = (uint)i.flags;
    if (flags & 8) {
        // Glow: added to what is below (additive pipeline), as the game draws remapped glows.
        if (max(c.r, max(c.g, c.b)) * c.a < 0.01) discard;
        return float4(c.rgb * c.a * i.color.a, 0);
    }
    if (c.a < 0.4) discard;  // alpha test so the depth buffer orders overlapping sprites
    if (flags & 1) return float4(0, 0, 0, 0.5 * i.color.a);  // shadow
    if (flags & 4) return float4(i.color.rgb * saturate(dot(c.rgb, float3(0.299, 0.587, 0.114)) * 1.8), c.a);
    float mask = teamMask.Sample(maskSampler, i.uv).r;
    // Team colour: the masked area is greyscale art; multiply it by the player colour (as the game
    // does), which keeps its shading and the colour's own strength.
    float3 team = c.rgb * i.color.rgb;
    float3 rgb = lerp(c.rgb, team, mask);
    if (flags & 2) rgb = lerp(rgb, float3(1, 0.15, 0.1), 0.6);  // invalid placement
    return float4(rgb, c.a * i.color.a);
}
