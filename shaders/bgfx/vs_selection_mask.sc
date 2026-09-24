$input a_position, a_texcoord0, a_indices, a_weight
$output v_selectionUv
#include <bgfx_shader.sh>
uniform mat4 u_joints[64];
uniform vec4 u_skinning;
void main() {
    vec4 position = vec4(a_position, 1.0);
    if (u_skinning.x > 0.5) {
        mat4 skin = u_joints[int(a_indices.x)] * a_weight.x
                  + u_joints[int(a_indices.y)] * a_weight.y
                  + u_joints[int(a_indices.z)] * a_weight.z
                  + u_joints[int(a_indices.w)] * a_weight.w;
        position = mul(skin, position);
    }
    v_selectionUv = a_texcoord0;
    // Match the scene vertex path exactly. A precombined model-view-projection
    // matrix shifts depth enough at close zoom to reject one side of the mask.
    vec4 world = mul(u_model[0], position);
    gl_Position = mul(u_viewProj, world);
}
