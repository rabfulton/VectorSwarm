#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 1) uniform sampler2D u_kelp;
layout(set = 0, binding = 2) uniform sampler2D u_noise;

layout(push_constant) uniform UnderwaterPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=spore_density */
    vec4 p1; /* rgb=dark_bark, w=spore_scale */
    vec4 p2; /* rgb=haze_tint, w=haze_alpha */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=drift_speed, w=canopy_density */
    vec4 p4; /* x=world_w, y=world_h, z=parallax_strength, w=flora_density */
    vec4 p5; /* rgb=biolume_tint, w=membrane_glow */
    vec4 p6; /* x=root_arch_density, y=godray_strength */
    vec4 p7; /* x=branch_wobble_amp, y=branch_wobble_speed, z=biolume_pulse_freq, w=foreground_alpha */
} pc;

float sat(float x) {
    return clamp(x, 0.0, 1.0);
}

float noise01(vec2 uv) {
    vec4 n = texture(u_noise, uv);
    return dot(n.rgb, vec3(0.52, 0.31, 0.17));
}

float fbm(vec2 uv) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * noise01(uv);
        uv = uv * 2.03 + vec2(0.37, -0.29);
        a *= 0.5;
    }
    return v;
}

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float voronoi_edge(vec2 p) {
    vec2 g = floor(p);
    vec2 f = fract(p);
    float d1 = 1.0e9;
    float d2 = 1.0e9;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 o = vec2(float(i), float(j));
            vec2 h = hash22(g + o);
            vec2 r = o + h - f;
            float d = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return max(sqrt(max(d2, 0.0)) - sqrt(max(d1, 0.0)), 0.0);
}

float trunk_band(vec2 uv, float camera_x, float layer_parallax, float density, float wobble_amp, float wobble_speed, float t) {
    float lane_count = mix(7.0, 18.0, sat(density * 0.35));
    float x = (uv.x + camera_x * layer_parallax) * lane_count;
    float cell = floor(x);
    float fx = fract(x) - 0.5;
    float accum = 0.0;

    for (int i = -1; i <= 1; ++i) {
        float fi = float(i);
        float id = cell + fi;
        float seed = hash11(id * 17.13 + layer_parallax * 41.7 + 0.3);
        float seed_b = hash11(id * 29.71 + layer_parallax * 13.2 + 7.1);
        float sway = sin(t * (0.24 + wobble_speed * 0.26) + id * 0.73 + uv.y * 3.8);
        float center = fi + (seed - 0.5) * 0.72 + sway * wobble_amp * (0.06 + 0.05 * seed_b);
        float width = mix(0.07, 0.22, seed_b) * mix(0.75, 1.20, density * 0.18);
        float height = mix(0.24, 0.66, seed) * mix(0.75, 1.15, density * 0.20);
        float y_base = 1.02;
        float y_top = y_base - height;
        float stem = 1.0 - smoothstep(width * 0.82, width, abs(fx - center));
        float vertical = smoothstep(y_top - 0.03, y_top + 0.03, uv.y) * (1.0 - smoothstep(y_base - 0.02, y_base + 0.02, uv.y));
        float cap_dx = (fx - center) / max(width * mix(1.7, 2.8, seed), 1.0e-4);
        float cap_dy = (uv.y - (y_top + height * 0.10)) / max(height * mix(0.12, 0.22, seed_b), 1.0e-4);
        float cap_r2 = dot(vec2(cap_dx, cap_dy), vec2(cap_dx, cap_dy));
        float cap = 1.0 - smoothstep(0.76, 1.0, cap_r2);
        accum = max(accum, max(stem * vertical, cap));
    }

    return sat(accum);
}

float root_arch_band(vec2 uv, float camera_x, float parallax, float density, float t) {
    float arc_count = mix(4.0, 10.0, sat(density * 0.30));
    float x = (uv.x + camera_x * parallax) * arc_count;
    float cell = floor(x);
    float fx = fract(x) - 0.5;
    float accum = 0.0;

    for (int i = -1; i <= 1; ++i) {
        float fi = float(i);
        float id = cell + fi;
        float seed = hash11(id * 11.7 + 5.3);
        float center = fi + (seed - 0.5) * 0.34;
        float span = mix(0.42, 0.78, seed);
        float rise = mix(0.08, 0.22, seed);
        float y = 1.03 - uv.y;
        float dx = (fx - center) / span;
        float dy = (y - rise) / max(rise, 1.0e-4);
        float ring = abs(dot(vec2(dx, dy), vec2(dx, dy)) - 1.0);
        float arch = 1.0 - smoothstep(0.04, 0.22, ring);
        arch *= smoothstep(-0.2, 0.3, y);
        arch *= 1.0 - smoothstep(rise + 0.02, rise + 0.18, y);
        accum = max(accum, arch);
    }

    return sat(accum);
}

float canopy_mask(vec2 uv, vec2 cam_uv, float drift, float density, float parallax, float t) {
    vec2 far_uv = (uv + cam_uv * (0.18 + 0.18 * parallax)) * vec2(1.6, 0.85);
    far_uv += vec2(t * 0.005, -t * 0.004) * (0.5 + 0.5 * drift);
    float base = fbm(far_uv * vec2(1.0, 1.8));
    float detail = noise01(far_uv * vec2(2.2, 2.8) + vec2(3.1, -1.7));
    float coverage = smoothstep(0.42, 0.86, base * 0.74 + detail * 0.30 + density * 0.14);
    coverage *= smoothstep(0.02, 0.48, uv.y);
    return sat(coverage);
}

float canopy_openness_fast(vec2 uv, vec2 cam_uv, float drift, float density, float parallax, float t) {
    vec2 ray_uv = (uv + cam_uv * (0.12 + 0.14 * parallax)) * vec2(1.45, 0.78);
    ray_uv += vec2(t * 0.004, -t * 0.003) * (0.5 + 0.4 * drift);
    float a = noise01(ray_uv * vec2(1.2, 2.0));
    float b = noise01(ray_uv * vec2(2.0, 2.7) + vec2(2.3, -1.1));
    float coverage = smoothstep(0.48, 0.84, a * 0.68 + b * 0.32 + density * 0.12);
    coverage *= smoothstep(0.02, 0.46, uv.y);
    return 1.0 - sat(coverage);
}

vec4 spore_cloud_field(vec2 world_uv, float t, float drift_speed, float spore_density) {
    vec2 flock_grid = vec2(4.4, 1.9);
    vec2 p = world_uv * flock_grid;
    vec2 base = floor(p);
    vec2 f = fract(p);
    vec3 accum_col = vec3(0.0);
    float accum_a = 0.0;
    float density = clamp(spore_density / 1.7, 0.0, 1.5);

    for (int jy = -1; jy <= 1; ++jy) {
        for (int ix = -1; ix <= 1; ++ix) {
            vec2 cell = base + vec2(float(ix), float(jy));
            float flock_seed = hash11(cell.x * 13.17 + cell.y * 31.93 + 0.27);
            float flock_seed_b = hash11(cell.x * 23.71 + cell.y * 11.37 + 1.91);
            float flock_seed_c = hash11(cell.x * 41.11 + cell.y * 17.53 + 2.87);
            float flock_live = smoothstep(0.36, 0.92, flock_seed_c + density * 0.24);
            if (flock_live <= 0.0) {
                continue;
            }

            vec2 cell_local = f - vec2(float(ix), float(jy));
            float angle = flock_seed * 6.2831853 + 0.55 * sin(t * (0.22 + 0.08 * drift_speed) + flock_seed_b * 6.0);
            vec2 heading = vec2(cos(angle), sin(angle));
            float wind_angle = noise01(cell * 0.17 + vec2(t * 0.015, -t * 0.011)) * 6.2831853;
            vec2 wind_dir = vec2(cos(wind_angle), sin(wind_angle + flock_seed_b * 2.7));
            heading = normalize(mix(heading, wind_dir, 0.72));
            vec2 side = vec2(-heading.y, heading.x);
            vec2 gust = vec2(
                noise01(cell * 0.21 + vec2(t * 0.055, -t * 0.038)),
                noise01(cell * 0.17 + vec2(-t * 0.036, t * 0.046))
            ) - 0.5;
            vec2 world_flow = vec2(
                noise01(world_uv * vec2(0.52, 0.28) + vec2(t * 0.060, -t * 0.022)),
                noise01(world_uv * vec2(0.44, 0.34) + vec2(-t * 0.034, t * 0.048))
            ) - 0.5;
            vec2 flock_center = vec2(0.5) + (hash22(cell) - 0.5) * vec2(0.62, 0.40);
            flock_center += heading * sin(t * (0.24 + 0.18 * drift_speed) + flock_seed * 8.0) * 0.20;
            flock_center += side * cos(t * (0.38 + 0.14 * drift_speed) + flock_seed_b * 10.0) * 0.10;
            flock_center += gust * vec2(0.46, 0.28);
            flock_center += world_flow * vec2(0.55, 0.26);

            float stream_arch = dot(cell_local - flock_center, heading);
            float ribbon_arch = dot(cell_local - flock_center, side);
            float body_field = exp(-(stream_arch * stream_arch * 1.8 + ribbon_arch * ribbon_arch * 6.8));
            if (body_field <= 0.018) {
                continue;
            }

            for (int bi = 0; bi < 12; ++bi) {
                float boid_id = float(bi);
                float seed = hash11(cell.x * 17.13 + cell.y * 31.79 + boid_id * 7.31 + 0.31);
                float seed_b = hash11(cell.x * 29.71 + cell.y * 11.17 + boid_id * 5.17 + 1.77);
                float seed_c = hash11(cell.x * 41.17 + cell.y * 23.53 + boid_id * 3.91 + 3.91);
                float live_mask = step(0.24 - density * 0.05, seed_c) * flock_live;
                if (live_mask <= 0.0) {
                    continue;
                }

                float boid_phase = t * (0.62 + 0.26 * drift_speed + seed_b * 0.28) + boid_id * 1.37 + flock_seed * 9.0;
                float rank = (seed - 0.5) * 0.72;
                float spread = (seed_b - 0.5) * 0.24;
                vec2 boid_center = flock_center;
                boid_center += heading * (rank + sin(boid_phase) * 0.18 + sin(boid_phase * 0.43 + 1.2) * 0.10);
                boid_center += side * (spread + sin(boid_phase * 1.31 + seed_c * 9.0) * 0.11);
                boid_center += gust * (0.14 + 0.10 * seed_c);
                boid_center += world_flow * (0.16 + 0.10 * seed);

                vec2 local = cell_local - boid_center;
                float along = dot(local, heading);
                float across = dot(local, side);
                float point = exp(-(along * along + across * across) * mix(2600.0, 4800.0, seed));
                float wing = exp(-(along * along * 620.0 + across * across * 2400.0));
                float spark = exp(-(along * along * 4200.0 + across * across * 4200.0));
                float twinkle = 0.55 + 0.45 * sin(t * (3.5 + 2.1 * seed_b) + seed * 23.0);
                float mote = (point * 0.95 + wing * 0.05 + spark * 0.28) * (0.75 + 0.45 * twinkle) * live_mask * body_field;

                vec3 pastel_a = vec3(0.70, 0.98, 0.88);
                vec3 pastel_b = vec3(1.00, 0.80, 0.88);
                vec3 pastel_c = vec3(0.84, 0.82, 1.00);
                vec3 pastel_d = vec3(1.00, 0.95, 0.76);
                vec3 tint = mix(pastel_a, pastel_b, seed);
                tint = mix(tint, pastel_c, seed_b * 0.60);
                tint = mix(tint, pastel_d, flock_seed * 0.28);

                accum_col += tint * mote * (0.92 + 0.28 * density);
                accum_a += mote * (0.86 + 0.16 * density);
            }
        }
    }

    float shimmer = 0.84 + 0.16 * sin(t * 2.8 + p.x * 0.9 + p.y * 0.7);
    accum_col *= shimmer;
    accum_col = clamp(accum_col, vec3(0.0), vec3(1.85));
    accum_a = sat(accum_a);
    return vec4(accum_col, accum_a);
}

vec3 background_megatrunks(vec2 uv, vec2 cam_uv, float canopy_density, float parallax, float t) {
    vec2 warp_uv = (uv + cam_uv * (0.08 + 0.08 * parallax)) * vec2(0.92, 0.42);
    vec2 flow = vec2(
        noise01(warp_uv * vec2(0.8, 1.7) + vec2(0.3, -0.7)),
        noise01(warp_uv * vec2(1.2, 1.1) + vec2(2.1, 1.4))
    ) - 0.5;
    vec2 far_uv = warp_uv * vec2(1.4, 0.34) + flow * 0.24 + vec2(t * 0.0016, 0.0);
    vec2 mid_uv = warp_uv * vec2(1.9, 0.48) + flow * 0.34 + vec2(-1.7, 0.9);

    float edge_far = voronoi_edge(far_uv);
    float edge_mid = voronoi_edge(mid_uv);
    float cell_far = 1.0 - smoothstep(0.16, 0.42, edge_far);
    float cell_mid = 1.0 - smoothstep(0.12, 0.30, edge_mid);

    float trunk_vertical = smoothstep(0.04, 0.96, uv.y);
    float far_mass = cell_far * (0.40 + 0.60 * smoothstep(0.16, 0.82, noise01(far_uv * 0.7 + vec2(3.2, -1.4))));
    float mid_mass = cell_mid * (0.38 + 0.62 * smoothstep(0.24, 0.84, noise01(mid_uv * 0.9 + vec2(-2.1, 1.8))));
    float mass = max(far_mass * 0.75, mid_mass);
    mass *= trunk_vertical * (0.42 + 0.44 * sat(canopy_density * 0.24));

    float rim_far = smoothstep(0.0, 0.09, 0.24 - edge_far) * far_mass;
    float rim_mid = smoothstep(0.0, 0.08, 0.18 - edge_mid) * mid_mass;
    float texture = 0.60 + 0.40 * noise01(vec2(far_uv.x * 0.7, uv.y * 4.0) + vec2(1.7, -2.3));
    float rim = max(rim_far * 0.72, rim_mid * 0.92) * texture;
    float veins = smoothstep(0.72, 0.96, noise01(mid_uv * vec2(1.4, 3.7) + vec2(t * 0.004, -t * 0.003))) * mass;

    return vec3(sat(mass), sat(rim), sat(veins));
}

vec4 foreground_vines(vec2 uv, vec2 cam_uv, float t, float parallax, float density) {
    float vine0 = 0.0;
    float vine1 = 0.0;
    float vine2 = 0.0;
    float thorn = 0.0;
    float band_count = floor(mix(2.0, 4.0, sat(density * 0.28)));
    float x = (uv.x + cam_uv.x * (0.58 + 0.16 * parallax));

    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        float live = step(fi, band_count - 0.5);
        if (live <= 0.0) {
            continue;
        }

        float seed = hash11(fi * 15.31 + 0.41);
        float seed_b = hash11(fi * 27.17 + 1.27);
        float wave = x * (3.6 + 2.2 * seed_b);
        float center = 0.90 + fi * 0.050 + (seed - 0.5) * 0.024;
        center += sin(wave * 1.7 + t * (0.12 + 0.06 * seed) + seed * 6.0) * (0.020 + 0.018 * seed_b);
        center += sin(wave * 3.6 - t * (0.08 + 0.04 * seed_b) + seed_b * 8.0) * (0.008 + 0.010 * seed);
        float width = mix(0.030, 0.054, seed_b);
        float body = 1.0 - smoothstep(width * 0.70, width, abs(uv.y - center));
        body *= smoothstep(0.76, 0.93, uv.y) * live;
        if (i == 0) {
            vine0 = max(vine0, body);
        } else if (i == 1) {
            vine1 = max(vine1, body);
        } else {
            vine2 = max(vine2, body);
        }

        float seg = fract(wave * (1.6 + 0.8 * seed) + seed * 4.0);
        float tooth = smoothstep(0.84, 0.96, 1.0 - abs(seg * 2.0 - 1.0));
        float thorn_height = mix(0.004, 0.010, seed) * tooth;
        float thorn_center = center - thorn_height;
        float thorn_a = 1.0 - smoothstep(width * 0.12, width * 0.28, abs(uv.y - thorn_center));
        thorn_a *= tooth * body;
        thorn = max(thorn, thorn_a * (0.32 + 0.16 * seed_b));
    }

    return vec4(sat(vine0), sat(vine1), sat(vine2), sat(thorn));
}

void main() {
    vec2 frag_px = gl_FragCoord.xy;
    vec2 vp = vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec2 uv = frag_px / vp; /* 0=top, 1=bottom */
    float t = pc.p0.z;

    float spore_density = clamp(pc.p0.w, 0.0, 4.0);
    float spore_scale = max(pc.p1.w, 0.05);
    float drift_speed = max(pc.p3.z, 0.0);
    float canopy_density = clamp(pc.p3.w, 0.0, 4.0);
    float world_w = max(pc.p4.x, 1.0);
    float world_h = max(pc.p4.y, 1.0);
    float parallax = clamp(pc.p4.z, 0.0, 4.0);
    float flora_density = clamp(pc.p4.w, 0.0, 4.0);
    float membrane_glow = clamp(pc.p5.w, 0.0, 2.0);
    float root_arch_density = clamp(pc.p6.x, 0.0, 4.0);
    float godray_strength = clamp(pc.p6.y, 0.0, 2.0);
    float high_quality = step(0.5, pc.p6.z);
    float wobble_amp = clamp(pc.p7.x, 0.0, 4.0);
    float wobble_speed = clamp(pc.p7.y, 0.0, 6.0);
    float pulse_freq = clamp(pc.p7.z, 0.0, 6.0);
    float foreground_alpha = clamp(pc.p7.w, 0.0, 1.0);

    vec2 cam_uv = vec2(pc.p3.x / world_w, pc.p3.y / world_h);
    vec2 world_uv = (vec2(pc.p3.x, pc.p3.y) + frag_px) / vec2(world_w, world_h);

    vec3 bark_col = pc.p1.rgb;
    vec3 haze_col = pc.p2.rgb;
    vec3 glow_col = pc.p5.rgb;
    vec3 ray_col = vec3(0.80, 0.87, 0.74);

    vec3 col = mix(vec3(0.018, 0.020, 0.030), mix(haze_col, vec3(0.10, 0.18, 0.14), 0.35), smoothstep(0.0, 0.82, uv.y));
    col = mix(col, bark_col * 0.70 + vec3(0.01, 0.00, 0.02), smoothstep(0.80, 1.0, uv.y));

    vec2 haze_uv = world_uv * spore_scale * vec2(1.2, 0.9);
    haze_uv += vec2(t * 0.010, -t * 0.006) * (0.5 + drift_speed * 0.4);
    float haze0 = fbm(haze_uv * vec2(1.0, 1.6));
    float haze1 = fbm(haze_uv * vec2(2.1, 2.4) + vec2(1.7, -3.2));
    float haze = smoothstep(0.30, 0.92, haze0 * 0.68 + haze1 * 0.32);

    vec3 bg_trunks = background_megatrunks(uv, cam_uv, canopy_density, parallax, t);
    float canopy_far = canopy_mask(uv, cam_uv, drift_speed, canopy_density, parallax * 0.35, t);
    vec4 flora_tex = texture(u_kelp, uv);
    float flora_pre = clamp(flora_tex.a, 0.0, 1.0);
    float flora_far = max(canopy_far * 0.88, flora_pre * 0.16);
    float flora_mid = flora_pre;
    vec3 flora_rgb = flora_tex.rgb;
    float flora_lum = dot(flora_rgb, vec3(0.2126, 0.7152, 0.0722));
    flora_rgb = mix(vec3(flora_lum), flora_rgb, 1.24);
    flora_rgb *= 0.90;
    float flora_glow = sat((max(max(flora_rgb.r, flora_rgb.g), flora_rgb.b) - flora_pre * 0.24) * 1.6);

    float pulse = 0.55 + 0.45 * sin(t * (0.35 + pulse_freq * 0.55) + haze1 * 9.0);
    vec4 cloud = vec4(0.0);

    float rim = smoothstep(0.16, 0.86, flora_mid) * (1.0 - smoothstep(0.90, 1.0, flora_mid));
    float translucency = rim * membrane_glow * (0.75 + 0.65 * pulse) * smoothstep(0.10, 0.78, uv.y);
    float hot_rim = pow(rim, 0.72) * membrane_glow * (0.55 + 0.75 * pulse);

    vec3 far_col = mix(bark_col * 0.70, vec3(0.16, 0.28, 0.24), haze * 0.28);
    vec3 mid_col = mix(bark_col * 0.58, haze_col * 0.95, haze * 0.30);
    vec3 trunk_shadow = vec3(0.01, 0.015, 0.025);
    vec3 trunk_rim = mix(vec3(0.28, 0.42, 0.36), glow_col * 0.82, 0.55);
    vec3 cloud_col = vec3(0.0);
    col = mix(col, far_col, flora_far * 0.58);
    col = mix(col, mid_col, flora_mid * 0.94);
    col = mix(col, trunk_shadow, bg_trunks.x * 0.82);
    col += trunk_rim * bg_trunks.y * 0.84;
    col += mix(glow_col, vec3(0.78, 0.90, 0.86), 0.40) * bg_trunks.z * 0.40;
    col = mix(col, flora_rgb, flora_pre * 0.86);
    col *= (1.0 - flora_pre * 0.010);
    col += flora_rgb * flora_glow * 0.24;
    col += glow_col * translucency * 0.52;
    col += mix(glow_col, flora_rgb, 0.46) * hot_rim * 0.20;

    float ray = 0.0;
    vec2 ray_dir = normalize(vec2(-0.48, 0.88));
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        vec2 suv = uv + ray_dir * fi * 0.042;
        float weight = (high_quality > 0.5 || i < 3) ? 1.0 : 0.0;
        float openness = canopy_openness_fast(suv, cam_uv, drift_speed, canopy_density, parallax * 0.25, t);
        ray += openness * weight;
    }
    ray /= (high_quality > 0.5) ? 4.0 : 3.0;
    float godray = ray * haze * godray_strength * smoothstep(0.06, 0.72, uv.y) * (1.0 - flora_mid * 0.55);
    col += ray_col * godray * 0.25;

    float spores = 0.0;

    float dust = smoothstep(0.44, 0.92, haze0 * 0.64 + haze1 * 0.36);
    col = mix(col, mix(bark_col * 0.72, haze_col, 0.65), dust * pc.p2.w * 0.32);

    vec4 vines = foreground_vines(uv, cam_uv, t, parallax, root_arch_density + flora_density * 0.5);
    float near_occ = flora_pre * (0.18 + 0.14 * smoothstep(0.28, 1.0, uv.y));
    if (high_quality > 0.5) {
        near_occ = max(near_occ, trunk_band(uv, cam_uv.x, 0.60 + 0.24 * parallax, flora_density * 1.2, wobble_amp * 0.70, wobble_speed * 1.10, t + 7.9) * 0.65);
    }
    float occ_alpha = near_occ * foreground_alpha * smoothstep(0.12, 1.0, uv.y);
    col *= (1.0 - occ_alpha * 0.58);
    float vine_mask0 = sat(vines.x * foreground_alpha * 1.18);
    float vine_mask1 = sat(vines.y * foreground_alpha * 1.18);
    float vine_mask2 = sat(vines.z * foreground_alpha * 1.18);
    float thorn_mask = sat(vines.w * foreground_alpha * 1.18);
    vec3 vine_col0 = bark_col * 0.32 + haze_col * 0.20 + vec3(0.05, 0.02, 0.06);
    vec3 vine_col1 = bark_col * 0.28 + glow_col * 0.10 + vec3(0.03, 0.05, 0.02);
    vec3 vine_col2 = bark_col * 0.24 + haze_col * 0.12 + glow_col * 0.08 + vec3(0.04, 0.02, 0.00);
    vec3 thorn_col = bark_col * 0.18 + haze_col * 0.06 + vec3(0.03, 0.01, 0.03);
    col = mix(col, vine_col0, smoothstep(0.06, 0.20, vine_mask0));
    col = mix(col, vine_col1, smoothstep(0.06, 0.20, vine_mask1));
    col = mix(col, vine_col2, smoothstep(0.06, 0.20, vine_mask2));
    col = mix(col, thorn_col, smoothstep(0.04, 0.12, thorn_mask));

    float luminance = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luminance), col, 1.20);
    col = col / (vec3(1.0) + col * 0.28);
    col = clamp((col - 0.5) * 1.14 + 0.5, 0.0, 1.0);

    float alpha = clamp(pc.p2.w * (0.52 + haze * 0.18 + flora_far * 0.16 + flora_mid * 0.40 + bg_trunks.x * 0.34), 0.0, 0.98);
    alpha = max(alpha, 0.72 + bg_trunks.x * 0.12);
    alpha = max(alpha, occ_alpha * 0.88);
    alpha = max(alpha, max(max(vine_mask0, vine_mask1), vine_mask2) * 0.92);
    out_color = vec4(col, alpha);
}
