#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform sampler2D u_noise;

layout(push_constant) uniform UnderwaterPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=voronoi_scale */
    vec4 p1; /* rgb=ice_dark, w=crack_width */
    vec4 p2; /* rgb=ice_bright, w=alpha */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=distort_amp, w=parallax */
    vec4 p4; /* x=snow_density_hint, y=shimmer, z=world_w, w=world_h */
    vec4 p5; /* x=snow_angle_rad, y=snow_speed */
    vec4 p6;
    vec4 p7;
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
        uv = uv * 2.03 + vec2(0.17, -0.29);
        a *= 0.5;
    }
    return v;
}

float ridge(vec2 uv) {
    float n = noise01(uv);
    return 1.0 - abs(n * 2.0 - 1.0);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

/* Voronoi edge metric (d2-d1): thin values near cell borders. */
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

vec2 layer_uv(vec2 frag_uv, vec2 cam_uv, float camera_factor, vec2 scale, float time_mul) {
    vec2 uv = (frag_uv + cam_uv * camera_factor) * scale;
    uv += vec2(pc.p0.z * 0.005 * time_mul, 0.0);
    return uv;
}

void main() {
    vec2 frag_px = gl_FragCoord.xy;
    vec2 vp = vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec2 frag_uv = frag_px / vp;
    float t = pc.p0.z;

    float world_w = max(pc.p4.z, 1.0);
    float world_h = max(pc.p4.w, 1.0);
    vec2 cam_uv = vec2(pc.p3.x / world_w, pc.p3.y / world_h);

    float vor_scale = max(pc.p0.w, 0.25);
    float crack_w = clamp(pc.p1.w, 0.02, 0.9);
    float warp_amp = clamp(pc.p3.z, 0.0, 2.0);
    float parallax = clamp(pc.p3.w, 0.0, 4.0);
    float shimmer = clamp(pc.p4.y, 0.0, 2.0);

    float para01 = sat(parallax * 0.30);
    float far_cf = mix(0.26, 0.52, para01);
    float mid_cf = mix(0.56, 0.86, para01);
    float near_cf = mix(0.92, 1.28, para01);

    vec2 uv_far = layer_uv(frag_uv, cam_uv, far_cf, vec2(1.8, 1.0), 0.6);
    vec2 uv_mid = layer_uv(frag_uv, cam_uv, mid_cf, vec2(2.6, 1.1), 1.0);
    vec2 uv_near = layer_uv(frag_uv, cam_uv, near_cf, vec2(3.8, 1.2), 1.4);

    float y = frag_uv.y; /* 0=top, 1=bottom */

    float far_top_fade = smoothstep(0.08, 0.92, y);
    float mid_top_fade = smoothstep(0.09, 0.94, y);
    float near_top_fade = smoothstep(0.10, 0.95, y);

    vec3 sky_top = vec3(0.005, 0.020, 0.035);
    vec3 sky_mid = vec3(0.020, 0.080, 0.130);
    vec3 sky_low = vec3(0.060, 0.180, 0.260);
    vec3 col = mix(sky_top, sky_mid, smoothstep(0.02, 0.45, y));
    col = mix(col, sky_low, smoothstep(0.40, 1.0, y));

    /* Keep sky noise low-frequency and non-animated to avoid top-band shimmer/flicker. */
    float haze0 = noise01(frag_uv * vec2(0.52, 0.18) + cam_uv * vec2(0.10, 0.05));
    float haze1 = noise01(frag_uv * vec2(0.94, 0.26) + cam_uv * vec2(0.16, 0.08) + vec2(3.7, -1.9));
    float haze = haze0 * 0.64 + haze1 * 0.36;
    col += vec3(0.035, 0.070, 0.095) * smoothstep(0.54, 0.92, haze) * (0.18 + 0.24 * para01);

    vec3 far_col = vec3(0.10, 0.22, 0.30);
    vec3 mid_col = max(pc.p1.rgb, vec3(0.0));
    vec3 near_col = max(pc.p2.rgb, vec3(0.0));

    float far_tex = fbm(uv_far * vec2(1.0, 2.2));
    float mid_tex = fbm(uv_mid * vec2(1.2, 2.5));
    float near_tex = fbm(uv_near * vec2(1.5, 3.1));

    vec3 far_ice = far_col * (0.70 + far_tex * 0.40);
    vec3 mid_ice = mix(mid_col * 0.72, near_col * 0.70, mid_tex * 0.65);
    vec3 near_ice = mix(mid_col * 0.50, near_col, near_tex * 0.78);

    /* Full-screen layered texture blend with a top-fade on the front-most layer. */
    float far_alpha = (0.56 + 0.14 * para01) * far_top_fade;
    float mid_alpha = (0.48 + 0.16 * para01) * mid_top_fade;
    float near_alpha = (0.08 + 0.78 * near_top_fade) * (0.86 + 0.14 * para01);
    near_alpha *= 0.86 + 0.14 * fbm(vec2(uv_near.x * 0.65, y * 1.25));
    near_alpha = clamp(near_alpha, 0.0, 1.0);

    col = mix(col, far_ice, clamp(far_alpha, 0.0, 1.0));
    col = mix(col, mid_ice, clamp(mid_alpha, 0.0, 1.0));
    col = mix(col, near_ice, near_alpha);

    vec2 crack_flow = vec2(
        noise01(uv_mid * vec2(1.7, 1.1) + vec2(0.13, -0.27)) - 0.5,
        noise01(uv_mid * vec2(1.3, 1.5) + vec2(-0.31, 0.29)) - 0.5
    );
    float crack_scale = vor_scale * 0.72;
    /* Mid layer: higher crack frequency. Front layer: lower crack frequency. */
    vec2 crack_uv_mid = uv_mid * vec2(3.8, 2.1) * crack_scale + crack_flow * warp_amp * 1.1;
    vec2 crack_uv_near = uv_near * vec2(2.6, 1.6) * crack_scale + crack_flow * warp_amp * 1.6;

    float edge_mid = voronoi_edge(crack_uv_mid);
    float edge_near = voronoi_edge(crack_uv_near + vec2(2.7, -1.9));
    float crack_mid = 1.0 - smoothstep(crack_w * 0.50, crack_w * 1.45, edge_mid);
    float crack_near = 1.0 - smoothstep(crack_w * 0.45, crack_w * 1.7, edge_near);

    float crack_mask_mid = 0.28 + 0.58 * mid_alpha;
    float crack_mask_near = 0.12 + 0.88 * near_top_fade;

    vec3 crack_dark = vec3(0.05, 0.12, 0.18);
    vec3 crack_glow = vec3(0.34, 0.70, 0.86);
    col = mix(col, crack_dark, crack_mid * crack_mask_mid * 0.78);
    col = mix(col, crack_dark * 0.9, crack_near * crack_mask_near * 0.88);

    float rim_mid = smoothstep(0.0, crack_w * 0.70, crack_w * 1.45 - edge_mid) * crack_mask_mid;
    float rim_near = smoothstep(0.0, crack_w * 0.85, crack_w * 1.7 - edge_near) * crack_mask_near;
    col += crack_glow * (rim_mid * 0.18 + rim_near * 0.28) * (0.6 + 0.4 * shimmer);

    float sparkle_seed = noise01(crack_uv_near * 3.8 + vec2(t * 0.07, -t * 0.05));
    float tw = 0.5 + 0.5 * sin(t * (8.0 + sparkle_seed * 11.0) + sparkle_seed * 37.0);
    float sparkle = smoothstep(0.975, 1.0, sparkle_seed + crack_near * 0.18) * tw * crack_mask_near;
    col += vec3(0.85, 0.96, 1.0) * sparkle * (0.28 + 0.32 * shimmer);

    /* Increase scene dynamic range: deeper darks with stronger ice highlights. */
    col = col / (vec3(1.0) + col * 0.14);
    col = clamp((col - 0.5) * 1.30 + 0.5, 0.0, 1.0);

    float alpha = clamp(pc.p2.w, 0.0, 1.0);
    out_color = vec4(col, alpha);
}
