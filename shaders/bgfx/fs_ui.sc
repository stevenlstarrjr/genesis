$input v_uiPos, v_uiUv, v_uiBounds, v_uiClip, v_uiStyle, v_uiFill, v_uiBorder
#include <bgfx_shader.sh>
SAMPLER2D(s_uiAtlas, 0);
void main() {
    if (v_uiPos.x < v_uiClip.x || v_uiPos.y < v_uiClip.y ||
        v_uiPos.x >= v_uiClip.z || v_uiPos.y >= v_uiClip.w) discard;
    if (v_uiStyle.z > 0.5) {
        gl_FragColor = texture2D(s_uiAtlas, v_uiUv);
        return;
    }
    // A square, borderless panel divider must cover every pixel of its
    // four-pixel width; edge smoothing otherwise exposes the scene below.
    if (v_uiStyle.x <= 0.0 && v_uiStyle.y <= 0.0) {
        gl_FragColor = v_uiFill;
        return;
    }
    vec2 halfSize = v_uiBounds.zw * 0.5;
    vec2 center = v_uiBounds.xy + halfSize;
    float radius = min(v_uiStyle.x, min(halfSize.x, halfSize.y));
    vec2 q = abs(v_uiPos - center) - halfSize + radius;
    float distanceToEdge = length(max(q, vec2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - radius;
    float aa = max(fwidth(distanceToEdge), 0.5);
    float outer = 1.0 - smoothstep(-aa, aa, distanceToEdge);
    float inner = 1.0 - smoothstep(-aa, aa, distanceToEdge + v_uiStyle.y);
    // Frames cover what lies outside the rounded corners and restroke the
    // border above the container's children.
    vec4 color = v_uiStyle.w > 0.5
        ? v_uiBorder * (outer - inner) + v_uiFill * (1.0 - outer)
        : v_uiBorder * (outer - inner) + v_uiFill * inner;
    gl_FragColor = color;
}
