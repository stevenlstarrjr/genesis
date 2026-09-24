$input v_texcoord0

#include <bgfx_shader.sh>
#include "hdr_output.sh"

// Runtime portion of Eric Bruneton's official precomputed model.
SAMPLER2D(s_atmosphereTransmittance, 0);
SAMPLER3D(s_atmosphereScattering, 1);
SAMPLER3D(s_atmosphereSingleMie, 2);
SAMPLER2D(s_hdri, 3);
uniform vec4 u_skyRight;
uniform vec4 u_skyUp;
uniform vec4 u_skyForward;
uniform vec4 u_sunDirection; // xyz direction, environment radiance multiplier
uniform vec4 u_atmosphereParams; // bottom radius, top radius, altitude km, scale
uniform vec4 u_hdriParams; // enabled, intensity, rotation radians, visible
uniform vec4 u_editorBackground; // solid grey editor background when no sky is authored

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
bool intersectsGround(float r, float mu, float bottomRadius)
{
    return mu < 0.0 && r * r * (mu * mu - 1.0) + bottomRadius * bottomRadius >= 0.0;
}
vec2 transmittanceUv(float r, float mu, float bottomRadius, float topRadius)
{
    float H = sqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho = safeSqrt(r * r - bottomRadius * bottomRadius);
    float d = distanceToTop(r, mu, topRadius);
    float dMin = topRadius - r;
    float dMax = rho + H;
    return vec2(texCoord((d - dMin) / max(dMax - dMin, 1e-6), 256.0), texCoord(rho / H, 64.0));
}
vec4 scatteringUvwz(float r, float mu, float muS, float nu, bool ground, float bottomRadius, float topRadius)
{
    float H = sqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho = safeSqrt(r * r - bottomRadius * bottomRadius);
    float uR = texCoord(rho / H, 32.0);
    float rmu = r * mu;
    float discriminant = rmu * rmu - r * r + bottomRadius * bottomRadius;
    float uMu;
    if (ground) {
        float d = -rmu - safeSqrt(discriminant);
        float dMin = r - bottomRadius;
        float dMax = rho;
        float x = dMax == dMin ? 0.0 : (d - dMin) / (dMax - dMin);
        uMu = 0.5 - 0.5 * texCoord(x, 64.0);
    } else {
        float d = -rmu + safeSqrt(discriminant + H * H);
        float dMin = topRadius - r;
        float dMax = rho + H;
        uMu = 0.5 + 0.5 * texCoord((d - dMin) / max(dMax - dMin, 1e-6), 64.0);
    }
    float d = distanceToTop(bottomRadius, muS, topRadius);
    float dMin = topRadius - bottomRadius;
    float dMax = H;
    float a = (d - dMin) / (dMax - dMin);
    float D = distanceToTop(bottomRadius, -0.20791169, topRadius);
    float A = (D - dMin) / (dMax - dMin);
    float uMuS = texCoord(max(1.0 - a / A, 0.0) / (1.0 + a), 32.0);
    return vec4((nu + 1.0) * 0.5, uMuS, uMu, uR);
}
void combinedScattering(float r, float mu, float muS, float nu, bool ground, float bottomRadius, float topRadius, out vec3 scattering, out vec3 mie)
{
    vec4 uvwz = scatteringUvwz(r, mu, muS, nu, ground, bottomRadius, topRadius);
    float x = uvwz.x * 7.0;
    float slice = floor(x);
    float f = x - slice;
    vec3 uvw0 = vec3((slice + uvwz.y) / 8.0, uvwz.z, uvwz.w);
    vec3 uvw1 = vec3((slice + 1.0 + uvwz.y) / 8.0, uvwz.z, uvwz.w);
    scattering = mix(texture3D(s_atmosphereScattering, uvw0).rgb, texture3D(s_atmosphereScattering, uvw1).rgb, f);
    mie = mix(texture3D(s_atmosphereSingleMie, uvw0).rgb, texture3D(s_atmosphereSingleMie, uvw1).rgb, f);
}
void main()
{
    // The HDR scene framebuffer also contains a normal attachment. Explicitly
    // initialize it for the sky; WebGPU requires an output for every write target.
    gl_FragData[1] = vec4(0.0, 0.0, 0.0, 0.0);
    if (u_editorBackground.x > 0.5)
    {
        gl_FragData[0] = vec4(hdrForStorage(vec3_splat(0.16)), 1.0);
        return;
    }
    vec2 ndc = vec2(v_texcoord0.x * 2.0 - 1.0, 1.0 - v_texcoord0.y * 2.0);
    vec3 ray = normalize(u_skyForward.xyz + u_skyRight.xyz * ndc.x * u_skyRight.w + u_skyUp.xyz * ndc.y * u_skyUp.w);
    if (u_hdriParams.x > 0.5 && u_hdriParams.w > 0.5)
    {
        gl_FragData[0] = vec4(hdrForStorage(texture2D(s_hdri, equirectangularUv(ray)).rgb * u_hdriParams.y), 1.0);
        return;
    }
    vec3 sun = normalize(u_sunDirection.xyz);
    float bottomRadius = u_atmosphereParams.x;
    float topRadius = u_atmosphereParams.y;
    float r = bottomRadius + max(u_atmosphereParams.z, 0.00001);
    float mu = clamp(ray.y, -1.0, 1.0);
    float muS = clamp(sun.y, -1.0, 1.0);
    float nu = clamp(dot(ray, sun), -1.0, 1.0);
    bool ground = intersectsGround(r, mu, bottomRadius);
    vec3 scattering;
    vec3 mie;
    combinedScattering(r, mu, muS, nu, ground, bottomRadius, topRadius, scattering, mie);
    float rayleighPhase = 3.0 / (16.0 * 3.14159265) * (1.0 + nu * nu);
    float g = 0.8;
    float miePhase = 3.0 / (8.0 * 3.14159265) * (1.0 - g * g) / (2.0 + g * g) * (1.0 + nu * nu) / pow(max(1.0 + g * g - 2.0 * g * nu, 1e-4), 1.5);
    vec3 sky = (scattering * rayleighPhase + mie * miePhase) * u_atmosphereParams.w;
    if (ground) sky *= vec3(0.12, 0.15, 0.18);
    float disc = smoothstep(0.9999844, 0.9999891, nu);
    vec3 transmittance = ground ? vec3_splat(0.0) : texture2D(s_atmosphereTransmittance, transmittanceUv(r, mu, bottomRadius, topRadius)).rgb;
    sky += disc * transmittance * vec3(18.0, 16.5, 14.0);
    gl_FragData[0] = vec4(hdrForStorage(sky * u_sunDirection.w), 1.0);
}
