$input v_texcoord0

#include <bgfx_shader.sh>
#include "hdr_output.sh"

SAMPLER3D(s_probeScattering, 0);
SAMPLER3D(s_probeSingleMie, 1);
SAMPLER2D(s_hdri, 2);
uniform vec4 u_probeSun;
uniform vec4 u_probeParams; // roughness, sky scale, bottom radius km, top radius km
uniform vec4 u_probeGround; // ground color, observer altitude km
uniform vec4 u_probeFace; // cubemap face index, output mip size, HDRI width/height
uniform vec4 u_hdriParams; // enabled, intensity, rotation radians, visible

vec2 equirectangularUv(vec3 direction)
{
    float cosine = cos(u_hdriParams.z);
    float sine = sin(u_hdriParams.z);
    vec3 rotated = vec3(direction.x * cosine - direction.z * sine, direction.y,
        direction.x * sine + direction.z * cosine);
    return vec2(fract(atan2(rotated.z, rotated.x) / 6.2831853 + 0.5),
        acos(clamp(rotated.y, -1.0, 1.0)) / 3.14159265);
}

float safeSqrt(float x) { return sqrt(max(x, 0.0)); }
float texCoord(float x, float size) { return 0.5 / size + x * (1.0 - 1.0 / size); }
float distanceToTop(float r, float mu, float topRadius)
{
    return max(-r * mu + safeSqrt(r * r * (mu * mu - 1.0) + topRadius * topRadius), 0.0);
}

vec4 scatteringUvwz(float r, float mu, float muS, float nu, bool ground,
    float bottomRadius, float topRadius)
{
    float H = sqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho = safeSqrt(r * r - bottomRadius * bottomRadius);
    float uR = texCoord(rho / H, 32.0);
    float rmu = r * mu;
    float discriminant = rmu * rmu - r * r + bottomRadius * bottomRadius;
    float uMu;
    if (ground)
    {
        float dGround = -rmu - safeSqrt(discriminant);
        float dMinGround = r - bottomRadius;
        float dMaxGround = rho;
        float x = dMaxGround == dMinGround ? 0.0 : (dGround - dMinGround) / (dMaxGround - dMinGround);
        uMu = 0.5 - 0.5 * texCoord(x, 64.0);
    }
    else
    {
        float dSky = -rmu + safeSqrt(discriminant + H * H);
        float dMinSky = topRadius - r;
        float dMaxSky = rho + H;
        uMu = 0.5 + 0.5 * texCoord((dSky - dMinSky) / max(dMaxSky - dMinSky, 1e-6), 64.0);
    }
    float dSun = distanceToTop(bottomRadius, muS, topRadius);
    float dMin = topRadius - bottomRadius;
    float dMax = H;
    float a = (dSun - dMin) / (dMax - dMin);
    float D = distanceToTop(bottomRadius, -0.20791169, topRadius);
    float A = (D - dMin) / (dMax - dMin);
    float uMuS = texCoord(max(1.0 - a / A, 0.0) / (1.0 + a), 32.0);
    return vec4((nu + 1.0) * 0.5, uMuS, uMu, uR);
}

float hdriSampleLod(vec3 direction, float solidAngle)
{
    // Equirectangular texels shrink toward the poles. Match the filtered sample
    // footprint in solid angle, not in UV area (Colbert/Krivanek, GPU Gems 3).
    float sinTheta = max(sqrt(max(1.0 - direction.y * direction.y, 0.0)),
        sin(1.5707963268 / u_probeFace.w));
    float texelSolidAngle = 19.7392088022 * sinTheta / (u_probeFace.z * u_probeFace.w);
    float maxMip = floor(log2(max(u_probeFace.z, u_probeFace.w)));
    return clamp(0.5 * log2(max(solidAngle / max(texelSolidAngle, 1e-12), 1.0)), 0.0, maxMip);
}

vec3 skyRadiance(vec3 direction, float solidAngle)
{
    vec3 hdriRadiance = texture2DLod(s_hdri, equirectangularUv(direction), hdriSampleLod(direction, solidAngle)).rgb
        * u_hdriParams.y;
    float bottomRadius = u_probeParams.z;
    float topRadius = u_probeParams.w;
    float r = bottomRadius + max(u_probeGround.w, 0.00001);
    float mu = clamp(direction.y, -1.0, 1.0);
    vec3 sun = normalize(u_probeSun.xyz);
    float muS = clamp(sun.y, -1.0, 1.0);
    float nu = clamp(dot(direction, sun), -1.0, 1.0);
    bool ground = mu < 0.0 && r * r * (mu * mu - 1.0) + bottomRadius * bottomRadius >= 0.0;
    vec3 radiance = u_probeGround.rgb;
    if (!ground)
    {
        vec4 uvwz = scatteringUvwz(r, mu, muS, nu, false, bottomRadius, topRadius);
        float x = uvwz.x * 7.0;
        float slice = floor(x);
        float f = x - slice;
        vec3 uvw0 = vec3((slice + uvwz.y) / 8.0, uvwz.z, uvwz.w);
        vec3 uvw1 = vec3((slice + 1.0 + uvwz.y) / 8.0, uvwz.z, uvwz.w);
        vec3 scattering = mix(texture3DLod(s_probeScattering, uvw0, 0.0).rgb,
            texture3DLod(s_probeScattering, uvw1, 0.0).rgb, f);
        vec3 mie = mix(texture3DLod(s_probeSingleMie, uvw0, 0.0).rgb,
            texture3DLod(s_probeSingleMie, uvw1, 0.0).rgb, f);
        float rayleighPhase = 3.0 / (16.0 * 3.14159265) * (1.0 + nu * nu);
        float g = 0.8;
        float miePhase = 3.0 / (8.0 * 3.14159265) * (1.0 - g * g) / (2.0 + g * g)
            * (1.0 + nu * nu) / pow(max(1.0 + g * g - 2.0 * g * nu, 1e-4), 1.5);
        radiance = max((scattering * rayleighPhase + mie * miePhase) * u_probeParams.y,
            vec3_splat(0.0));
    }
    // Preserve HDR energy while rejecting only pathological grazing LUT values.
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    vec3 atmosphereRadiance = radiance * min(1.0, 32.0 / max(peak, 1e-4));
    return mix(atmosphereRadiance, hdriRadiance, step(0.5, u_hdriParams.x));
}

vec3 importanceSampleGgx(vec2 xi, float roughness, vec3 normal)
{
    float a = max(roughness * roughness, 0.001);
    float phi = 6.2831853 * xi.y;
    float cosTheta = sqrt((1.0 - xi.x) / max(1.0 + (a * a - 1.0) * xi.x, 1e-5));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 helper = abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta)
        + normal * cosTheta);
}

vec3 cubemapDirection(vec2 uv, int face)
{
    vec2 p = uv * 2.0 - 1.0;
    p.y = -p.y;
    if (face == 0) return normalize(vec3( 1.0, p.y, -p.x));
    if (face == 1) return normalize(vec3(-1.0, p.y,  p.x));
    if (face == 2) return normalize(vec3( p.x, 1.0, -p.y));
    if (face == 3) return normalize(vec3( p.x,-1.0,  p.y));
    if (face == 4) return normalize(vec3( p.x, p.y,  1.0));
    return normalize(vec3(-p.x, p.y, -1.0));
}

void main()
{
    vec3 reflection = cubemapDirection(v_texcoord0, int(u_probeFace.x + 0.5));
    float majorAxis = max(abs(reflection.x), max(abs(reflection.y), abs(reflection.z)));
    // Jacobian of this cubemap texel. Even the mirror mip must integrate its
    // footprint; a single point lookup can hit/miss a tiny sun entirely.
    float outputSolidAngle = 4.0 * majorAxis * majorAxis * majorAxis
        / (u_probeFace.y * u_probeFace.y);
    if (u_probeParams.x < 0.025)
    {
        gl_FragColor = vec4(hdrForStorage(skyRadiance(reflection, outputSolidAngle)), 1.0);
        return;
    }
    vec3 accumulated = vec3_splat(0.0);
    float weight = 0.0;
    float sampleCount = u_hdriParams.x > 0.5 ? 128.0 : 32.0;
    float alpha = max(u_probeParams.x * u_probeParams.x, 0.001);
    float alphaSquared = alpha * alpha;
    for (int sampleIndex = 0; sampleIndex < 128; ++sampleIndex)
    {
        if (float(sampleIndex) >= sampleCount) break;
        float fi = float(sampleIndex) + 0.5;
        vec2 xi = vec2(fi / sampleCount, fract(fi * 0.61803398875));
        vec3 halfVector = importanceSampleGgx(xi, u_probeParams.x, reflection);
        vec3 direction = normalize(2.0 * dot(reflection, halfVector) * halfVector - reflection);
        float NoL = max(dot(reflection, direction), 0.0);
        float NoH = saturate(dot(reflection, halfVector));
        float denominator = NoH * NoH * (alphaSquared - 1.0) + 1.0;
        float distribution = alphaSquared / max(3.14159265 * denominator * denominator, 1e-12);
        // V=N in split-sum prefiltering, so D*NoH/(4*VoH) simplifies to D/4.
        float pdf = max(distribution * 0.25, 1e-6);
        float sampleSolidAngle = 1.0 / (sampleCount * pdf);
        if (NoL > 0.0)
            accumulated += skyRadiance(direction, max(sampleSolidAngle, outputSolidAngle)) * NoL;
        weight += NoL;
    }
    gl_FragColor = vec4(hdrForStorage(accumulated / max(weight, 1e-4)), 1.0);
}
