$input a_position, a_indices, a_weight

#include <bgfx_shader.sh>

uniform mat4 u_joints[64];
uniform vec4 u_skinning;

void main()
{
    vec4 position = vec4(a_position, 1.0);
    if (u_skinning.x > 0.5)
    {
        mat4 skin = u_joints[int(a_indices.x)] * a_weight.x
                  + u_joints[int(a_indices.y)] * a_weight.y
                  + u_joints[int(a_indices.z)] * a_weight.z
                  + u_joints[int(a_indices.w)] * a_weight.w;
        position = mul(skin, position);
    }
    gl_Position = mul(u_modelViewProj, position);
}
