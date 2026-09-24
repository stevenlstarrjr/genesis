$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneDepth, 0);
SAMPLER2DSHADOW(s_volumeShadow0, 1);
SAMPLER2DSHADOW(s_volumeShadow1, 2);
SAMPLER2DSHADOW(s_volumeShadow2, 3);

uniform vec4 u_skyRight;
uniform vec4 u_skyUp;
uniform vec4 u_skyForward;
uniform vec4 u_cameraPos;
uniform vec4 u_lightDirIntensity;
uniform vec4 u_lightColor;
uniform vec4 u_ambientSky;
uniform vec4 u_fogParams;      // density, height falloff, anisotropy, max distance
uniform vec4 u_depthParams;    // near, far, scattering strength, unused
uniform vec4 u_shadowParams;
uniform vec4 u_qualityParams; // AO samples, contact steps, volume steps, effect scale
uniform vec4 u_cascadeSplits;
uniform mat4 u_lightMtx0;
uniform mat4 u_lightMtx1;
uniform mat4 u_lightMtx2;

float volumeShadowMap(vec3 worldPosition, mat4 lightMatrix, int cascade)
{
    vec4 lightPosition = mul(lightMatrix, vec4(worldPosition, 1.0));
    vec3 p = lightPosition.xyz / max(lightPosition.w, 1e-5);
    if (min(min(p.x, p.y), min(1.0 - p.x, 1.0 - p.y)) < 0.0 || p.z < 0.0 || p.z > 1.0)
        return -1.0;
    vec3 coord = vec3(p.xy, p.z - u_shadowParams.y * 2.0);
    if (cascade == 0) return shadow2D(s_volumeShadow0, coord);
    if (cascade == 1) return shadow2D(s_volumeShadow1, coord);
    return shadow2D(s_volumeShadow2, coord);
}

float volumeShadow(vec3 worldPosition)
{
    float distanceFromCamera = max(dot(worldPosition - u_cameraPos.xyz,
        normalize(u_skyForward.xyz)), 0.0);
    int primary = 2;
    if (distanceFromCamera < u_cascadeSplits.x)
        primary = 0;
    else if (distanceFromCamera < u_cascadeSplits.y)
        primary = 1;
    else if (distanceFromCamera >= u_cascadeSplits.z)
        return 1.0;

    float visibility = primary == 0 ? volumeShadowMap(worldPosition, u_lightMtx0, 0)
        : primary == 1 ? volumeShadowMap(worldPosition, u_lightMtx1, 1)
        : volumeShadowMap(worldPosition, u_lightMtx2, 2);
    if (visibility >= 0.0) return visibility;

    // Tight cascade bounds can be missed at an extreme screen edge because of
    // raster and matrix precision. Never turn such a miss directly into light;
    // try every overlapping cascade before accepting an unshadowed sample.
    visibility = volumeShadowMap(worldPosition, u_lightMtx0, 0);
    if (visibility >= 0.0) return visibility;
    visibility = volumeShadowMap(worldPosition, u_lightMtx1, 1);
    if (visibility >= 0.0) return visibility;
    visibility = volumeShadowMap(worldPosition, u_lightMtx2, 2);
    return visibility >= 0.0 ? visibility : 1.0;
}

void main()
{
    vec2 ndc = vec2(v_texcoord0.x * 2.0 - 1.0, 1.0 - v_texcoord0.y * 2.0);
    vec3 ray = normalize(u_skyForward.xyz + u_skyRight.xyz * ndc.x * u_skyRight.w + u_skyUp.xyz * ndc.y * u_skyUp.w);
    float depth = texture2D(s_sceneDepth, v_texcoord0).x;
    float nearPlane = u_depthParams.x;
    float farPlane = u_depthParams.y;
    float linearZ = nearPlane * farPlane / max(farPlane - depth * (farPlane - nearPlane), 1e-5);
    float rayForward = max(dot(ray, normalize(u_skyForward.xyz)), 0.05);
    float distanceLimit = min(linearZ / rayForward, u_fogParams.w);
    // The Bruneton sky already represents atmospheric scattering to space.
    // Integrating the local gameplay fog for another 90 metres on pixels with
    // no scene geometry washes sunsets into a flat white/beige field.
    if (depth >= 0.999999) distanceLimit = 0.0;

    vec3 sun = normalize(u_lightDirIntensity.xyz);
    float nu = clamp(dot(ray, sun), -1.0, 1.0);
    float rayleighPhase = 3.0 / (16.0 * 3.14159265) * (1.0 + nu * nu);
    float g = u_fogParams.z;
    float miePhase = 3.0 / (8.0 * 3.14159265) * (1.0 - g * g) / (2.0 + g * g)
        * (1.0 + nu * nu) / pow(max(1.0 + g * g - 2.0 * g * nu, 1e-4), 1.5);

    float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float volumeStepCount = max(u_qualityParams.z, 1.0);
    float stepLength = distanceLimit / volumeStepCount;
    vec3 radiance = vec3_splat(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < 24; ++i)
    {
        if (float(i) >= volumeStepCount) break;
        float distanceAlongRay = (float(i) + 0.25 + jitter * 0.5) * stepLength;
        vec3 samplePosition = u_cameraPos.xyz + ray * distanceAlongRay;
        float heightDensity = exp(-max(samplePosition.y, 0.0) * u_fogParams.y);
        float extinction = u_fogParams.x * heightDensity;
        float stepOpacity = 1.0 - exp(-extinction * stepLength);
        float visibility = volumeShadow(samplePosition);
        vec3 ambient = u_ambientSky.rgb * 0.42;
        vec3 direct = u_lightColor.rgb * u_lightDirIntensity.w
            * (rayleighPhase + miePhase) * u_depthParams.z * visibility;
        radiance += transmittance * stepOpacity * (ambient + direct);
        transmittance *= 1.0 - stepOpacity;
    }
    gl_FragData[0] = vec4(radiance, transmittance);
    // Preserve the source scene depth for bilateral reconstruction at full
    // resolution. Keeping it separate leaves fog alpha available for optical
    // transmittance.
    gl_FragData[1] = vec4_splat(depth);
}
