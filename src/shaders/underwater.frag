#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 0) uniform UnderwaterLUT {
    vec4 kelp_seed_xjit[3 * 26];
    vec4 bubble_lut[8];
} lut;
layout(set = 0, binding = 1) uniform sampler2D u_kelp;
layout(set = 0, binding = 2) uniform sampler2D u_noise;

layout(push_constant) uniform UnderwaterPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=density */
    vec4 p1; /* rgb=primary_dim, w=caustic_strength */
    vec4 p2; /* rgb=secondary, w=haze_alpha */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=caustic_scale, w=current_speed */
    vec4 p4; /* x=bubble_rate, y=palette_shift, z=world_w, w=world_h */
    vec4 p5; /* x=kelp_density, y=kelp_sway_amp, z=kelp_sway_speed, w=kelp_height */
    vec4 p6; /* x=kelp_parallax_strength, y=kelp_rt_w, z=kelp_rt_h, w=high_quality */
    vec4 p7; /* x=kelp_tint_r, y=kelp_tint_g, z=kelp_tint_b, w=kelp_tint_strength */
} pc;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/* Replaces the three fbm() calls from the original shader.
   UVs are the original FBM inputs divided by 5, so the noise texture
   (baked with first-octave period = tex_size/5) produces the same spatial
   frequencies as fbm(n_uv * vec2(5.0, 2.2)), fbm(n_uv * vec2(9.8, 4.0))
   and fbm(n_uv * vec2(14.0, 8.0)) respectively. */
vec3 sample_underwater_noise(vec2 n_uv, vec2 flow, float t, float cur) {
    vec2 uv0 = n_uv * vec2(1.00, 0.44) + flow * 0.20;
    vec2 uv1 = n_uv * vec2(1.96, 0.80) - flow * 0.31 + vec2(1.22, -0.74);
    vec2 uv2 = n_uv * vec2(2.80, 1.60) + vec2(t * 0.030, -t * 0.016) * cur;
    return vec3(
        texture(u_noise, uv0).r,
        texture(u_noise, uv1).g,
        texture(u_noise, uv2).b
    );
}

float bubble_field(vec2 world_px, float t, float bubble_rate) {
    float tile_w = max(pc.p4.z, 1.0);
    float d = 0.0;
    const int emit_n = 7;
    for (int i = 0; i < emit_n; ++i) {
        float fi = float(i);
        vec4 bl = lut.bubble_lut[i];
        float ex = bl.x;
        float cycle = bl.w;
        float phase = fract((t * (0.10 + 0.03 * fi) * max(bubble_rate, 0.0)) + bl.y);
        float y01 = 1.0 - phase;
        float x01 = ex + sin((phase * 1.8 + fi * 0.41) * 3.14159265) * bl.z;
        vec2 c = vec2(x01 * pc.p4.z, y01 * pc.p4.w);
        vec2 delta = world_px - c;
        delta.x -= tile_w * round(delta.x / tile_w);
        float r = mix(6.0, 18.0, hash12(vec2(fi, floor(t / cycle) + 3.0)));
        float dd = length(delta);
        float ring = exp(-pow((dd - r) / max(r * 0.20, 1.0), 2.0));
        d += 0.36 * ring;
    }
    return clamp(d, 0.0, 1.0);
}

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
    const float max_h_coeff = 0.48 * 1.16; /* max((0.22+0.26*seed)*(0.82+0.34*lf)) */
    if (y01_global > base_y01 || y01_global < (base_y01 - kelp_height * max_h_coeff)) {
        return vec4(0.0);
    }

    const int layers = 3;
    const int stems_per_layer = 26;
    /* xjit <= 0.04 UV = ~1.04 slots; bend <= ~21px = ~0.27 slots.
       Checking +-2 slots around the fragment's slot covers all contributors. */
    const int slot_radius = 2;
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

        float stem_spacing = tile_w / float(stems_per_layer);
        int slot_center = int(floor(mod(wp.x, tile_w) / stem_spacing));

        for (int ds = -slot_radius; ds <= slot_radius; ++ds) {
            int i = slot_center + ds;
            int i_wrap = ((i % stems_per_layer) + stems_per_layer) % stems_per_layer;
            float fi = float(i_wrap);
            int lut_idx = layer * stems_per_layer + i_wrap;
            vec4 kl = lut.kelp_seed_xjit[lut_idx];
            float seed = kl.x;
            if (seed > layer_density) {
                continue;
            }

            float x01 = (fi + 0.5) / float(stems_per_layer);
            x01 += kl.y;
            float x0 = x01 * tile_w;

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
            float body = 1.0 - smoothstep(width * 0.70, width, d);
            body *= smoothstep(0.02, 0.12, along) * smoothstep(1.0, 0.86, along);

            float rib = 1.0 - smoothstep(0.0, width * 0.28, d);
            vec3 stem_col = mix(pc.p1.rgb * 0.70, pc.p2.rgb * 1.05, 0.25 + 0.55 * lf);
            float stem_shade = (0.58 + 0.78 * lf) * (0.68 + 0.52 * rib);
            stem_col *= stem_shade;

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

vec4 sample_kelp_tent(vec2 uv) {
    ivec2 ts = textureSize(u_kelp, 0);
    vec2 texel = 1.0 / vec2(max(ts.x, 1), max(ts.y, 1));
    vec4 c = texture(u_kelp, uv) * 0.40;
    c += texture(u_kelp, uv + vec2(texel.x, 0.0)) * 0.15;
    c += texture(u_kelp, uv - vec2(texel.x, 0.0)) * 0.15;
    c += texture(u_kelp, uv + vec2(0.0, texel.y)) * 0.15;
    c += texture(u_kelp, uv - vec2(0.0, texel.y)) * 0.15;
    return c;
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    float w = max(pc.p0.x, 1.0);
    float h = max(pc.p0.y, 1.0);
    float t = pc.p0.z;
    float density = max(pc.p0.w, 0.0);

    vec2 world_px = vec2(pc.p3.x, pc.p3.y) + frag_px;
    vec2 world_uv = world_px / vec2(max(pc.p4.z, 1.0), max(pc.p4.w, 1.0));

    float scale = max(pc.p3.z, 0.05);
    float cur = max(pc.p3.w, 0.0);
    vec2 flow = vec2(t * 0.040, -t * 0.022) * cur;
    vec2 n_uv = world_uv * scale;

    vec3 n = sample_underwater_noise(n_uv, flow, t, cur);
    float haze = smoothstep(0.28, 0.86, n.x * 0.72 + n.y * 0.28);
    haze *= density;

    float ca = pow(clamp(n.z, 0.0, 1.0), 2.2);
    ca *= max(pc.p1.w, 0.0) * density;

    float bubbles = bubble_field(world_px, t, pc.p4.x) * density;
    vec4 kelp = (pc.p6.w > 0.5)
        ? (kelp_field(world_px, t) * density)
        : sample_kelp_tent(frag_px / vec2(w, h));
    float kelp_override = clamp(pc.p7.w, 0.0, 1.0);
    if (kelp_override > 0.0) {
        vec3 tint_rgb = max(pc.p7.rgb, vec3(0.0));
        kelp.rgb = mix(kelp.rgb, tint_rgb, kelp_override);
    }
    float shade = clamp(0.18 + haze * 0.75 + ca * 0.60, 0.0, 1.0);

    vec3 col_a = pc.p1.rgb;
    vec3 col_b = pc.p2.rgb;
    float shift = pc.p4.y;
    vec3 col = mix(col_a, col_b, clamp(shade + shift * 0.20, 0.0, 1.0));
    col += vec3(0.55, 0.72, 0.88) * (0.42 * bubbles + 0.18 * ca);
    float kelp_mix_a = clamp(kelp.a * mix(1.05, 2.60, kelp_override), 0.0, 1.0);
    float kelp_bg_dim = kelp_mix_a * mix(0.08, 0.42, kelp_override);
    col *= (1.0 - kelp_bg_dim);
    vec3 kelp_mix_col = mix(kelp.rgb + col * 0.10, kelp.rgb, 0.70 + 0.30 * kelp_override);
    col = mix(col, kelp_mix_col, kelp_mix_a);

    float alpha = clamp((0.07 + 0.20 * haze + 0.14 * ca + 0.30 * bubbles) * max(pc.p2.w, 0.0), 0.0, 0.60);
    alpha = clamp(alpha + kelp_mix_a * max(pc.p2.w, 0.0), 0.0, 0.70);
    out_color = vec4(col, alpha);
}
