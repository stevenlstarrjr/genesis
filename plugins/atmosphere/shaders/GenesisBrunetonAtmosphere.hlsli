#ifndef GENESIS_BRUNETON_ATMOSPHERE_HLSLI
#define GENESIS_BRUNETON_ATMOSPHERE_HLSLI

#if !defined(__cplusplus) || defined(__INTELLISENSE__)

// Genesis spectral atmosphere integrator. It evaluates the same spherical
// density model used by Bruneton's precomputed method directly while baking
// the environment cube. This removes the old fixed-Earth Q2RTX LUT lookup and
// lets Python-controlled physical coefficients affect the rendered sky.

static const float GENESIS_ATMOSPHERE_PI = 3.14159265358979323846f;

float2 GenesisRaySphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return float2(1.0f, -1.0f);
    float root = sqrt(discriminant);
    return float2(-b - root, -b + root);
}

float3 GenesisAtmosphereDensity(AtmosphereParameters atmosphere, float heightKm)
{
    float rayleigh = exp(-max(heightKm, 0.0f) / atmosphere.RayleighScaleHeightKm);
    float mie = exp(-max(heightKm, 0.0f) / atmosphere.MieScaleHeightKm);
    float ozone = saturate(1.0f - abs(heightKm - atmosphere.OzoneCenterHeightKm) /
                                    atmosphere.OzoneHalfWidthKm);
    return float3(rayleigh, mie, ozone);
}

float3 GenesisExtinction(AtmosphereParameters atmosphere, float3 density)
{
    return atmosphere.RayleightScatteringRGB * density.x
         + atmosphere.MieExtinctionRGB * density.y
         + atmosphere.AbsorptionExtinctionRGB * density.z;
}

float3 GenesisOpticalDepth(AtmosphereParameters atmosphere, float3 origin,
                           float3 direction, float distanceKm, uint sampleCount)
{
    float stepLength = distanceKm / float(sampleCount);
    float3 depth = 0.0f;
    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        float distanceAlongRay = (float(i) + 0.5f) * stepLength;
        float height = length(origin + direction * distanceAlongRay) - atmosphere.PlanetSurfaceRadius;
        depth += GenesisExtinction(atmosphere, GenesisAtmosphereDensity(atmosphere, height)) * stepLength;
    }
    return depth;
}

float GenesisRayleighPhase(float cosineTheta)
{
    return 3.0f * (1.0f + cosineTheta * cosineTheta) / (16.0f * GENESIS_ATMOSPHERE_PI);
}

float GenesisMiePhase(float cosineTheta, float g)
{
    float gg = g * g;
    float denominator = max(1.0f + gg - 2.0f * g * cosineTheta, 1e-4f);
    return (3.0f * (1.0f - gg) * (1.0f + cosineTheta * cosineTheta)) /
           (8.0f * GENESIS_ATMOSPHERE_PI * (2.0f + gg) * pow(denominator, 1.5f));
}

bool GenesisSunOccluded(AtmosphereParameters atmosphere, float3 samplePosition, float3 sunDirection)
{
    float2 groundHit = GenesisRaySphere(samplePosition, sunDirection, atmosphere.PlanetSurfaceRadius);
    return groundHit.y > 0.0f && groundHit.x > 0.0f;
}

float3 GenesisBrunetonSky(AtmosphereParameters atmosphere, float3 camera,
                          float3 viewDirection, float3 sunDirection,
                          out float3 sunTransmittance)
{
    viewDirection = normalize(viewDirection);
    sunDirection = normalize(sunDirection);

    float2 atmosphereHit = GenesisRaySphere(camera, viewDirection, atmosphere.PlanetAtmosphereRadius);
    float startDistance = max(atmosphereHit.x, 0.0f);
    float endDistance = atmosphereHit.y;
    if (endDistance <= startDistance)
    {
        sunTransmittance = 1.0f;
        return 0.0f;
    }

    float2 groundHit = GenesisRaySphere(camera, viewDirection, atmosphere.PlanetSurfaceRadius);
    bool hitsGround = groundHit.x > 0.0f;
    if (hitsGround)
        endDistance = min(endDistance, groundHit.x);

    const uint ViewSamples = 12;
    const uint SunSamples = 6;
    float stepLength = (endDistance - startDistance) / float(ViewSamples);
    float3 accumulatedDepth = 0.0f;
    float3 scattering = 0.0f;
    float cosineTheta = dot(viewDirection, sunDirection);
    float rayleighPhase = GenesisRayleighPhase(cosineTheta);
    float miePhase = GenesisMiePhase(cosineTheta, atmosphere.MieHenyeyGreensteinG);

    [loop]
    for (uint i = 0; i < ViewSamples; ++i)
    {
        float distanceAlongRay = startDistance + (float(i) + 0.5f) * stepLength;
        float3 samplePosition = camera + viewDirection * distanceAlongRay;
        float height = length(samplePosition) - atmosphere.PlanetSurfaceRadius;
        float3 density = GenesisAtmosphereDensity(atmosphere, height);
        float3 localExtinction = GenesisExtinction(atmosphere, density);
        accumulatedDepth += localExtinction * stepLength;

        if (!GenesisSunOccluded(atmosphere, samplePosition, sunDirection))
        {
            float sunDistance = GenesisRaySphere(samplePosition, sunDirection, atmosphere.PlanetAtmosphereRadius).y;
            float3 sunDepth = GenesisOpticalDepth(atmosphere, samplePosition, sunDirection, sunDistance, SunSamples);
            float3 transmittance = exp(-(accumulatedDepth + sunDepth));
            float3 localScattering = atmosphere.RayleightScatteringRGB * density.x * rayleighPhase
                                   + atmosphere.MieScatteringRGB * density.y * miePhase;
            scattering += transmittance * localScattering * stepLength;
        }
    }

    float sunExit = GenesisRaySphere(camera, sunDirection, atmosphere.PlanetAtmosphereRadius).y;
    sunTransmittance = GenesisSunOccluded(atmosphere, camera, sunDirection)
        ? 0.0f
        : exp(-GenesisOpticalDepth(atmosphere, camera, sunDirection, sunExit, SunSamples));

    float3 radiance = scattering * atmosphere.StarIrradiance;

    // Compact approximation of higher scattering orders. The wavelength-
    // dependent term lifts the horizon without washing out the zenith.
    float horizon = pow(1.0f - saturate(abs(viewDirection.z)), 3.0f);
    radiance += radiance * (0.12f + 0.28f * horizon) *
                saturate(atmosphere.RayleightScatteringRGB * 30.0f);

    if (hitsGround)
    {
        float sunAmount = saturate(dot(normalize(camera + viewDirection * endDistance), sunDirection));
        radiance += atmosphere.GroundAlbedoRGB * atmosphere.StarIrradiance *
                    sunTransmittance * sunAmount / GENESIS_ATMOSPHERE_PI;
    }

    return max(radiance, 0.0f);
}

#endif // shader code
#endif // GENESIS_BRUNETON_ATMOSPHERE_HLSLI
