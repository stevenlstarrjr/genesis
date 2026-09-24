$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_indices, a_weight
$output v_worldPos, v_normal, v_tangent, v_materialUv0, v_materialUv1, v_occlusionUv, v_shadowPos0, v_shadowPos1, v_shadowPos2

#include <bgfx_shader.sh>

uniform mat4 u_lightMtx0;
uniform mat4 u_lightMtx1;
uniform mat4 u_lightMtx2;
uniform mat4 u_joints[64];
uniform vec4 u_skinning;

void main()
{
    vec4 position = vec4(a_position, 1.0);
    vec3 normal = a_normal;
    vec3 tangent = a_tangent.xyz;

    if (u_skinning.x > 0.5)
    {
        mat4 skin = u_joints[int(a_indices.x)] * a_weight.x
                  + u_joints[int(a_indices.y)] * a_weight.y
                  + u_joints[int(a_indices.z)] * a_weight.z
                  + u_joints[int(a_indices.w)] * a_weight.w;
        position = mul(skin, position);
        normal = mul(skin, vec4(normal, 0.0)).xyz;
        tangent = mul(skin, vec4(tangent, 0.0)).xyz;
    }

    vec4 world = mul(u_model[0], position);
    v_worldPos = world.xyz;
    v_normal = normalize(mul(u_model[0], vec4(normal, 0.0)).xyz);
    v_tangent = vec4(mul(u_model[0], vec4(tangent, 0.0)).xyz, a_tangent.w);
    v_materialUv0 = vec4(a_texcoord0, a_texcoord1);
    v_materialUv1 = vec4(a_texcoord2, a_texcoord3);
    v_occlusionUv = a_texcoord4;
    v_shadowPos0 = mul(u_lightMtx0, world);
    v_shadowPos1 = mul(u_lightMtx1, world);
    v_shadowPos2 = mul(u_lightMtx2, world);
    gl_Position = mul(u_viewProj, world);
}
