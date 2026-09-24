$input v_selectionUv
#include <bgfx_shader.sh>
SAMPLER2D(s_baseColor, 0);
uniform vec4 u_baseColorFactor;
uniform vec4 u_materialParams;
uniform vec4 u_textureFlags;
void main() {
    float alpha = u_baseColorFactor.a;
    if (u_textureFlags.x > 0.5) alpha *= texture2D(s_baseColor, v_selectionUv).a;
    if (alpha < u_materialParams.w) discard;
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
