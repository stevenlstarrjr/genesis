#include <bgfx_compute.sh>

IMAGE2D_WO(s_exposureOut, rgba32f, 0);
SAMPLER2D(s_previousExposure, 1);
BUFFER_RO(s_histogram, uint, 2);
uniform vec4 u_exposureParams0; // minimum/maximum log2 luminance, low/high percentile
uniform vec4 u_exposureParams1; // minimum/maximum EV, dt, manual exposure compensation
uniform vec4 u_exposureParams2; // enabled, middle gray, brighten speed, first-frame flag

NUM_THREADS(1, 1, 1)
void main()
{
    uint total = 0u;
    for (uint countBin = 0u; countBin < 256u; ++countBin)
        total += s_histogram[countBin];

    float low = float(total) * u_exposureParams0.z;
    float high = float(total) * u_exposureParams0.w;
    float cumulative = 0.0;
    float weightedLog = 0.0;
    float included = 0.0;
    for (uint weightedBin = 0u; weightedBin < 256u; ++weightedBin)
    {
        float count = float(s_histogram[weightedBin]);
        float next = cumulative + count;
        float overlap = max(min(next, high) - max(cumulative, low), 0.0);
        float t = (float(weightedBin) + 0.5) / 256.0;
        weightedLog += overlap * mix(u_exposureParams0.x, u_exposureParams0.y, t);
        included += overlap;
        cumulative = next;
    }

    float averageLog = weightedLog / max(included, 1.0);
    float targetEv = u_exposureParams1.w;
    if (u_exposureParams2.x > 0.5 && total > 0u)
        targetEv += log2(max(u_exposureParams2.y, 1e-4)) - averageLog;
    targetEv = clamp(targetEv, u_exposureParams1.x, u_exposureParams1.y);

    float previousEv = texture2DLod(s_previousExposure, vec2(0.5, 0.5), 0.0).x;
    // Dark-to-bright adaptation is deliberately quicker than bright-to-dark.
    float speed = targetEv < previousEv ? 3.5 : u_exposureParams2.z;
    float blend = 1.0 - exp(-max(u_exposureParams1.z, 0.0) * speed);
    if (u_exposureParams2.w > 0.5)
        blend = 1.0;
    float adaptedEv = mix(previousEv, targetEv, blend);
    imageStore(s_exposureOut, ivec2(0, 0), vec4(adaptedEv, targetEv, averageLog, 1.0));
}
