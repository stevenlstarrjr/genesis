#ifndef GENESIS_ENVIRONMENT_DIFFUSE_SH
#define GENESIS_ENVIRONMENT_DIFFUSE_SH

uniform vec4 u_hdriIrradiance[9]; // World-oriented SH9, cosine convolution + HDRI intensity baked in.

// Basis/order matches src/lighting/EnvironmentLighting.cpp. Returns irradiance,
// not radiance: the material applies albedo/pi. No extra hemisphere/fake ground.
vec3 hdriDiffuseIrradiance(vec3 n)
{
    vec3 e = u_hdriIrradiance[0].rgb * 0.2820947918;
    e += u_hdriIrradiance[1].rgb * (0.4886025119 * n.y);
    e += u_hdriIrradiance[2].rgb * (0.4886025119 * n.z);
    e += u_hdriIrradiance[3].rgb * (0.4886025119 * n.x);
    e += u_hdriIrradiance[4].rgb * (1.0925484306 * n.x * n.y);
    e += u_hdriIrradiance[5].rgb * (1.0925484306 * n.y * n.z);
    e += u_hdriIrradiance[6].rgb * (0.3153915653 * (3.0 * n.z * n.z - 1.0));
    e += u_hdriIrradiance[7].rgb * (1.0925484306 * n.x * n.z);
    e += u_hdriIrradiance[8].rgb * (0.5462742153 * (n.x * n.x - n.y * n.y));
    return max(e, vec3_splat(0.0));
}

#endif
