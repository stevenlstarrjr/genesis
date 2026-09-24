#ifndef GENESIS_AGX_HLSLI
#define GENESIS_AGX_HLSLI

// Analytic AgX transform adapted to HLSL from the MIT-licensed implementation:
// https://github.com/bWFuanVzYWth/AgX
// Copyright (c) 2024 linlin. See plugins/color/THIRD_PARTY_NOTICES.md.

float3 GenesisAgXCurve(float3 value)
{
    const float threshold = 0.6060606060606061f;
    const float3 mask = step(value, threshold.xxx);
    const float3 a = lerp(69.86278913545539f.xxx, 59.507875f.xxx, mask);
    const float3 b = lerp(3.25f.xxx, 3.0f.xxx, mask);
    const float3 c = lerp((-4.0f / 13.0f).xxx, (-1.0f / 3.0f).xxx, mask);
    return 0.5f + ((-2.0f * threshold) + 2.0f * value) *
           pow(1.0f + a * pow(abs(value - threshold), b), c);
}

float3 GenesisAgXToLinear(float3 value)
{
    value = saturate(value);
    const float3 low = value / 12.92f;
    const float3 high = pow((value + 0.055f) / 1.055f, 2.4f);
    return lerp(low, high, step(0.04045f.xxx, value));
}

float3 GenesisAgX(float3 color)
{
    const float minEv = -12.473931188332413f;
    const float maxEv = 4.026068811667588f;
    const float inverseRange = 1.0f / (maxEv - minEv);

    const float3x3 inset = float3x3(
        0.842401071f, 0.078436502f, 0.079162427f,
        0.042401071f, 0.878436502f, 0.079162427f,
        0.042401071f, 0.078436502f, 0.879162966f);
    const float3x3 outset = float3x3(
         1.196998661f, -0.098045627f, -0.098953034f,
        -0.053001339f,  1.151954373f, -0.098953034f,
        -0.053001339f, -0.098045627f,  1.151046966f);

    color = mul(inset, max(color, 1e-10f));
    color = saturate(log2(max(color, 1e-10f)) * inverseRange - minEv * inverseRange);
    color = GenesisAgXCurve(color);
    color = mul(outset, color);
    return saturate(color);
}

float3 GenesisAgXLook(float3 color, uint look)
{
    if (look == 1) // medium-high contrast
        return saturate((color - 0.18f) * 1.12f + 0.18f);

    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    if (look == 2) // punchy
        return saturate(lerp(luminance.xxx, color, 1.12f) * 1.04f);
    if (look == 3) // golden
        return saturate(color * float3(1.055f, 1.0f, 0.92f));
    return color;
}

#endif
