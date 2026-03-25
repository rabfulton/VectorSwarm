#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_grid_state;

layout(std140, set = 0, binding = 1) uniform GridAux {
    vec4 src[32];
    vec4 sphere_cell_flash[641];
} grid_aux;

layout(push_constant) uniform GridPC {
    vec4 p0;      /* x=viewport_w, y=viewport_h, z=grid_dx, w=grid_dy */
    vec4 p1;      /* x=distort_gain, y=strain_gain, z=state_w, w=state_h */
    vec4 p2;      /* rgb=dim_color, w=intensity_scale */
    vec4 p3;      /* rgb=bright_color, w=line_boost */
    vec4 p4;      /* x=camera_x, y=camera_y, z=world_w, w=world_h */
    vec4 p5;      /* x=mode(0=flat,1=sphere), y=sphere_radius_px, z=seed_count, w=line_px */
    vec4 p6;      /* sphere orientation quaternion wxyz */
    vec4 p7;      /* x=spring_distort_px, y=time_s, z=reserved, w=reserved */
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
    float edge_cell = d2 - d1;

    /* Convert to pixel space so line width/AA are resolution-consistent. */
    float px_per_cell = max(min(s.x, s.y), 1.0);
    float edge_px = edge_cell * px_per_cell;

    const float reference_h = 720.0;
    float viewport_scale = max(min(pc.p0.x, pc.p0.y) / reference_h, 0.001);
    float line_px = 2.00 * viewport_scale;
    float aa_min_px = 1.35 * viewport_scale;
    float half_w_px = 0.5 * line_px;
    float aa_px = max(fwidth(edge_px), aa_min_px);
    return 1.0 - smoothstep(half_w_px, half_w_px + aa_px, edge_px);
}

vec3 quat_rotate(vec4 q, vec3 v) {
    float w = q.x;
    vec3 u = q.yzw;
    vec3 uv = cross(u, v);
    vec3 uuv = cross(u, uv);
    return v + 2.0 * (w * uv + uuv);
}

vec3 quat_conjugate_rotate(vec4 q, vec3 v) {
    return quat_rotate(vec4(q.x, -q.y, -q.z, -q.w), v);
}

float sphere_voronoi_gap(vec3 local_p, int seed_count, out int best_idx) {
    const float golden_angle = 2.39996323;
    float best = -1.0e9;
    float second_best = -1.0e9;
    best_idx = 0;
    for (int i = 0; i < 2048; ++i) {
        if (i >= seed_count) {
            break;
        }
        float u = (float(i) + 0.5) / float(max(seed_count, 1));
        float y = 1.0 - 2.0 * u;
        float rr = sqrt(max(0.0, 1.0 - y * y));
        float phi = golden_angle * float(i);
        vec3 seed = vec3(cos(phi) * rr, y, sin(phi) * rr);
        float d = dot(local_p, seed);
        if (d > best) {
            second_best = best;
            best = d;
            best_idx = i;
        } else if (d > second_best) {
            second_best = d;
        }
    }
    return best - second_best;
}

float sphere_cell_flash_value(int cell_id) {
    int idx = clamp(cell_id, 0, 2047);
    vec4 packed = grid_aux.sphere_cell_flash[idx >> 2];
    int lane = idx & 3;
    if (lane == 0) return packed.x;
    if (lane == 1) return packed.y;
    if (lane == 2) return packed.z;
    return packed.w;
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
    float fill_alpha = 0.0;
    vec3 fill_col = pc.p3.rgb;
    if (pc.p5.x > 0.5) {
        vec2 center = vec2(pc.p0.x, pc.p0.y) * 0.5;
        float sphere_radius = max(pc.p5.y, 1.0);
        vec2 sphere_xy = vec2(
            frag_px.x - center.x + disp.x * pc.p7.x,
            center.y - frag_px.y - disp.y * pc.p7.x
        ) / sphere_radius;
        float r2 = dot(sphere_xy, sphere_xy);
        if (r2 > 1.0) {
            discard;
        }
        vec3 sphere_view = vec3(sphere_xy, sqrt(max(0.0, 1.0 - r2)));
        vec3 sphere_local = normalize(quat_conjugate_rotate(pc.p6, sphere_view));
        int cell_id = 0;
        float gap = sphere_voronoi_gap(sphere_local, int(pc.p5.z + 0.5), cell_id);
        float line_px = max(pc.p5.w, 0.05);
        float threshold = 0.00018 * line_px;
        float aa = max(fwidth(gap) * 0.55, 0.00006);
        line = 1.0 - smoothstep(threshold, threshold + aa, gap);
        {
            float cell_flash = clamp(sphere_cell_flash_value(cell_id), 0.0, 1.0);
            float interior = smoothstep(threshold + aa, threshold * 10.0 + aa, gap);
            fill_alpha = cell_flash * interior * pc.p7.z * pc.p2.w;
            fill_col = mix(pc.p2.rgb, pc.p3.rgb, 0.78 + 0.22 * cell_flash);
            line *= (1.0 + cell_flash * pc.p7.w);
        }
    }
    if (line <= 0.001 && fill_alpha <= 0.001) {
        discard;
    }

    float strain = clamp(length(disp) / max(min(pc.p0.z, pc.p0.w) * 0.36, 1.0), 0.0, 1.0);
    strain = max(strain, clamp(length(vel) * pc.p1.y, 0.0, 1.0));
    vec3 col = mix(pc.p2.rgb, pc.p3.rgb, 0.22 + 0.78 * strain);
    float alpha = line * (0.25 + 0.55 * strain) * pc.p2.w * pc.p3.w;
    if (pc.p5.x > 0.5) {
        vec3 line_col = mix(pc.p2.rgb, pc.p3.rgb, 0.86 + 0.14 * strain);
        float line_alpha = alpha * 10.5;
        float total_alpha = line_alpha + fill_alpha;
        col = (line_col * line_alpha + fill_col * fill_alpha) / max(total_alpha, 1.0e-4);
        alpha = total_alpha;
    }
    out_color = vec4(col, alpha);
}
