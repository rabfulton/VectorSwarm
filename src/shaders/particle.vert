#version 450

layout(location = 0) in vec4 in_p0; /* x, y, radius_px, kind */
layout(location = 1) in vec4 in_col; /* r, g, b, a */
layout(location = 2) in vec4 in_aux; /* x=dir_x, y=dir_y, z=trail, w=heat */

layout(location = 0) out vec4 v_col;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out float v_kind;
layout(location = 3) out float v_trail;
layout(location = 4) out float v_heat;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height */
} pc;

void main() {
    vec2 corner;
    if (gl_VertexIndex == 0) corner = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) corner = vec2(1.0, -1.0);
    else if (gl_VertexIndex == 2) corner = vec2(-1.0, 1.0);
    else corner = vec2(1.0, 1.0);

    /* Keep GPU particles visually close to legacy CPU particle size. */
    float size_mul = 1.45;
    if (in_p0.w < 0.5) {
        size_mul = 1.65;
    } else if (in_p0.w > 1.5) {
        size_mul = 2.15;
    }
    vec2 dir = in_aux.xy;
    float dir_len = length(dir);
    if (dir_len > 1e-5) {
        dir /= dir_len;
    } else {
        dir = vec2(1.0, 0.0);
    }
    vec2 perp = vec2(-dir.y, dir.x);
    float trail = clamp(in_aux.z, 0.0, 1.0);
    float base_r = in_p0.z * size_mul;
    float tail_scale = 1.0 + trail * 2.10;
    float head_scale = 1.0 + trail * 0.45;
    float along = corner.x * ((corner.x < 0.0) ? (base_r * tail_scale) : (base_r * head_scale));
    float cross = corner.y * base_r * (1.0 - trail * 0.30);
    vec2 p = in_p0.xy + dir * along + perp * cross;
    vec2 ndc;
    ndc.x = (p.x / pc.params.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (p.y / pc.params.y) * 2.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_col = in_col;
    v_uv = vec2(corner.x * ((corner.x < 0.0) ? tail_scale : head_scale), corner.y * (1.0 - trail * 0.30));
    v_kind = in_p0.w;
    v_trail = trail;
    v_heat = in_aux.w;
}
