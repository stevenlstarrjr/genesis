#ifndef GENESIS_CLOUDS_HLSLI
#define GENESIS_CLOUDS_HLSLI

// Shared C++/HLSL layout, with every field in a 16-byte block.
struct GenesisCloudLayer {
    float4 Shape; // coverage, density, altitude km, thickness km
    float4 Detail; // kind, horizontal scale km, erosion, enabled
};
struct GenesisCloudConstants {
    GenesisCloudLayer Layers[2];
    float4 Wind; // horizontal displacement km (xy), reserved
    uint Mode; // 0: untouched legacy sky, 1: Genesis clouds
    uint Enabled;
    uint Steps;
    uint Seed;
};

#if !defined(__cplusplus) || defined(__INTELLISENSE__)

float GenesisCloudWeatherNoise(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float4 v = float4(dot(cell, float2(127.1, 311.7)),
        dot(cell + float2(1, 0), float2(127.1, 311.7)),
        dot(cell + float2(0, 1), float2(127.1, 311.7)),
        dot(cell + 1, float2(127.1, 311.7)));
    float4 n = frac(sin(v) * 43758.5453);
    return lerp(lerp(n.x, n.y, f.x), lerp(n.z, n.w, f.x), f.y);
}

float GenesisCloudDensity(float3 pos, float groundRadius, GenesisCloudLayer layer,
    GenesisCloudConstants settings, Texture3D noise, SamplerState samp)
{
    float height = (length(pos) - groundRadius - layer.Shape.z) / layer.Shape.w;
    if (height <= 0 || height >= 1 || layer.Shape.x <= 0 || layer.Detail.w == 0) return 0;
    float2 seedOffset = float2(settings.Seed % 997, (settings.Seed * 37) % 991) * 0.137;
    float2 xy = (pos.xy - settings.Wind.xy) / max(layer.Detail.y, 0.1) + seedOffset;
    uint kind = (uint)layer.Detail.x;
    if (kind == 2) xy *= float2(0.22, 2.8); // elongated cirrus filaments
    float broad = noise.SampleLevel(samp, float3(xy * 0.35, 0.5), 0).r;
    float body = noise.SampleLevel(samp, float3(xy, height), 0).r;
    float fine = noise.SampleLevel(samp, float3(xy * 3.7, height * 0.8 + 0.1), 0).g;
    // Coverage comes from a separate weather field: the legacy density volume
    // is biased toward solid cloud and cannot by itself control open-sky area.
    float weather = GenesisCloudWeatherNoise(xy * 0.8) * 0.7
                  + GenesisCloudWeatherNoise(xy * 2.1 + 17.3) * 0.3;
    float threshold = 1.0 - layer.Shape.x;
    float shape = saturate((weather - threshold) / 0.18);
    float field = lerp(broad, body, 0.65);
    float billows = GenesisCloudWeatherNoise(xy * 6.0 + height * 1.7)
                  * GenesisCloudWeatherNoise(xy * 13.0 - height * 2.3);
    shape = saturate(shape - (1.0 - field) * layer.Detail.z * 0.45
                           - (1.0 - fine) * layer.Detail.z * 0.15
                           - (1.0 - billows) * layer.Detail.z * 0.35);
    // Stratus spreads into a continuous deck; cumulus keeps a flat base and
    // rounded tops; cirrus is optically thin. Storm is a deeper convective form.
    float top = kind == 1 ? 1.0 : lerp(0.45, 1.0, shape);
    float profile = smoothstep(0.0, 0.12, height) * (1.0 - smoothstep(top * 0.5, top, height));
    if (kind == 1) shape = lerp(shape, layer.Shape.x, 0.65 * layer.Shape.x);
    if (kind == 2) {
        profile = pow(saturate(4 * height * (1 - height)), 2.0);
        // Fine high-altitude ice filaments, not an opaque striped ceiling.
        shape *= (0.15 + 0.85 * GenesisCloudWeatherNoise(xy * float2(3, 11))) * 0.18;
    }
    return shape * profile * layer.Shape.y;
}

// Spherical cloud-shell intersection, observer below the cloud bases.
float GenesisCloudExit(float3 origin, float3 direction, float radius) {
    float b = dot(origin, direction);
    return max(0.0, -b + sqrt(max(0.0, b*b - dot(origin, origin) + radius*radius)));
}

float4 GenesisCloudRaymarch(float3 camera, float3 viewDir, float3 sunDir,
    float groundRadius, float3 skyRadiance, float3 sunIrradiance,
    GenesisCloudConstants settings, Texture3D noise, SamplerState samp)
{
    if (settings.Enabled == 0 || viewDir.z < -0.02) return float4(0, 0, 0, 1);
    float3 scattering = 0;
    float transmittance = 1;
    // The validated API keeps layers ordered and non-overlapping.
    [unroll] for (uint index = 0; index < 2; ++index) {
        GenesisCloudLayer layer = settings.Layers[index];
        if (layer.Detail.w == 0 || layer.Shape.x == 0 || layer.Shape.y == 0) continue;
        float start = GenesisCloudExit(camera, viewDir, groundRadius + layer.Shape.z);
        float end = GenesisCloudExit(camera, viewDir, groundRadius + layer.Shape.z + layer.Shape.w);
        float lengthKm = min(end - start, 80.0);
        float stepKm = lengthKm / settings.Steps;
        float mu = dot(viewDir, sunDir);
        float phase = 0.5 * (0.75 / (12.566371 * pow(max(0.05, 1.25 - mu), 1.5))) + 0.5 / 12.566371;
        float3 ambient = skyRadiance * 0.6 + sunIrradiance * (0.018 + 0.025 * saturate(sunDir.z));
        [loop] for (uint i = 0; i < settings.Steps; ++i) {
            float3 pos = camera + viewDir * (start + (i + 0.5) * stepKm);
            float density = GenesisCloudDensity(pos, groundRadius, layer, settings, noise, samp);
            if (density <= 0.0001) continue;
            float extinction = density * 2.5;
            float stepTransmittance = exp(-extinction * stepKm);
            // Two deterministic light samples; no frame-varying random noise.
            float shadowStep = layer.Shape.w * 0.35;
            float shadowDensity = GenesisCloudDensity(pos + sunDir * shadowStep, groundRadius, layer, settings, noise, samp)
                                + GenesisCloudDensity(pos + sunDir * shadowStep * 2, groundRadius, layer, settings, noise, samp);
            float sunlight = exp(-shadowDensity * shadowStep * 2.5);
            float height = saturate((length(pos) - groundRadius - layer.Shape.z) / layer.Shape.w);
            float ambientOcclusion = lerp(layer.Detail.x == 3 ? 0.15 : 0.4, 1.0, height);
            float3 lighting = ambient * ambientOcclusion + sunIrradiance * phase * sunlight;
            scattering += transmittance * (1 - stepTransmittance) * lighting;
            transmittance *= stepTransmittance;
            // Do not leave a percent-level residual that leaks the very bright
            // sun disk through an otherwise optically opaque storm deck.
            if (transmittance < 0.001) { transmittance = 0; break; }
        }
        if (transmittance == 0) break;
    }
    return float4(max(scattering, 0), saturate(transmittance));
}
#endif
#endif
