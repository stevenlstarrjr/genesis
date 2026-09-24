#include <bgfx_compute.sh>

SAMPLER2D(s_histogramHdr, 0);
SAMPLER2D(s_histogramFog, 1);
SAMPLER2D(s_histogramAo, 2);
BUFFER_RW(s_histogram, uint, 3);
uniform vec4 u_histogramParams; // inverse width/height, minimum log2 luminance, inverse log range

NUM_THREADS(16, 16, 1)
void main()
{
    // One sample per 4x4 block keeps the global atomic cost small enough for
    // the target RTX 2060 while still producing a stable exposure histogram.
    vec2 pixel = vec2(gl_GlobalInvocationID.xy) * 4.0 + vec2(2.0, 2.0);
    vec2 uv = pixel * u_histogramParams.xy;
    if (uv.x >= 1.0 || uv.y >= 1.0)
        return;

    vec4 fog = texture2DLod(s_histogramFog, uv, 0.0);
    vec2 occlusion = texture2DLod(s_histogramAo, uv, 0.0).xy;
    float lightingOcclusion = mix(1.0, occlusion.x, 0.58) * mix(1.0, occlusion.y, 0.30);
    vec3 hdr = max(texture2DLod(s_histogramHdr, uv, 0.0).rgb * lightingOcclusion * fog.a + fog.rgb,
        vec3_splat(0.0));
    float luminance = max(dot(hdr, vec3(0.2126, 0.7152, 0.0722)), exp2(u_histogramParams.z));
    float normalizedLog = saturate((log2(luminance) - u_histogramParams.z) * u_histogramParams.w);
    uint bin = uint(normalizedLog * 255.0 + 0.5);
    atomicAdd(s_histogram[bin], 1u);
}
