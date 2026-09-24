$input v_color0, v_editorClip, v_editorWorld
#include <bgfx_shader.sh>
SAMPLER2D(s_editorDepth, 0);
uniform vec4 u_editorParams;
uniform vec4 u_editorGrid;
void main() {
    vec3 ndc = v_editorClip.xyz / v_editorClip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (u_editorParams.z < 0.5) uv.y = 1.0 - uv.y;
    float depth = mix(ndc.z, ndc.z * 0.5 + 0.5, u_editorParams.y);
    float depthBias = u_editorParams.w < -0.5 ? 0.0005 : 0.000015;
    if (u_editorParams.x > 0.5 && depth > texture2D(s_editorDepth, uv).r + depthBias) discard;
    vec4 color = v_color0;
    if (u_editorParams.w > 0.5) {
        float distanceToCamera = length(v_editorWorld.xz - u_editorGrid.xy);
        color.a *= 1.0 - smoothstep(u_editorGrid.z, u_editorGrid.w, distanceToCamera);
    }
    gl_FragColor = color;
}
