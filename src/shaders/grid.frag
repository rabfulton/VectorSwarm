#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_grid_state;

layout(push_constant) uniform GridPC {
    vec4 p0;      /* x=viewport_w, y=viewport_h, z=grid_dx, w=grid_dy */
    vec4 p1;      /* x=distort_gain, y=strain_gain, z=state_w, w=state_h */
    vec4 p2;      /* rgb=dim_color, w=intensity_scale */
    vec4 p3;      /* rgb=bright_color, w=line_boost */
    vec4 p4;      /* x=camera_x, y=camera_y, z=world_w, w=world_h */
} pc;

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float voronoi_edge_band(vec2 world, vec2 cell_spacing) {
    vec2 s = max(cell_spacing, vec2(1.0));
    vec2 p = world / s;
    vec2 cell = floor(p);
    vec2 f = fract(p);

    float f1 = 1.0e9;
    float f2 = 1.0e9;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = vec2(float(i), float(j));
            vec2 h = hash22(cell + g);
            vec2 seed = g + vec2(0.16) + h * vec2(0.68);
            vec2 d = seed - f;
            float dist2 = dot(d, d);
            if (dist2 < f1) {
                f2 = f1;
                f1 = dist2;
            } else if (dist2 < f2) {
                f2 = dist2;
            }
        }
    }

    float d1 = sqrt(max(f1, 1.0e-8));
    float d2 = sqrt(max(f2, 1.0e-8));
    float edge = d2 - d1;

    float px_to_cell = 1.0 / max(min(s.x, s.y), 1.0);
    const float line_px = 1.60;
    float half_w = 0.5 * line_px * px_to_cell;
    float aa = max(fwidth(edge), 0.90 * px_to_cell);
    return 1.0 - smoothstep(half_w, half_w + aa, edge);
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    vec2 uv = frag_px / vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec4 st = texture(u_grid_state, uv);
    vec2 disp = st.xy * pc.p1.x;
    vec2 vel = st.zw;
    vec2 p = frag_px + disp;
    vec2 world = vec2(
        (pc.p4.x - pc.p4.z * 0.5) + p.x,
        (pc.p4.y - pc.p4.w * 0.5) + (pc.p0.y - p.y)
    );

    float line = voronoi_edge_band(world, vec2(pc.p0.z, pc.p0.w));
    if (line <= 0.001) {
        discard;
    }

    float strain = clamp(length(disp) / max(min(pc.p0.z, pc.p0.w) * 0.36, 1.0), 0.0, 1.0);
    strain = max(strain, clamp(length(vel) * pc.p1.y, 0.0, 1.0));
    vec3 col = mix(pc.p2.rgb, pc.p3.rgb, 0.22 + 0.78 * strain);
    float alpha = line * (0.25 + 0.55 * strain) * pc.p2.w * pc.p3.w;
    out_color = vec4(col, alpha);
}
