#version 450

layout(location = 0) in vec3 in_pos;

layout(location = 0) out vec3 v_local;
layout(location = 1) out vec3 v_view;

layout(push_constant) uniform Push {
    vec4 p0;     /* x=viewport_w, y=viewport_h, z=time_s, w=style_gain */
    vec4 p1;     /* x=center_x, y=center_y, z=sphere_radius_px, w=dpi */
    vec4 q;      /* sphere orientation quaternion wxyz */
    vec4 color0; /* primary palette */
    vec4 color1; /* secondary palette */
    vec4 color2; /* accent palette */
    vec4 tune0;  /* x=shell_offset, y=layer_t, z=layer_alpha, w=seed */
    vec4 tune1;  /* style-specific tuning */
} pc;

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.yzw, v);
    return v + q.x * t + cross(q.yzw, t);
}

void main() {
    vec3 local_n = normalize(in_pos);
    float shell_radius = pc.p1.z * (1.0 + pc.tune0.x);
    vec3 view_n = quat_rotate(pc.q, local_n);
    vec2 screen = pc.p1.xy + view_n.xy * shell_radius;
    vec2 ndc;
    ndc.x = (screen.x / pc.p0.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (screen.y / pc.p0.y) * 2.0;
    gl_Position = vec4(ndc, clamp(0.5 - view_n.z * 0.22, 0.0, 1.0), 1.0);
    v_local = local_n;
    v_view = view_n;
}
