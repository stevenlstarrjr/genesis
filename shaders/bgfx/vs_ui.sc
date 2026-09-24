$input a_position, a_color0, a_color1, a_texcoord0, a_texcoord5, a_texcoord6, a_texcoord7
$output v_uiPos, v_uiUv, v_uiBounds, v_uiClip, v_uiStyle, v_uiFill, v_uiBorder
#include <bgfx_shader.sh>
uniform vec4 u_uiViewport;
void main() {
    v_uiPos = a_position.xy;
    v_uiUv = a_texcoord0;
    v_uiBounds = a_texcoord5;
    v_uiClip = a_texcoord6;
    v_uiStyle = a_texcoord7;
    v_uiFill = a_color0;
    v_uiBorder = a_color1;
    gl_Position = vec4(a_position.x * 2.0 / u_uiViewport.x - 1.0,
        1.0 - a_position.y * 2.0 / u_uiViewport.y, 0.0, 1.0);
}
