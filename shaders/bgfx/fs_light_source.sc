$input v_normal

#include <bgfx_shader.sh>
#include "hdr_output.sh"

uniform vec4 u_sourceColor;

void main()
{
    gl_FragData[0] = vec4(hdrForStorage(u_sourceColor.rgb), 1.0);
    gl_FragData[1] = vec4(normalize(v_normal) * 0.5 + 0.5, 1.0);
}
