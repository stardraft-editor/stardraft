// A mod's palette recolor over the drawn map (Renderer::setColorGrade; Tools > Cosmonarchy > Recolor preview): each
// pixel as the mod's exe plugin recolors a palette color (color_palette.cpp: set_palette_recolor). Direct3D 12 only:
// not in tools/gen_shaders.py (no SPIR-V or MSL); the other drivers recolor the SD art on the CPU (setColorMap).

struct VSOut {
    float4 pos : SV_Position;
};

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

cbuffer RecolorUniforms : register(b0, space3) {
    float4 stopRgbGamma[16];  // each stop's r, g, b, gamma
    float4 stopSat[4];        // the stops' saturations, 4 to a vector
    float count;              // 1 to 16
    float3 pad;
};

Texture2D source : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);

float easeInOut(float t) { return lerp(t * t, 1.0 - (t - 1.0) * (t - 1.0), t); }  // (lerp(a, b, t) = a + (b - a) t)
float smoothstep2(float t, float a, float b) { return lerp(a, b, easeInOut(t)); }
float stopSatAt(uint i) { return stopSat[i / 4][i % 4]; }

float mixColor(float base, float blend, float saturation, float brightness) {
    base = smoothstep2(saturation, brightness, base);
    return floor(saturate(base * blend) * 255.0) / 255.0;  // (the plugin's u8: the fraction cut off)
}

float4 PSMain(VSOut i) : SV_Target {
    float4 c = source.Load(int3(i.pos.xy, 0));
    float3 b = round(c.rgb * 255.0);  // (the palette color's bytes)
    float brightness = (b.r + b.g + b.b) / 3.0 / 255.0;
    uint n = (uint)count;
    float4 rg = stopRgbGamma[0];
    float sat = stopSatAt(0);
    if (n > 1) {
        uint last = n - 1;
        float t = saturate(easeInOut(brightness)) * last;
        uint index = min((uint)t, last);
        if (index == last) {
            rg = stopRgbGamma[last], sat = stopSatAt(last);
        } else {
            float f = t - index;
            float4 a = stopRgbGamma[index], z = stopRgbGamma[index + 1];
            rg = float4(smoothstep2(f, a.r, z.r), smoothstep2(f, a.g, z.g), smoothstep2(f, a.b, z.b), smoothstep2(f, a.a, z.a));
            sat = smoothstep2(f, stopSatAt(index), stopSatAt(index + 1));
        }
    }
    float3 base = b / 255.0;
    return float4(mixColor(base.r, rg.r * rg.a, sat, brightness), mixColor(base.g, rg.g * rg.a, sat, brightness),
                  mixColor(base.b, rg.b * rg.a, sat, brightness), c.a);
}
