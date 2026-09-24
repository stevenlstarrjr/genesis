$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_aoDepth, 0);
SAMPLER2D(s_aoNormal, 1);
uniform vec4 u_skyRight;
uniform vec4 u_skyUp;
uniform vec4 u_skyForward;
uniform vec4 u_cameraPos;
uniform vec4 u_lightDirIntensity;
uniform vec4 u_depthParams;
uniform vec4 u_aoParams; // radius, bias, contact distance, contact thickness
uniform vec4 u_qualityParams; // AO samples, contact steps, volume steps, effect scale

float linearDepth(float depth)
{
    return u_depthParams.x * u_depthParams.y
        / max(u_depthParams.y - depth * (u_depthParams.y - u_depthParams.x), 1e-5);
}

vec3 viewRay(vec2 uv)
{
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return normalize(u_skyForward.xyz + u_skyRight.xyz * ndc.x * u_skyRight.w
        + u_skyUp.xyz * ndc.y * u_skyUp.w);
}

vec3 worldPosition(vec2 uv, float depth)
{
    vec3 ray = viewRay(uv);
    float rayForward = max(dot(ray, normalize(u_skyForward.xyz)), 0.05);
    return u_cameraPos.xyz + ray * (linearDepth(depth) / rayForward);
}

vec2 projectWorld(vec3 position, out float viewZ)
{
    vec3 relative = position - u_cameraPos.xyz;
    vec3 forward = normalize(u_skyForward.xyz);
    vec3 right = normalize(u_skyRight.xyz);
    vec3 up = normalize(u_skyUp.xyz);
    viewZ = dot(relative, forward);
    vec2 ndc = vec2(dot(relative, right) / max(viewZ * u_skyRight.w, 1e-4),
        dot(relative, up) / max(viewZ * u_skyUp.w, 1e-4));
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

void main()
{
    float centerDepth = texture2D(s_aoDepth, v_texcoord0).x;
    if (centerDepth >= 0.999999)
    {
        gl_FragData[0] = vec4(1.0, 1.0, 0.0, 1.0);
        gl_FragData[1] = vec4_splat(centerDepth);
        return;
    }

    vec3 position = worldPosition(v_texcoord0, centerDepth);
    vec3 normal = normalize(texture2D(s_aoNormal, v_texcoord0).xyz * 2.0 - 1.0);
    vec3 helper = abs(normal.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    float occlusion = 0.0;

    float aoSampleCount = max(u_qualityParams.x, 1.0);
    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)
    {
        if (float(sampleIndex) >= aoSampleCount) break;
        float fi = float(sampleIndex) + 0.5;
        float z = fi / aoSampleCount;
        float angle = fi * 2.3999632;
        float radial = sqrt(max(1.0 - z * z, 0.0));
        vec3 direction = tangent * (cos(angle) * radial) + bitangent * (sin(angle) * radial) + normal * z;
        float sampleRadius = u_aoParams.x * (0.25 + 0.75 * fi / aoSampleCount);
        vec3 probe = position + direction * sampleRadius;
        float expectedZ;
        vec2 uv = projectWorld(probe, expectedZ);
        if (expectedZ > 0.0 && min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)) >= 0.0)
        {
            float sampleDepth = texture2DLod(s_aoDepth, uv, 0.0).x;
            float actualZ = linearDepth(sampleDepth);
            float delta = expectedZ - actualZ;
            float range = saturate(1.0 - abs(delta) / max(u_aoParams.x, 1e-4));
            occlusion += step(u_aoParams.y, delta) * range;
        }
    }
    float ao = pow(saturate(1.0 - occlusion / aoSampleCount), 1.35);

    float contactOcclusion = 0.0;
    vec3 sun = normalize(u_lightDirIntensity.xyz);
    vec3 rayStart = position + normal * max(u_aoParams.y * 2.0, 0.02);
    float contactStepCount = max(u_qualityParams.y, 1.0);
    for (int stepIndex = 1; stepIndex <= 12; ++stepIndex)
    {
        if (float(stepIndex) > contactStepCount) break;
        float rayDistance = u_aoParams.z * float(stepIndex) / contactStepCount;
        vec3 probe = rayStart + sun * rayDistance;
        float expectedZ;
        vec2 uv = projectWorld(probe, expectedZ);
        if (expectedZ > 0.0 && min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)) >= 0.0)
        {
            float actualZ = linearDepth(texture2DLod(s_aoDepth, uv, 0.0).x);
            float separation = expectedZ - actualZ;
            vec3 blockerNormal = normalize(texture2DLod(s_aoNormal, uv, 0.0).xyz * 2.0 - 1.0);
            float differentSurface = 1.0 - step(0.82, dot(blockerNormal, normal));
            float blocker = step(u_aoParams.y, separation) * step(separation, u_aoParams.w)
                * differentSurface;
            contactOcclusion += blocker;
        }
    }
    float contact = 1.0 - saturate(contactOcclusion * 0.5);

    gl_FragData[0] = vec4(ao, contact, 0.0, 1.0);
    gl_FragData[1] = vec4_splat(centerDepth);
}
