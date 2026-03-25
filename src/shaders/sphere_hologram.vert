#version 450

layout(location = 0) in vec4 in_pos_kind; /* xyz=local sphere position, w=mesh kind */
layout(location = 1) in vec2 in_arc_weight; /* x=arc coordinate, y=line weight */

layout(location = 0) out vec3 v_view;
layout(location = 1) out float v_kind;
layout(location = 2) out float v_arc;
layout(location = 3) out float v_weight;
layout(location = 4) out vec2 v_screen_uv;

layout(push_constant) uniform Push {
    vec4 p0;     /* x=viewport_w, y=viewport_h, z=time_s, w=style_gain */
    vec4 p1;     /* x=center_x, y=center_y, z=sphere_radius_px, w=dpi */
    vec4 q;      /* sphere orientation quaternion wxyz */
    vec4 color0; /* primary palette */
    vec4 color1; /* secondary palette */
    vec4 color2; /* accent palette */
    vec4 tune0;  /* style-specific tuning */
    vec4 tune1;  /* style-specific tuning */
} pc;

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.yzw, v);
    return v + q.x * t + cross(q.yzw, t);
}

vec2 line_aa_offset(float tap_code, float px) {
    if (tap_code > 1.5 && tap_code < 2.5) {
        return vec2(px, 0.0);
    }
    if (tap_code > 2.5 && tap_code < 3.5) {
        return vec2(-px, 0.0);
    }
    if (tap_code > 3.5 && tap_code < 4.5) {
        return vec2(0.0, px);
    }
    if (tap_code > 4.5 && tap_code < 5.5) {
        return vec2(0.0, -px);
    }
    return vec2(0.0);
}

void main() {
    vec3 pos_view = quat_rotate(pc.q, in_pos_kind.xyz);
    float shell_r = max(length(pos_view), 1.0e-4);
    vec3 dir_view = pos_view / shell_r;
    vec2 screen = pc.p1.xy + pos_view.xy * pc.p1.z;
    screen += line_aa_offset(pc.tune1.w, 0.42 * pc.p1.w);
    vec2 ndc;
    ndc.x = (screen.x / pc.p0.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (screen.y / pc.p0.y) * 2.0;
    gl_Position = vec4(ndc, clamp(0.5 - dir_view.z * 0.25, 0.0, 1.0), 1.0);
    v_view = dir_view;
    v_kind = in_pos_kind.w;
    v_arc = in_arc_weight.x;
    v_weight = in_arc_weight.y;
    v_screen_uv = screen / max(pc.p0.xy, vec2(1.0));
}
