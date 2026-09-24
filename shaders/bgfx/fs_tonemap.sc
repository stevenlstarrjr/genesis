$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_fog, 1);
SAMPLER2D(s_sceneDepthTonemap, 2);
SAMPLER2D(s_fogDepth, 3);
SAMPLER2D(s_exposure, 4);
SAMPLER2D(s_ao, 5);
SAMPLER2D(s_aoDepthUpsample, 6);
SAMPLER2D(s_bloom, 7);
SAMPLER2D(s_materialAo, 8);
uniform vec4 u_colorParams; // view transform, contrast, saturation, bloom intensity
uniform vec4 u_upsampleParams; // inverse fog width/height, near, far
uniform vec4 u_debugParams; // 0 final, 1 SSAO, 2 contact, 3 invalid buffers, 4 color chart, 5 material AO

float linearDepth(float depth)
{
    float nearPlane = u_upsampleParams.z;
    float farPlane = u_upsampleParams.w;
    return nearPlane * farPlane / max(farPlane - depth * (farPlane - nearPlane), 1e-5);
}

vec4 bilateralWeights(vec4 bilinear, vec4 depths, float centerZ)
{
    float nearPlane = u_upsampleParams.z;
    float farPlane = u_upsampleParams.w;
    vec4 sampleZ = nearPlane * farPlane
        / max(farPlane - depths * (farPlane - nearPlane), vec4_splat(1e-5));
    float tolerance = max(centerZ * 0.015, 0.08);
    // Normalize in log space. At silhouettes all four exponential depth
    // weights can underflow; dividing zero by epsilon made fog alpha zero
    // (opaque black) and AO black. The largest weight is now always one.
    vec4 logWeights = log(max(bilinear, vec4_splat(1e-10)))
        - abs(sampleZ - centerZ) / tolerance;
    logWeights -= max(max(logWeights.x, logWeights.y), max(logWeights.z, logWeights.w));
    vec4 weights = exp(logWeights);
    return weights / dot(weights, vec4_splat(1.0));
}

vec4 bilateralFog(vec2 uv)
{
    vec2 texel = u_upsampleParams.xy;
    vec2 pixel = uv / texel - 0.5;
    vec2 base = floor(pixel);
    vec2 f = pixel - base;
    vec2 uv00 = (base + vec2(0.5, 0.5)) * texel;
    vec2 uv10 = uv00 + vec2(texel.x, 0.0);
    vec2 uv01 = uv00 + vec2(0.0, texel.y);
    vec2 uv11 = uv00 + texel;
    float centerZ = linearDepth(texture2D(s_sceneDepthTonemap, uv).x);
    vec2 bilinearX = vec2(1.0 - f.x, f.x);
    vec2 bilinearY = vec2(1.0 - f.y, f.y);
    vec4 weights = bilateralWeights(vec4(bilinearX.x * bilinearY.x, bilinearX.y * bilinearY.x,
        bilinearX.x * bilinearY.y, bilinearX.y * bilinearY.y),
        vec4(texture2D(s_fogDepth, uv00).x, texture2D(s_fogDepth, uv10).x,
            texture2D(s_fogDepth, uv01).x, texture2D(s_fogDepth, uv11).x), centerZ);
    return texture2D(s_fog, uv00) * weights.x + texture2D(s_fog, uv10) * weights.y
        + texture2D(s_fog, uv01) * weights.z + texture2D(s_fog, uv11) * weights.w;
}

vec2 bilateralOcclusion(vec2 uv)
{
    vec2 texel = u_upsampleParams.xy;
    vec2 pixel = uv / texel - 0.5;
    vec2 base = floor(pixel);
    vec2 f = pixel - base;
    vec2 uv00 = (base + vec2(0.5, 0.5)) * texel;
    vec2 uv10 = uv00 + vec2(texel.x, 0.0);
    vec2 uv01 = uv00 + vec2(0.0, texel.y);
    vec2 uv11 = uv00 + texel;
    float centerZ = linearDepth(texture2D(s_sceneDepthTonemap, uv).x);
    vec2 bilinearX = vec2(1.0 - f.x, f.x);
    vec2 bilinearY = vec2(1.0 - f.y, f.y);
    vec4 weights = bilateralWeights(vec4(bilinearX.x * bilinearY.x, bilinearX.y * bilinearY.x,
        bilinearX.x * bilinearY.y, bilinearX.y * bilinearY.y),
        vec4(texture2D(s_aoDepthUpsample, uv00).x, texture2D(s_aoDepthUpsample, uv10).x,
            texture2D(s_aoDepthUpsample, uv01).x, texture2D(s_aoDepthUpsample, uv11).x), centerZ);
    return texture2D(s_ao, uv00).xy * weights.x + texture2D(s_ao, uv10).xy * weights.y
        + texture2D(s_ao, uv01).xy * weights.z + texture2D(s_ao, uv11).xy * weights.w;
}

float filmicLuminance(float value)
{
    return saturate((value * (2.51 * value + 0.03)) / (value * (2.43 * value + 0.59) + 0.14));
}

// Analytic AgX by linlin (MIT): https://github.com/bWFuanVzYWth/AgX
// See plugins/color/THIRD_PARTY_NOTICES.md. The result is display encoded.
vec3 agxCurve(vec3 value)
{
    const float threshold = 0.6060606060606061;
    vec3 lowMask = step(value, vec3_splat(threshold));
    vec3 a = mix(vec3_splat(69.86278913545539), vec3_splat(59.507875), lowMask);
    vec3 b = mix(vec3_splat(3.25), vec3_splat(3.0), lowMask);
    vec3 c = mix(vec3_splat(-4.0 / 13.0), vec3_splat(-1.0 / 3.0), lowMask);
    return 0.5 + ((-2.0 * threshold) + 2.0 * value)
        * pow(1.0 + a * pow(abs(value - threshold), b), c);
}

vec3 agxTransform(vec3 color)
{
    const float minEv = -12.473931188332413;
    const float maxEv = 4.026068811667588;
    const float inverseRange = 1.0 / (maxEv - minEv);
    color = max(color, vec3_splat(1e-10));
    vec3 inset;
    inset.r = dot(color, vec3(0.842401071, 0.078436502, 0.079162427));
    inset.g = dot(color, vec3(0.042401071, 0.878436502, 0.079162427));
    inset.b = dot(color, vec3(0.042401071, 0.078436502, 0.879162966));
    inset = saturate(log2(max(inset, vec3_splat(1e-10))) * inverseRange
        - minEv * inverseRange);
    inset = agxCurve(inset);
    vec3 outset;
    outset.r = dot(inset, vec3(1.196998661, -0.098045627, -0.098953034));
    outset.g = dot(inset, vec3(-0.053001339, 1.151954373, -0.098953034));
    outset.b = dot(inset, vec3(-0.053001339, -0.098045627, 1.151046966));
    return saturate(outset);
}

bool invalidFloat(float value)
{
    // Bit classification survives D3D's fast-math assumptions about texture reads.
    return (floatBitsToUint(value) & 0x7fffffffu) >= 0x7f800000u;
}

bool invalidColor(vec4 value)
{
    return invalidFloat(value.x) || invalidFloat(value.y)
        || invalidFloat(value.z) || invalidFloat(value.w);
}

void main()
{
    if (u_debugParams.x > 4.5 && u_debugParams.x < 5.5)
    {
        float visibility = texture2D(s_sceneDepthTonemap, v_texcoord0).r < 0.999999
            ? texture2D(s_materialAo, v_texcoord0).a : 1.0;
        gl_FragColor = vec4(vec3_splat(visibility), 1.0);
        return;
    }
    if (u_debugParams.x > 2.5 && u_debugParams.x < 3.5)
    {
        vec4 scene = texture2D(s_hdr, v_texcoord0);
        vec4 bloom = texture2D(s_bloom, v_texcoord0);
        vec4 volume = bilateralFog(v_texcoord0);
        // RGB identify the first damaged buffers, before the display transform:
        // red = scene HDR, green = bloom, blue = fog (including lost transmittance).
        gl_FragColor = vec4(invalidColor(scene) ? 1.0 : 0.0,
            invalidColor(bloom) ? 1.0 : 0.0,
            invalidColor(volume) || volume.a < 0.01 ? 1.0 : 0.0, 1.0);
        return;
    }
    vec2 occlusion = u_debugParams.y > 0.5 ? vec2_splat(1.0) : bilateralOcclusion(v_texcoord0);
    if (u_debugParams.x > 0.5 && u_debugParams.x < 2.5)
    {
        float visibility = u_debugParams.x < 1.5 ? occlusion.x : occlusion.y;
        gl_FragColor = vec4(vec3_splat(visibility), 1.0);
        return;
    }
    vec4 fog = u_debugParams.y > 0.5 ? vec4(0.0, 0.0, 0.0, 1.0) : bilateralFog(v_texcoord0);
    float lightingOcclusion = mix(1.0, occlusion.x, 0.58) * mix(1.0, occlusion.y, 0.30);
    float exposureEv = texture2D(s_exposure, vec2(0.5, 0.5)).x;
    vec3 hdr = (texture2D(s_hdr, v_texcoord0).rgb * lightingOcclusion * fog.a + fog.rgb
        + texture2D(s_bloom, v_texcoord0).rgb * u_colorParams.w) * exp2(exposureEv);
    if (u_debugParams.x > 5.5)
        hdr = texture2D(s_bloom,v_texcoord0).rgb * u_colorParams.w * exp2(exposureEv);
    if (u_debugParams.x > 3.5 && u_debugParams.x < 4.5)
        // Eight neutral swatches from -4 to +3 stops around linear 18% gray,
        // independent of scene lighting, bloom and exposure. Same output path.
        hdr = vec3_splat(0.18 * exp2(floor(v_texcoord0.x * 8.0) - 4.0));
    vec3 mapped;
    float outputLuminance;
    if (u_colorParams.x > 3.5)
    {
        // AgX preserves highlight hue and rolls bright PBR reflections into the
        // display range without turning the whole material chalk white.
        mapped = agxTransform(hdr);
        outputLuminance = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    }
    else
    {
        // ACES compatibility path.
        float sourceLuminance = max(dot(hdr, vec3(0.2126, 0.7152, 0.0722)), 1e-5);
        outputLuminance = filmicLuminance(sourceLuminance);
        mapped = hdr * (outputLuminance / sourceLuminance);
        float highlightDesaturation = saturate(outputLuminance * outputLuminance * 0.35);
        mapped = mix(mapped, vec3_splat(outputLuminance), highlightDesaturation);
    }
    mapped = mix(vec3_splat(outputLuminance), mapped, u_colorParams.z);
    mapped = pow(max(mapped, vec3_splat(0.0)), vec3_splat(1.0 / u_colorParams.y));
    // This analytic AgX curve already returns display-encoded BT.709. Applying
    // another 1/2.2 power lifts blacks/midtones and makes PBR look washed out.
    // The legacy ACES fit, in contrast, still needs its output encoding.
    if (u_colorParams.x < 3.5)
        mapped = pow(mapped, vec3_splat(1.0 / 2.2));
    gl_FragColor = vec4(mapped, 1.0);
}
