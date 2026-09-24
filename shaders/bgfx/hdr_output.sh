#ifndef GENESIS_HDR_OUTPUT_SH
#define GENESIS_HDR_OUTPUT_SH

// Only limit at FP16 storage boundaries, after lighting/filtering in FP32.
// Preserve hue while keeping bright sun/emission representable. This is a
// storage limit, not an exposure/tonemapping operation. The source HDRI retains
// its full FP32 radiance for integration.
vec3 hdrForStorage(vec3 radiance)
{
    radiance = max(radiance, vec3_splat(0.0));
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    return radiance * min(1.0, 65000.0 / max(peak, 1e-6));
}

#endif
