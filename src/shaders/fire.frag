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

vec2 flow_field(vec2 uv, float t, float high_quality) {
    vec4 a = texture(u_noise, uv + vec2(t * 0.017, -t * 0.021));
    vec4 b = texture(u_noise, uv * 1.73 + vec2(-t * 0.012, t * 0.016) + vec2(0.37, 0.11));
    vec2 flow = vec2(a.a - a.r, b.g - b.b);
    if (high_quality > 0.5) {
        vec4 c = texture(u_noise, uv * 2.27 + vec2(t * 0.011, -t * 0.015) + vec2(-0.21, 0.47));
        flow += vec2(c.b - c.r, c.a - c.g) * 0.55;
    }
    return flow * 1.45;
}

float field_two_octave(vec2 uv, vec2 flow, vec2 drift) {
    float n0 = noise_pack(uv + flow * 0.20 + drift);
    float n1 = noise_pack(uv * 2.05 - flow * 0.12 + drift * vec2(-1.3, 1.2) + vec2(0.37, -0.61));
    return sat(n0 * 0.66 + n1 * 0.34);
}

vec3 lava_ramp(float t) {
    vec3 crust = pc.p1.rgb;
    vec3 mid = pc.p2.rgb;
    vec3 hot = pc.p5.rgb;
    vec3 white_hot = pc.p6.rgb;
    vec3 col = mix(crust, mid, smoothstep(0.10, 0.44, t));
    col = mix(col, hot, smoothstep(0.36, 0.80, t));
    col = mix(col, white_hot, smoothstep(0.76, 1.00, t));
    return col;
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    vec2 vp = vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec2 frag_uv = frag_px / vp; /* 0=top, 1=bottom */
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

    vec2 base_uv = world_uv * magma_scale * vec2(2.15, 1.42);
    vec2 base_flow = flow_field(base_uv, t * (0.65 + 0.55 * rise_speed), high_quality);
    vec2 warped_uv = base_uv + base_flow * warp_amp * 0.76;

    float n0 = field_two_octave(warped_uv, base_flow, vec2(t * 0.018, -t * 0.014) * (0.7 + 0.5 * rise_speed));
    float n1 = field_two_octave(warped_uv * 1.72 + vec2(1.2, -0.8), base_flow * 1.12, vec2(-t * 0.011, t * 0.015));
    float ridges = 1.0 - abs(n1 * 2.0 - 1.0);
    float crack_seed = texture(u_noise, warped_uv * 2.6 + base_flow * 0.10).a;
    float cracks = smoothstep(0.56, 0.90, ridges * 0.72 + crack_seed * 0.44);
    float lava = smoothstep(0.36, 0.84, n0 + ridges * 0.20);
    float seams = lava * (1.0 - cracks * 0.84);
    float pulse = 0.78 + 0.22 * sin(t * pulse_freq + n1 * 7.9);
    float emissive = seams * pulse;

    float floor_band = exp(-y_from_bottom / max(world_h * (0.10 + 0.14 * plume_h), 1.0));
    vec2 flame_uv = vec2(
        world_uv.x * (1.58 + magma_scale * 0.18),
        y_from_bottom / max(world_h * (0.20 + 0.12 * plume_h), 1.0)
    );
    flame_uv.y -= t * (0.34 + 0.54 * rise_speed);
    vec2 flame_flow = flow_field(flame_uv * 1.24 + vec2(0.21, -0.14), t * 1.06, high_quality);
    float tongues0 = noise_pack(flame_uv + flame_flow * 0.34 + vec2(t * 0.029, -t * 0.021));
    float tongues1 = noise_pack(flame_uv * 2.04 - flame_flow * 0.18 + vec2(-t * 0.019, t * 0.016) + vec2(0.71, -1.27));
    float pocket = noise_pack(vec2(world_uv.x * 2.5 + t * 0.04, world_uv.y * 0.7));
    float flame = smoothstep(0.42, 0.89, tongues0 * 0.70 + tongues1 * 0.46 + pocket * 0.26 - 0.16);
    flame *= floor_band * (0.58 + 0.42 * smoothstep(0.35, 0.85, pocket));

    float smoke0 = smoothstep(0.58, 0.94, noise_pack(world_uv * vec2(0.62, 0.42) + vec2(t * 0.008, -t * 0.003)));
    float smoke1 = smoothstep(0.54, 0.90, noise_pack(world_uv * vec2(1.02, 0.70) + vec2(-t * 0.011, t * 0.006) + vec2(1.7, 3.1)));
    float smoke2 = smoothstep(0.50, 0.86, noise_pack(world_uv * vec2(1.55, 1.05) + vec2(t * 0.015, t * 0.010) + vec2(4.2, -2.8)));
    float smoke = (smoke0 * 0.16 + smoke1 * 0.20 + smoke2 * 0.22 * high_quality) * (0.56 + 0.44 * (1.0 - floor_band));
    smoke = min(smoke, smoke_cap);

    float ember_density = clamp(pc.p5.w, 0.0, 3.5);
    vec2 ember_uv = world_uv * vec2(24.0, 12.0) + base_flow * 0.62 + vec2(-t * 0.18, t * (0.40 + 0.40 * rise_speed));
    vec4 ember_s = texture(u_noise, ember_uv);
    float ember_seed = ember_s.r * 0.58 + ember_s.g * 0.27 + ember_s.b * 0.15;
    float ember_twinkle = 0.55 + 0.45 * sin(t * (10.0 + ember_s.a * 6.5) + ember_seed * 31.0);
    float embers = smoothstep(0.92, 0.995, ember_seed + floor_band * 0.40 + emissive * 0.18 - smoke * 0.23);
    embers *= clamp(ember_density / 1.9, 0.0, 1.0) * ember_twinkle;

    vec3 col = lava_ramp(sat(0.14 + seams * 0.88));
    col *= 0.52 + 0.58 * seams;
    col += lava_ramp(sat(0.54 + emissive * 0.62)) * (0.24 + emissive * 0.82);
    col += lava_ramp(sat(0.72 + flame * 0.36)) * flame * 1.24;

    vec3 smoke_col = mix(vec3(0.16, 0.08, 0.05), vec3(0.34, 0.15, 0.08), smoke0);
    col = mix(col, smoke_col, smoke * 0.28);
    col += vec3(1.0, 0.76, 0.36) * embers * 0.70;

    float glow = clamp(emissive * 1.08 + flame * 1.20, 0.0, 1.6);
    col += pc.p6.rgb * pow(clamp(glow, 0.0, 1.4), 2.0) * 0.52;
    col += pc.p5.rgb * pow(clamp(flame, 0.0, 1.0), 1.4) * 0.22;
    col = col / (vec3(1.0) + col * 0.42);
    col = clamp((col - 0.5) * 1.22 + 0.5, 0.0, 1.0);

    /* Fade to black from the top edge through the top 20% of the screen. */
    float top_fade = smoothstep(0.0, 0.20, frag_uv.y);
    col *= top_fade;

    float alpha = clamp(0.70 + glow * 0.16 + smoke * 0.05, 0.0, 0.98);
    out_color = vec4(col, alpha);
}
