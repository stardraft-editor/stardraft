// Instanced sprites (units, doodad overlays). One instance = one textured quad; draw order comes
// from the depth value (derived from y on the CPU), so instances need no sorting.

struct Instance {
    float2 pos;    // map pixels, quad top-left
    float2 size;   // map pixels
    float4 uv;     // u0, v0, u1, v1 (u0 > u1 mirrors)
    float4 color;  // player colour (rgb) and opacity (a)
    float depth;
    float flags;  // 1 = shadow (draw as translucent black), 2 = red tint (invalid placement),
                  // 4 = colourise with `color` (selection circles: white art drawn green, as in the game), 8 = glow;
                  // the images.dat draw functions: 16 = cloak (2-7), 32 = EMP shockwave (8), 64 = warp texture (12),
                  // 128 = hallucination (16), 256 = warp flash (17)
    float level;  // cloak: how far along it is (0..1, 1 cloaked); warp flash: how white (1..0)
    float pad;
    float4 warpUv;   // warp texture: its uv at the quad's top-left and bottom-right corners
    float4 warpLim;  // and the uv rectangle of its frame (black outside it)
};

// SDL binds storage buffers as raw views (ByteAddressBuffer). A StructuredBuffer over a raw view is not
// defined in D3D12: some drivers read it as one float per field (GTX 1080: no sprites at all).
ByteAddressBuffer instances : register(t0, space0);

Instance loadInstance(uint i) {
    const uint at = i * 96;  // sizeof(GpuInstance)
    Instance n;
    const float4 a = asfloat(instances.Load4(at)), d = asfloat(instances.Load4(at + 48));
    n.pos = a.xy, n.size = a.zw;
    n.uv = asfloat(instances.Load4(at + 16));
    n.color = asfloat(instances.Load4(at + 32));
    n.depth = d.x, n.flags = d.y, n.level = d.z, n.pad = d.w;
    n.warpUv = asfloat(instances.Load4(at + 64));
    n.warpLim = asfloat(instances.Load4(at + 80));
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
    float2 local : TEXCOORD3;                 // map pixels from the quad's top-left
    nointerpolation float4 info : TEXCOORD4;  // the quad's width (map pixels), u per map pixel, level
    float2 warpUv : TEXCOORD5;
    nointerpolation float4 warpLim : TEXCOORD6;
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
    o.local = c * inst.size;
    o.info = float4(inst.size.x, (inst.uv.z - inst.uv.x) / max(inst.size.x, 1e-6), inst.level, 0);
    o.warpUv = lerp(inst.warpUv.xy, inst.warpUv.zw, c);
    o.warpLim = inst.warpLim;
    return o;
}

// The fragment shader's own values: the target's size and the zoom (to find what is under a sprite in `backdrop`, the
// picture before the see-through sprites), and SD art (1: the cloak as the classic game draws it).
cbuffer SpriteFragUniforms : register(b0, space3) {
    float2 targetSize;
    float fragZoom;
    float sdArt;
};

Texture2D diffuse : register(t0, space2);
SamplerState diffuseSampler : register(s0, space2);
Texture2D teamMask : register(t1, space2);  // (warp texture: the Warp Texture image)
SamplerState maskSampler : register(s1, space2);
Texture2D backdrop : register(t2, space2);
SamplerState backdropSampler : register(s2, space2);

// The brightness that StarCraft: Remastered's shaders use (cloak, hallucination).
float luminance(float3 c) { return dot(c, float3(0.30196078, 0.59215686, 0.10980392)); }

// What is drawn under the sprite at this fragment, `dx` map pixels to the right.
float3 under(float4 pos, float dx) {
    float2 at = float2(pos.x + dx * fragZoom, pos.y) / targetSize;
    return backdrop.SampleLevel(backdropSampler, at, 0).rgb;
}

float4 PSMain(VSOut i) : SV_Target {
    float4 c = diffuse.Sample(diffuseSampler, i.uv);
    uint flags = (uint)i.flags;
    if (flags & 8) {
        // Glow: added to what is below (additive pipeline), as the game draws remapped glows.
        if (max(c.r, max(c.g, c.b)) * c.a < 0.01) discard;
        return float4(c.rgb * c.a * i.color.a, 0);
    }
    if (flags & 32) {
        // EMP shockwave: the pixel shows what is (r, g, b average, as much as the sprite covers) * 8 to the right.
        if (c.a < 0.004) discard;
        return float4(under(i.pos, (c.r + c.g + c.b) / 3 * c.a * 8), i.color.a);
    }
    if (c.a < 0.4) discard;  // alpha test so the depth buffer orders overlapping sprites
    if (flags & 1) return float4(0, 0, 0, 0.5 * i.color.a);  // shadow
    if (flags & 4) return float4(i.color.rgb * saturate(dot(c.rgb, float3(0.299, 0.587, 0.114)) * 1.8), c.a);
    float mask = teamMask.Sample(maskSampler, i.uv).r;
    // Team colour: the masked area is greyscale art; multiply it by the player colour (as the game
    // does), which keeps its shading and the colour's own strength.
    float3 team = c.rgb * i.color.rgb;
    float3 rgb = lerp(c.rgb, team, mask);
    if (flags & 16) {
        float t = i.info.z;
        if (sdArt > 0.5) {
            // SD: half the sprite, half what is under it (trans50); while it cloaks or decloaks, the pixels brighter
            // than the level show as they are.
            float3 p = c.rgb * 255;
            float bright = floor((p.r * 28 + p.g * 77 + p.b * 151 + 4096) / 8192);
            if (bright > t * 8) return float4(rgb, i.color.a);
            return float4(lerp(rgb, under(i.pos, 0), 0.5), i.color.a);
        }
        // HD: what is under it, (brightness + 1/16) * 8 to the right where the sprite is there too; else right under it.
        float b = luminance(rgb);
        float dx = (b + 0.0625) * 8;
        float4 there = diffuse.SampleLevel(diffuseSampler, i.uv + float2(dx * i.info.y, 0), 0);
        bool alsoCloaked = b > 0 && i.local.x + dx < i.info.x && there.a >= 0.4 && luminance(there.rgb) > 0;
        return float4(lerp(rgb, under(i.pos, alsoCloaked ? dx : 0), t), i.color.a);
    }
    if (flags & 64) {
        // Warp texture: the sprite's shape, filled with the Warp Texture image's frame at the same place.
        float2 w = i.warpUv;
        bool inside = w.x >= i.warpLim.x && w.y >= i.warpLim.y && w.x <= i.warpLim.z && w.y <= i.warpLim.w;
        rgb = inside ? teamMask.SampleLevel(maskSampler, w, 0).rgb : float3(0, 0, 0);
    }
    if (flags & 128) rgb = min(float3(1, 1, 1), float3(0.75, 1.125, 2.65) * luminance(rgb));  // hallucination
    if (flags & 256) rgb = lerp(rgb, float3(1, 1, 1), i.info.z);                               // warp flash
    if (flags & 2) rgb = lerp(rgb, float3(1, 0.15, 0.1), 0.6);  // invalid placement
    return float4(rgb, c.a * i.color.a);
}
