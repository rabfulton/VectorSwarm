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

float canopy_mask(vec2 uv, vec2 cam_uv, float drift, float density, float parallax, float t) {
    vec2 far_uv = (uv + cam_uv * (0.18 + 0.18 * parallax)) * vec2(1.6, 0.85);
    far_uv += vec2(t * 0.005, -t * 0.004) * (0.5 + 0.5 * drift);
    float base = fbm(far_uv * vec2(1.0, 1.8));
    float detail = noise01(far_uv * vec2(2.2, 2.8) + vec2(3.1, -1.7));
    float coverage = smoothstep(0.42, 0.86, base * 0.74 + detail * 0.30 + density * 0.14);
    coverage *= smoothstep(0.02, 0.48, uv.y);
    return sat(coverage);
}

void main() {
    vec2 frag_px = gl_FragCoord.xy;
    vec2 vp = vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec2 uv = frag_px / vp;
    float t = pc.p0.z;

    float drift_speed = max(pc.p3.z, 0.0);
    float canopy_density = clamp(pc.p3.w, 0.0, 4.0);
    float world_w = max(pc.p4.x, 1.0);
    float world_h = max(pc.p4.y, 1.0);
    float parallax = clamp(pc.p4.z, 0.0, 4.0);
    vec2 cam_uv = vec2(pc.p3.x / world_w, pc.p3.y / world_h);

    vec3 trunks = background_megatrunks(uv, cam_uv, canopy_density, parallax, t);
    float canopy = canopy_mask(uv, cam_uv, drift_speed, canopy_density, parallax * 0.35, t);
    out_color = vec4(trunks, canopy);
}
