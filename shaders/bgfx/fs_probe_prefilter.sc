$input v_texcoord0

#include <bgfx_shader.sh>
#include "hdr_output.sh"

SAMPLERCUBE(s_probeSource, 0);
uniform vec4 u_localPrefilterParams; // roughness, face index, diffuse convolution, probe slot

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

vec3 cubeAtlasCoordinate(vec3 direction, float probeSlot)
{
    vec3 absoluteDirection = abs(direction);
    float face = 0.0;
    vec2 faceUv;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        if (direction.x >= 0.0) { face = 0.0; faceUv = vec2(-direction.z, direction.y) / absoluteDirection.x; }
        else { face = 1.0; faceUv = vec2(direction.z, direction.y) / absoluteDirection.x; }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        if (direction.y >= 0.0) { face = 2.0; faceUv = vec2(direction.x, -direction.z) / absoluteDirection.y; }
        else { face = 3.0; faceUv = vec2(direction.x, direction.z) / absoluteDirection.y; }
    }
    else
    {
        if (direction.z >= 0.0) { face = 4.0; faceUv = vec2(direction.x, direction.y) / absoluteDirection.z; }
        else { face = 5.0; faceUv = vec2(-direction.x, direction.y) / absoluteDirection.z; }
    }
    return vec3(faceUv.x * 0.5 + 0.5, 0.5 - faceUv.y * 0.5, probeSlot * 6.0 + face);
}

vec3 sampleProbeSource(vec3 direction)
{
    return textureCube(s_probeSource, direction).rgb;
}

vec3 importanceSampleGgx(vec2 xi, float roughness, vec3 normal)
{
    float a = max(roughness * roughness, 0.001);
    float phi = 6.28318530718 * xi.y;
    float cosTheta = sqrt((1.0 - xi.x) / max(1.0 + (a * a - 1.0) * xi.x, 1e-5));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 helper = abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta)
        + normal * cosTheta);
}

void main()
{
    vec3 reflection = cubemapDirection(v_texcoord0, int(u_localPrefilterParams.y + 0.5));
    if (u_localPrefilterParams.z > 0.5)
    {
        // Cosine-weighted hemisphere integral; store irradiance (not radiance).
        vec3 helper = abs(reflection.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent = normalize(cross(helper, reflection));
        vec3 bitangent = cross(reflection, tangent);
        vec3 irradiance = vec3_splat(0.0);
        for (int i = 0; i < 128; ++i)
        {
            float fi = float(i) + 0.5;
            float radius = sqrt(fi / 128.0);
            float phi = 6.28318530718 * fract(fi * 0.61803398875);
            vec3 direction = tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi))
                + reflection * sqrt(1.0 - fi / 128.0);
            irradiance += max(sampleProbeSource(direction), vec3_splat(0.0));
        }
        gl_FragColor = vec4(hdrForStorage(irradiance * (3.14159265 / 128.0)), 1.0);
        return;
    }
    float roughness = u_localPrefilterParams.x;
    if (roughness < 0.025)
    {
        gl_FragColor = vec4(hdrForStorage(sampleProbeSource(reflection)), 1.0);
        return;
    }
    vec3 accumulated = vec3_splat(0.0);
    float weight = 0.0;
    for (int sampleIndex = 0; sampleIndex < 64; ++sampleIndex)
    {
        float fi = float(sampleIndex) + 0.5;
        vec2 xi = vec2(fi / 64.0, fract(fi * 0.61803398875));
        vec3 halfVector = importanceSampleGgx(xi, roughness, reflection);
        vec3 direction = normalize(2.0 * dot(reflection, halfVector) * halfVector - reflection);
        float NoL = max(dot(reflection, direction), 0.0);
        accumulated += max(sampleProbeSource(direction), vec3_splat(0.0)) * NoL;
        weight += NoL;
    }
    gl_FragColor = vec4(hdrForStorage(accumulated / max(weight, 1e-4)), 1.0);
}
