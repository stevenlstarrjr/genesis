$input v_texcoord0

#include <bgfx_shader.sh>
#include "hdr_output.sh"

SAMPLER2D(s_bloomSource,0);
SAMPLER2D(s_bloomDetail,1);
uniform vec4 u_bloomParams; // low-resolution texel x/y, detail texel x/y

void main()
{
    vec2 t = u_bloomParams.xy;
    vec3 sum = texture2D(s_bloomSource,v_texcoord0).rgb * 4.0;
    sum += (texture2D(s_bloomSource,v_texcoord0+t*vec2(-1.0,0.0)).rgb
          + texture2D(s_bloomSource,v_texcoord0+t*vec2( 1.0,0.0)).rgb
          + texture2D(s_bloomSource,v_texcoord0+t*vec2(0.0,-1.0)).rgb
          + texture2D(s_bloomSource,v_texcoord0+t*vec2(0.0, 1.0)).rgb) * 2.0;
    sum += texture2D(s_bloomSource,v_texcoord0+t*vec2(-1.0,-1.0)).rgb
         + texture2D(s_bloomSource,v_texcoord0+t*vec2( 1.0,-1.0)).rgb
         + texture2D(s_bloomSource,v_texcoord0+t*vec2(-1.0, 1.0)).rgb
         + texture2D(s_bloomSource,v_texcoord0+t*vec2( 1.0, 1.0)).rgb;
    // Smooth the fine level as well. Mixing an unfiltered first level straight
    // back in exposes its compact footprint around very intense one-pixel glints.
    // Separable [1,4,6,4,1]/16 Gaussian, folded into nine bilinear fetches.
    // The +/-1 and +/-2 taps combine at +/-1.2 with weight 5/16.
    vec2 d = u_bloomParams.zw * 1.2;
    vec3 detail = texture2D(s_bloomDetail,v_texcoord0).rgb * 0.140625;
    detail += (texture2D(s_bloomDetail,v_texcoord0+d*vec2(-1.0,0.0)).rgb
             + texture2D(s_bloomDetail,v_texcoord0+d*vec2( 1.0,0.0)).rgb
             + texture2D(s_bloomDetail,v_texcoord0+d*vec2(0.0,-1.0)).rgb
             + texture2D(s_bloomDetail,v_texcoord0+d*vec2(0.0, 1.0)).rgb) * 0.1171875;
    detail += (texture2D(s_bloomDetail,v_texcoord0+d*vec2(-1.0,-1.0)).rgb
            + texture2D(s_bloomDetail,v_texcoord0+d*vec2( 1.0,-1.0)).rgb
            + texture2D(s_bloomDetail,v_texcoord0+d*vec2(-1.0, 1.0)).rgb
            + texture2D(s_bloomDetail,v_texcoord0+d*vec2( 1.0, 1.0)).rgb) * 0.09765625;
    gl_FragColor = vec4(hdrForStorage(mix(detail,sum/16.0,0.65)),1.0);
}
