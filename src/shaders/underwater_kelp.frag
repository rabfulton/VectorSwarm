#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 0) uniform UnderwaterLUT {
    vec4 kelp_seed_xjit[3 * 26];
    vec4 bubble_lut[8];
} lut;

layout(push_constant) uniform UnderwaterPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=density */
    vec4 p1; /* rgb=primary_dim, w=caustic_strength */
    vec4 p2; /* rgb=secondary, w=haze_alpha */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=caustic_scale, w=current_speed */
    vec4 p4; /* x=bubble_rate, y=palette_shift, z=world_w, w=world_h */
    vec4 p5; /* x=kelp_density, y=kelp_sway_amp, z=kelp_sway_speed, w=kelp_height */
    vec4 p6; /* x=kelp_parallax_strength, y=kelp_rt_w, z=kelp_rt_h */
} pc;

vec4 kelp_field(vec2 world_px, float t) {
    float kelp_density = max(pc.p5.x, 0.0);
    float sway_amp = max(pc.p5.y, 0.0);
    float sway_speed = max(pc.p5.z, 0.0);
    float kelp_height = max(pc.p5.w, 0.05);
    float parallax = max(pc.p6.x, 0.0);
    if (kelp_density <= 0.0) {
        return vec4(0.0);
    }
    float world_h = max(pc.p4.w, 1.0);
    float y01_global = world_px.y / world_h;
    const float base_y01 = 1.02;
    const float max_h_coeff = 0.48 * 1.16;
    if (y01_global > base_y01 || y01_global < (base_y01 - kelp_height * max_h_coeff)) {
        return vec4(0.0);
    }

    const int layers = 3;
    const int stems_per_layer = 26;
    vec3 accum_col = vec3(0.0);
    float accum_a = 0.0;

    for (int layer = 0; layer < layers; ++layer) {
        float lf = float(layer) / float(max(layers - 1, 1));
        float layer_density = clamp(kelp_density * (0.7 + 0.45 * (1.0 - lf)), 0.0, 1.0);
        float layer_parallax = (0.15 + 0.55 * lf) * parallax;
        vec2 wp = world_px + vec2(pc.p3.x * layer_parallax, 0.0);
        float tile_w = max(pc.p4.z, 1.0);
        float y01 = wp.y / world_h;
        float max_h01_layer = 0.48 * kelp_height * (0.82 + 0.34 * lf);
        if (y01 > base_y01 || y01 < (base_y01 - max_h01_layer)) {
            continue;
        }

        for (int i = 0; i < stems_per_layer; ++i) {
            float fi = float(i);
            vec4 kl = lut.kelp_seed_xjit[i];
            float seed = kl.x;
            if (seed > layer_density) {
                continue;
            }
            float x01 = (fi + 0.5) / float(stems_per_layer);
            x01 += kl.y;
            float x0 = x01 * pc.p4.z;

            float h01 = (0.22 + 0.26 * seed) * kelp_height * (0.82 + 0.34 * lf);
            h01 = max(h01, 0.05);
            float y_top01 = base_y01 - h01;
            if (y01 < y_top01 || y01 > base_y01) {
                continue;
            }

            float along = clamp((base_y01 - y01) / h01, 0.0, 1.0);
            float bend = sin(t * (0.55 + 0.35 * sway_speed) + fi * 0.87 + lf * 1.2 + along * 3.9);
            bend += 0.45 * sin(t * (1.15 + 0.45 * sway_speed) + fi * 1.31 + along * 7.2);
            float bend_px = bend * (6.0 + 15.0 * sway_amp) * pow(along, 1.25) * (0.95 - 0.25 * lf);
            float cx = x0 + bend_px;

            float width = mix(2.0, 10.0, 1.0 - along) * (0.82 + 0.24 * lf);
            float dx = wp.x - cx;
            dx -= tile_w * round(dx / tile_w);
            float d = abs(dx);
            float aa = max(fwidth(d), 0.60);
            float body = 1.0 - smoothstep(width * 0.70 - aa, width + aa, d);

            float aa_along = max(fwidth(along), 0.015);
            float along_lo = smoothstep(0.02 - aa_along, 0.12 + aa_along, along);
            float along_hi = smoothstep(1.0 + aa_along, 0.86 - aa_along, along);
            body *= along_lo * along_hi;

            float rib = 1.0 - smoothstep(max(0.0, -aa * 0.5), width * 0.28 + aa * 0.5, d);
            vec3 stem_col = mix(pc.p1.rgb * 0.70, pc.p2.rgb * 1.05, 0.25 + 0.55 * lf);
            stem_col *= (0.58 + 0.78 * lf) * (0.68 + 0.52 * rib);

            float alpha = body * (0.10 + 0.26 * lf);
            accum_col += stem_col * alpha * (1.0 - accum_a);
            accum_a += alpha * (1.0 - accum_a);
            if (accum_a > 0.98) {
                break;
            }
        }
    }

    return vec4(accum_col, clamp(accum_a, 0.0, 0.65));
}

void main() {
    float t = pc.p0.z;
    float density = max(pc.p0.w, 0.0);
    float rt_w = max(pc.p6.y, 1.0);
    float rt_h = max(pc.p6.z, 1.0);
    vec2 scale = vec2(max(pc.p0.x, 1.0) / rt_w, max(pc.p0.y, 1.0) / rt_h);
    vec2 frag_px = vec2(gl_FragCoord.xy) * scale;
    vec2 world_px = vec2(pc.p3.x, pc.p3.y) + frag_px;
    out_color = kelp_field(world_px, t) * density;
}
