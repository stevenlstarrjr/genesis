$input v_texcoord0

#include <bgfx_shader.sh>
#include "hdr_output.sh"

SAMPLER2D(s_bloomSource, 0);
uniform vec4 u_bloomParams; // source texel x/y, first level, anti-firefly scale

vec3 thresholdBloom(vec3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float knee = 0.5;
    float soft = clamp(brightness - 1.0 + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(brightness - 1.0, soft) / max(brightness, 1e-4);
    return color * contribution;
}

vec4 bloomSample(vec2 uv)
{
    vec3 color = max(texture2D(s_bloomSource,uv).rgb,vec3_splat(0.0));
    float weight = 1.0;
    if (u_bloomParams.z > 0.5)
    {
        // Normalized Karis-style weighting affects only the bloom branch.
        // Uniform bright emitters retain their intensity; isolated glints cannot
        // dominate an entire low-resolution blur footprint. Threshold before
        // reduction so the cutoff does not depend on the downsample grid phase.
        float peak = max(color.r,max(color.g,color.b));
        weight = 1.0 / (1.0 + peak / u_bloomParams.w);
        color = thresholdBloom(color);
    }
    return vec4(color * weight,weight);
}

void main()
{
    vec2 t = u_bloomParams.xy;
    // Overlapping 13-tap footprint, reduced by only 2x per level. The former
    // direct 4x reduction sampled the central 2x2 and missed intervening pixels.
    vec4 sum = bloomSample(v_texcoord0) * 0.125;
    sum += (bloomSample(v_texcoord0+t*vec2(-2.0,-2.0))
          + bloomSample(v_texcoord0+t*vec2( 2.0,-2.0))
          + bloomSample(v_texcoord0+t*vec2(-2.0, 2.0))
          + bloomSample(v_texcoord0+t*vec2( 2.0, 2.0))) * 0.03125;
    sum += (bloomSample(v_texcoord0+t*vec2( 0.0,-2.0))
          + bloomSample(v_texcoord0+t*vec2(-2.0, 0.0))
          + bloomSample(v_texcoord0+t*vec2( 2.0, 0.0))
          + bloomSample(v_texcoord0+t*vec2( 0.0, 2.0))) * 0.0625;
    sum += (bloomSample(v_texcoord0+t*vec2(-1.0,-1.0))
          + bloomSample(v_texcoord0+t*vec2( 1.0,-1.0))
          + bloomSample(v_texcoord0+t*vec2(-1.0, 1.0))
          + bloomSample(v_texcoord0+t*vec2( 1.0, 1.0))) * 0.125;
    gl_FragColor = vec4(hdrForStorage(sum.rgb / max(sum.a,1e-6)),1.0);
}
