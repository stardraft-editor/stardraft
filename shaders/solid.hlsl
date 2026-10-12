// Screen-space solid rectangles (selection boxes, locations, previews, text glyph quads) and lines
// (brush outlines): a line is a quad of the given thickness along the segment rect.xy -> rect.zw.

struct Rect {
    float4 rect;   // x0, y0, x1, y1 in screen pixels (line: its two ends)
    float4 color;  // straight alpha
    float4 seg;    // x: line thickness in pixels (0: a rectangle)
};

// A raw view, as SDL binds storage buffers (see sprite.hlsl).
ByteAddressBuffer rects : register(t0, space0);

Rect loadRect(uint i) {
    const uint at = i * 48;  // sizeof(GpuRect)
    Rect r;
    r.rect = asfloat(rects.Load4(at));
    r.color = asfloat(rects.Load4(at + 16));
    r.seg = asfloat(rects.Load4(at + 32));
    return r;
}

cbuffer SolidUniforms : register(b0, space1) {
    float2 viewport;
    float firstRect;
    float pad;
};

struct VSOut {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    Rect r = loadRect(iid + (uint)firstRect);
    static const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1)};
    float2 c = corners[vid % 6];
    float2 p = lerp(r.rect.xy, r.rect.zw, c);
    if (r.seg.x > 0) {
        float2 d = r.rect.zw - r.rect.xy;
        float len = max(length(d), 1e-3);
        float2 n = float2(-d.y, d.x) / len * (r.seg.x * 0.5);
        p = lerp(r.rect.xy, r.rect.zw, c.x) + n * (c.y * 2 - 1);
    }
    VSOut o;
    o.pos = float4(p / viewport * float2(2, -2) + float2(-1, 1), 0, 1);
    o.color = r.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target { return i.color; }
