#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

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
    for (int i = 0; i < 4; ++i) {
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
        float cap = 1.0 - smoothstep(0.78, 1.0, length(vec2(cap_dx, cap_dy)));
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
        float ring = abs(length(vec2(dx, dy)) - 1.0);
        float arch = 1.0 - smoothstep(0.02, 0.08, ring);
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
    float detail = fbm(far_uv * vec2(2.2, 2.8) + vec2(3.1, -1.7));
    float coverage = smoothstep(0.42, 0.86, base * 0.72 + detail * 0.42 + density * 0.14);
    coverage *= smoothstep(0.02, 0.48, uv.y);
    return sat(coverage);
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

    vec3 col = mix(vec3(0.03, 0.035, 0.025), haze_col * 0.95, smoothstep(0.0, 0.82, uv.y));
    col = mix(col, bark_col * 0.55, smoothstep(0.82, 1.0, uv.y));

    vec2 haze_uv = world_uv * spore_scale * vec2(1.2, 0.9);
    haze_uv += vec2(t * 0.010, -t * 0.006) * (0.5 + drift_speed * 0.4);
    float haze0 = fbm(haze_uv * vec2(1.0, 1.6));
    float haze1 = fbm(haze_uv * vec2(2.1, 2.4) + vec2(1.7, -3.2));
    float haze = smoothstep(0.30, 0.92, haze0 * 0.68 + haze1 * 0.32);

    float canopy_far = canopy_mask(uv, cam_uv, drift_speed, canopy_density, parallax * 0.35, t);
    float trunks_far = trunk_band(uv, cam_uv.x, 0.10 + 0.08 * parallax, flora_density * 0.65, wobble_amp * 0.30, wobble_speed * 0.65, t);
    float trunks_mid = trunk_band(uv, cam_uv.x, 0.28 + 0.14 * parallax, flora_density, wobble_amp * 0.45, wobble_speed * 0.85, t + 3.7);
    float roots = root_arch_band(uv, cam_uv.x, 0.34 + 0.16 * parallax, root_arch_density, t);

    float flora_far = max(canopy_far * 0.88, trunks_far * 0.72);
    float flora_mid = max(trunks_mid, roots * 0.82);

    float pulse = 0.55 + 0.45 * sin(t * (0.35 + pulse_freq * 0.55) + haze1 * 9.0);

    float rim = smoothstep(0.12, 0.88, flora_mid) * (1.0 - smoothstep(0.90, 1.0, flora_mid));
    float translucency = rim * membrane_glow * pulse * smoothstep(0.12, 0.74, uv.y);

    vec3 far_col = bark_col * (0.55 + haze * 0.25);
    vec3 mid_col = mix(bark_col * 0.72, haze_col * 0.75, haze * 0.20);
    col = mix(col, far_col, flora_far * 0.56);
    col = mix(col, mid_col, flora_mid * 0.84);
    col += glow_col * translucency * 0.58;

    float ray = 0.0;
    vec2 ray_dir = normalize(vec2(-0.48, 0.88));
    for (int i = 0; i < 7; ++i) {
        float fi = float(i);
        vec2 suv = uv + ray_dir * fi * 0.042;
        float openness = 1.0 - canopy_mask(suv, cam_uv, drift_speed, canopy_density, parallax * 0.25, t);
        ray += openness;
    }
    ray /= 7.0;
    float godray = ray * haze * godray_strength * smoothstep(0.06, 0.72, uv.y) * (1.0 - flora_mid * 0.55);
    col += ray_col * godray * 0.25;

    vec2 spore_uv = world_uv * spore_scale * vec2(16.0, 9.0);
    spore_uv += vec2(-t * 0.060, t * 0.035) * (0.4 + drift_speed * 0.5);
    float spores_a = noise01(spore_uv + vec2(0.3, -0.8));
    float spores_b = noise01(spore_uv * 1.9 + vec2(-4.1, 2.6));
    float spores_c = noise01(spore_uv * 0.65 + vec2(t * 0.015, -t * 0.011));
    float spores = smoothstep(0.90, 0.995, spores_a * 0.60 + spores_b * 0.28 + spores_c * 0.22 + haze * 0.10);
    spores *= clamp(spore_density / 1.6, 0.0, 1.0);
    col += mix(vec3(0.85, 0.83, 0.72), glow_col, 0.35) * spores * (0.25 + 0.35 * pulse);

    float dust = smoothstep(0.44, 0.92, haze0 * 0.64 + haze1 * 0.36);
    col = mix(col, mix(bark_col * 0.72, haze_col, 0.65), dust * pc.p2.w * 0.32);

    float near_occ = trunk_band(uv, cam_uv.x, 0.60 + 0.24 * parallax, flora_density * 1.2, wobble_amp * 0.70, wobble_speed * 1.10, t + 7.9);
    near_occ = max(near_occ, root_arch_band(uv, cam_uv.x, 0.68 + 0.24 * parallax, root_arch_density * 1.25, t + 2.1));
    float occ_alpha = near_occ * foreground_alpha * smoothstep(0.16, 1.0, uv.y);
    col *= (1.0 - occ_alpha * 0.82);

    col += glow_col * spores * 0.10;
    col = col / (vec3(1.0) + col * 0.28);
    col = clamp((col - 0.5) * 1.14 + 0.5, 0.0, 1.0);

    float alpha = clamp(pc.p2.w * (0.26 + haze * 0.34 + flora_far * 0.18 + flora_mid * 0.28 + spores * 0.16), 0.0, 0.92);
    alpha = max(alpha, occ_alpha * 0.55);
    out_color = vec4(col, alpha);
}
