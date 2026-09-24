$input v_worldPos, v_normal, v_tangent, v_materialUv0, v_materialUv1, v_occlusionUv, v_shadowPos0, v_shadowPos1, v_shadowPos2

#include <bgfx_shader.sh>
#include "hdr_output.sh"

SAMPLER2D(s_baseColor, 0);
SAMPLER2D(s_metalRough, 1);
SAMPLER2D(s_normal, 2);
SAMPLER2D(s_emissive, 3);
uniform vec4 u_viewportShading; // 0 wireframe, 1 solid, 2 material preview
uniform vec4 u_baseColorFactor;
uniform vec4 u_materialParams;
uniform vec4 u_emissiveFactor;
uniform vec4 u_textureFlags;
uniform vec4 u_selectionId;

void main()
{
    vec3 N = normalize(v_normal);
    vec3 color;
    if (u_viewportShading.x < 0.5)
    {
        color = vec3(0.025, 0.030, 0.038);
    }
    else if (u_viewportShading.x < 1.5)
    {
        // Clay lighting is stable as authored lights and materials change.
        float key = max(dot(N, normalize(vec3(0.45, 0.8, 0.45))), 0.0);
        float fill = max(dot(N, normalize(vec3(-0.65, 0.25, -0.35))), 0.0);
        color = vec3(0.38, 0.40, 0.43) * (0.50 + 0.65 * key + 0.18 * fill);
    }
    else
    {
        vec4 base = u_baseColorFactor;
        if (u_textureFlags.x > 0.5)
            base *= texture2D(s_baseColor, v_materialUv0.xy);
        if (base.a < u_materialParams.w)
            discard;
        float metallic = clamp(u_materialParams.x, 0.0, 1.0);
        float roughness = clamp(u_materialParams.y, 0.045, 1.0);
        if (u_textureFlags.y > 0.5)
        {
            vec4 packed = texture2D(s_metalRough, v_materialUv0.zw);
            metallic *= packed.b;
            roughness *= packed.g;
        }
        if (u_textureFlags.z > 0.5 && u_materialParams.z != 0.0)
        {
            vec3 T = v_tangent.xyz - N * dot(N, v_tangent.xyz);
            if (dot(T, T) < 1e-12)
                T = cross(abs(N.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), N);
            T = normalize(T);
            vec3 B = cross(N, T) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
            vec3 mapN = texture2D(s_normal, v_materialUv1.xy).xyz * 2.0 - 1.0;
            mapN.xy *= u_materialParams.z;
            N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
        }
        vec3 keyDirection = normalize(vec3(0.45, 0.8, 0.45));
        vec3 fillDirection = normalize(vec3(-0.65, 0.25, -0.35));
        float key = max(dot(N, keyDirection), 0.0);
        float fill = max(dot(N, fillDirection), 0.0);
        vec3 diffuse = base.rgb * (0.42 + 0.85 * key + 0.22 * fill) * (1.0 - metallic);
        vec3 reflected = mix(vec3_splat(0.04), base.rgb, metallic);
        float sheen = pow(max(dot(N, normalize(keyDirection + normalize(vec3(0.0, 0.0, 1.0)))), 0.0),
            mix(64.0, 4.0, roughness));
        vec3 emissive = u_emissiveFactor.rgb;
        if (u_textureFlags.w > 0.5)
            emissive *= texture2D(s_emissive, v_materialUv1.zw).rgb;
        color = diffuse + reflected * (0.15 + 1.4 * sheen) + emissive;
    }
    gl_FragData[0] = vec4(hdrForStorage(color), 1.0);
    gl_FragData[1] = vec4(N * 0.5 + 0.5, 1.0);
    gl_FragData[2] = vec4(u_selectionId.x, 0.0, 0.0, 1.0);
}
