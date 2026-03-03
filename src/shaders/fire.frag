#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform sampler2D u_noise;

layout(push_constant) uniform FirePC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=magma_scale */
    vec4 p1; /* rgb=crust_color, w=warp_amp */
    vec4 p2; /* rgb=mid_lava_color, w=pulse_freq */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=plume_height, w=rise_speed */
    vec4 p4; /* x=distortion_amp, y=smoke_alpha_cap, z=world_w, w=world_h */
    vec4 p5; /* rgb=hot_lava_color, w=ember_density */
    vec4 p6; /* rgb=white_hot_color, w=high_quality */
    vec4 p7; /* x=setpiece_phase, y=setpiece_mix */
} pc;

float sat(float x) {
    return clamp(x, 0.0, 1.0);
}

float noise_pack(vec2 uv) {
    vec4 n = texture(u_noise, uv);
    return dot(n.rgb, vec3(0.52, 0.31, 0.17));
}

vec2 flow_field(vec2 uv, float t) {
    vec4 a = texture(u_noise, uv + vec2(t * 0.019, -t * 0.023));
    vec4 b = texture(u_noise, uv * 1.73 + vec2(-t * 0.013, t * 0.017) + vec2(0.37, 0.11));
    float cx = texture(u_noise, uv + vec2(0.031, -0.017)).a - texture(u_noise, uv - vec2(0.029, -0.013)).a;
    float cy = texture(u_noise, uv + vec2(0.023, 0.021)).r - texture(u_noise, uv - vec2(0.021, 0.019)).r;
    vec2 base = vec2(a.a - a.r, b.g - b.b);
    vec2 curl = vec2(cx, cy);
    return base * 1.4 + curl * 1.7;
}

float advected_field(vec2 uv, float t, float rise_speed) {
    vec2 f = flow_field(uv * 0.73, t * (0.45 + 0.55 * rise_speed));
    float n0 = noise_pack(uv + f * 0.19 + vec2(t * 0.023, -t * 0.017) * rise_speed);
    float n1 = noise_pack(uv * 1.91 - f * 0.15 + vec2(-t * 0.018, t * 0.014) * (0.6 + 0.4 * rise_speed) + vec2(0.37, -0.61));
    return sat(n0 * 0.62 + n1 * 0.38);
}

vec3 lava_ramp(float t) {
    vec3 crust = pc.p1.rgb;
    vec3 mid = pc.p2.rgb;
    vec3 hot = pc.p5.rgb;
    vec3 white_hot = pc.p6.rgb;
    vec3 col = mix(crust, mid, smoothstep(0.08, 0.42, t));
    col = mix(col, hot, smoothstep(0.34, 0.78, t));
    col = mix(col, white_hot, smoothstep(0.74, 1.00, t));
    return col;
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    float t = pc.p0.z;
    float magma_scale = max(pc.p0.w, 0.05);
    float warp_amp = clamp(pc.p1.w, 0.0, 1.0);
    float pulse_freq = max(pc.p2.w, 0.01);
    float smoke_cap = clamp(pc.p4.y, 0.0, 1.0);
    float world_w = max(pc.p4.z, 1.0);
    float world_h = max(pc.p4.w, 1.0);
    float plume_h = max(pc.p3.z, 0.1);
    float rise_speed = max(pc.p3.w, 0.0);
    float high_quality = step(0.5, pc.p6.w);

    vec2 world_px = vec2(pc.p3.x, pc.p3.y) + frag_px;
    vec2 world_uv = world_px / vec2(world_w, world_h);
    float y_from_bottom = world_h - world_px.y;

    vec2 base_uv = world_uv * magma_scale * vec2(2.2, 1.45);
    vec2 flow = flow_field(base_uv, t * (0.7 + 0.6 * rise_speed));
    vec2 warped_uv = base_uv + flow * warp_amp * 0.75;

    float n0 = advected_field(warped_uv, t, rise_speed + 0.3);
    float n1 = advected_field(warped_uv * 2.35 + vec2(1.2, -0.8), t * 1.33, rise_speed + 0.9);
    float ridges = 1.0 - abs(n1 * 2.0 - 1.0);
    float crack_seed = texture(u_noise, warped_uv * 3.1 + flow * 0.14).a;
    float cracks = smoothstep(0.54, 0.90, ridges * 0.74 + crack_seed * 0.46);
    float lava = smoothstep(0.34, 0.84, n0 + ridges * 0.22);
    float seams = lava * (1.0 - cracks * 0.84);
    float pulse = 0.78 + 0.22 * sin(t * pulse_freq + n1 * 8.6);
    float emissive = seams * pulse;

    float floor_band = exp(-y_from_bottom / max(world_h * (0.10 + 0.15 * plume_h), 1.0));
    vec2 flame_uv = vec2(
        world_uv.x * (1.6 + magma_scale * 0.2),
        y_from_bottom / max(world_h * (0.20 + 0.12 * plume_h), 1.0)
    );
    flame_uv.y -= t * (0.36 + 0.56 * rise_speed);
    vec2 flame_flow = flow_field(flame_uv * 1.31 + vec2(0.21, -0.14), t * 1.24);
    float tongues0 = advected_field(flame_uv + flame_flow * 0.44, t * 1.17, rise_speed + 0.7);
    float tongues1 = advected_field(flame_uv * 2.2 - flame_flow * 0.28 + vec2(0.71, -1.27), t * 1.58, rise_speed + 1.1);
    float pocket = noise_pack(vec2(world_uv.x * 2.7 + t * 0.04, world_uv.y * 0.7));
    float flame = smoothstep(0.43, 0.90, tongues0 * 0.72 + tongues1 * 0.48 + pocket * 0.26 - 0.18);
    flame *= floor_band * (0.58 + 0.42 * smoothstep(0.35, 0.85, pocket));

    float smoke0 = smoothstep(0.56, 0.94, advected_field(world_uv * vec2(0.62, 0.42) + vec2(t * 0.008, -t * 0.003), t * 0.22, 0.25));
    float smoke1 = smoothstep(0.52, 0.90, advected_field(world_uv * vec2(1.02, 0.70) + vec2(-t * 0.011, t * 0.006) + vec2(1.7, 3.1), t * 0.41, 0.40));
    float smoke2 = smoothstep(0.48, 0.86, advected_field(world_uv * vec2(1.55, 1.05) + vec2(t * 0.015, t * 0.010) + vec2(4.2, -2.8), t * 0.67, 0.58));
    float smoke = (smoke0 * 0.15 + smoke1 * 0.20 + smoke2 * 0.24 * high_quality) * (0.55 + 0.45 * (1.0 - floor_band));
    smoke = min(smoke, smoke_cap);

    float ember_density = clamp(pc.p5.w, 0.0, 3.5);
    vec2 ember_uv = world_uv * vec2(24.0, 12.0) + flow_field(world_uv * 3.1, t) * 0.65 + vec2(-t * 0.19, t * (0.42 + 0.44 * rise_speed));
    vec4 ember_s = texture(u_noise, ember_uv);
    float ember_seed = ember_s.r * 0.58 + ember_s.g * 0.27 + ember_s.b * 0.15;
    float ember_twinkle = 0.55 + 0.45 * sin(t * (11.0 + ember_s.a * 7.0) + ember_seed * 31.0);
    float embers = smoothstep(0.92, 0.995, ember_seed + floor_band * 0.42 + emissive * 0.18 - smoke * 0.25);
    embers *= clamp(ember_density / 1.9, 0.0, 1.0) * ember_twinkle;

    vec3 col = lava_ramp(sat(0.14 + seams * 0.88));
    col *= 0.52 + 0.58 * seams;
    col += lava_ramp(sat(0.54 + emissive * 0.62)) * (0.24 + emissive * 0.84);
    col += lava_ramp(sat(0.72 + flame * 0.36)) * flame * 1.28;

    vec3 smoke_col = mix(vec3(0.16, 0.08, 0.05), vec3(0.34, 0.15, 0.08), smoke0);
    col = mix(col, smoke_col, smoke * 0.28);
    col += vec3(1.0, 0.76, 0.36) * embers * 0.72;

    float glow = clamp(emissive * 1.1 + flame * 1.25, 0.0, 1.6);
    col += pc.p6.rgb * pow(clamp(glow, 0.0, 1.4), 2.0) * 0.56;
    col += pc.p5.rgb * pow(clamp(flame, 0.0, 1.0), 1.4) * 0.24;
    col = col / (vec3(1.0) + col * 0.42);
    col = clamp((col - 0.5) * 1.24 + 0.5, 0.0, 1.0);

    float alpha = clamp(0.70 + glow * 0.16 + smoke * 0.05, 0.0, 0.98);
    out_color = vec4(col, alpha);
}
