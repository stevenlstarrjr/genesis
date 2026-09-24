$input v_texcoord0

#include <bgfx_shader.sh>

vec3 importanceSampleGgx(vec2 xi, float roughness)
{
    float a = max(roughness * roughness, 0.001);
    float phi = 6.28318530718 * xi.y;
    float cosTheta = sqrt((1.0 - xi.x) / max(1.0 + (a * a - 1.0) * xi.x, 1e-5));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float geometrySchlickGgxIbl(float NoV, float roughness)
{
    float k = roughness * roughness * 0.5;
    return NoV / max(NoV * (1.0 - k) + k, 1e-5);
}

void main()
{
    float NoV = clamp(v_texcoord0.x, 0.001, 0.999);
    float roughness = clamp(v_texcoord0.y, 0.001, 0.999);
    vec3 V = vec3(sqrt(max(1.0 - NoV * NoV, 0.0)), 0.0, NoV);
    float scale = 0.0;
    float bias = 0.0;
    for (int sampleIndex = 0; sampleIndex < 64; ++sampleIndex)
    {
        float fi = float(sampleIndex) + 0.5;
        vec2 xi = vec2(fi / 64.0, fract(fi * 0.61803398875));
        vec3 H = importanceSampleGgx(xi, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);
        if (NoL > 0.0)
        {
            float G = geometrySchlickGgxIbl(NoV, roughness)
                * geometrySchlickGgxIbl(NoL, roughness);
            float visibility = G * VoH / max(NoH * NoV, 1e-5);
            float fresnel = pow(1.0 - VoH, 5.0);
            scale += (1.0 - fresnel) * visibility;
            bias += fresnel * visibility;
        }
    }
    gl_FragColor = vec4(scale / 64.0, bias / 64.0, 0.0, 1.0);
}
