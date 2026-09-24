$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hud, 0);

void main()
{
    gl_FragColor = texture2D(s_hud, v_texcoord0);
}
