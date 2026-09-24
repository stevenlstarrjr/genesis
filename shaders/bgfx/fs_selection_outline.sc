$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_selectionMask, 0);
SAMPLER2D(s_sceneSelection, 1);
uniform vec4 u_selectionOutline;
float visibleSelection(vec2 uv) {
    // The scene pass writes this ID with the fragments that write scene depth.
    // Keep the complete object mask for the silhouette, and use the ID only
    // to hide edges behind another object.
    return texture2D(s_selectionMask, uv).r * texture2D(s_sceneSelection, uv).r;
}
void main() {
    vec2 uv = v_texcoord0;
    vec2 dx = vec2(u_selectionOutline.x, 0.0);
    vec2 dy = vec2(0.0, u_selectionOutline.y);
    // The complete mask prevents internal edges at occlusion boundaries.
    float center = texture2D(s_selectionMask, uv).r;
    float edge = 0.0;
    edge = max(edge, visibleSelection(uv + dx));
    edge = max(edge, visibleSelection(uv - dx));
    edge = max(edge, visibleSelection(uv + dy));
    edge = max(edge, visibleSelection(uv - dy));
    edge = max(edge, visibleSelection(uv + dx + dy));
    edge = max(edge, visibleSelection(uv + dx - dy));
    edge = max(edge, visibleSelection(uv - dx + dy));
    edge = max(edge, visibleSelection(uv - dx - dy));
    float alpha = clamp(edge - center, 0.0, 1.0);
    gl_FragColor = vec4(1.0, 0.43, 0.06, alpha);
}
