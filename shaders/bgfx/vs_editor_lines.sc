$input a_position, a_color0
$output v_color0, v_editorClip, v_editorWorld
#include <bgfx_shader.sh>
void main() {
    gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
    v_editorClip = gl_Position;
    v_editorWorld = a_position;
    v_color0 = a_color0;
}
