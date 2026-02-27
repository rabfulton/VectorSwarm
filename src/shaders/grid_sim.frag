#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_state; /* xy=disp, zw=vel */

layout(set = 0, binding = 0) uniform sampler2D u_prev_state;

layout(push_constant) uniform GridSimPC {
    vec4 p0;      /* x=state_w, y=state_h, z=dt_s, w=init_state */
    vec4 p1;      /* x=spring_k, y=neighbor_coupling, z=damping, w=impulse_gain */
    vec4 p2;      /* x=max_disp, y=max_vel, z=epsilon_zero, w=source_count */
    vec4 p3;      /* x=cam_dx_state, y=cam_dy_state, z=viewport_w, w=viewport_h */
    vec4 src[8];  /* x=px, y=py, z=amp_px, w=radius_px */
} pc;

ivec2 clamp_coord(ivec2 c, ivec2 mn, ivec2 mx) {
    return ivec2(clamp(c.x, mn.x, mx.x), clamp(c.y, mn.y, mx.y));
}

vec4 sample_prev_state(vec2 uv) {
    return texture(u_prev_state, uv);
}

void main() {
    vec2 texel = vec2(1.0 / max(pc.p0.x, 1.0), 1.0 / max(pc.p0.y, 1.0));
    vec2 uv = v_uv;
    vec2 uv_prev = uv + vec2(pc.p3.x * texel.x, pc.p3.y * texel.y);
    uv_prev = clamp(uv_prev, vec2(0.0), vec2(1.0));

    vec2 disp = vec2(0.0);
    vec2 vel = vec2(0.0);
    if (pc.p0.w < 0.5) {
        vec4 st = sample_prev_state(uv_prev);
        disp = st.xy;
        vel = st.zw;
    }

    vec2 disp_n = sample_prev_state(clamp(uv_prev + vec2(0.0, texel.y), vec2(0.0), vec2(1.0))).xy;
    vec2 disp_s = sample_prev_state(clamp(uv_prev - vec2(0.0, texel.y), vec2(0.0), vec2(1.0))).xy;
    vec2 disp_e = sample_prev_state(clamp(uv_prev + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0))).xy;
    vec2 disp_w = sample_prev_state(clamp(uv_prev - vec2(texel.x, 0.0), vec2(0.0), vec2(1.0))).xy;
    vec2 lap = (disp_n + disp_s + disp_e + disp_w) - (4.0 * disp);

    vec2 node_px = vec2(v_uv.x * max(pc.p3.z, 1.0), v_uv.y * max(pc.p3.w, 1.0));
    vec2 impulse = vec2(0.0);
    int src_n = clamp(int(pc.p2.w + 0.5), 0, 8);
    for (int i = 0; i < src_n; ++i) {
        vec2 d = node_px - pc.src[i].xy;
        float amp = pc.src[i].z;
        float r = max(pc.src[i].w, 1.0);
        float dist2 = dot(d, d);
        float dist = sqrt(max(dist2, 1.0));
        float fall = 0.0;
        if (amp < 0.0) {
            /* EMP shockwave ring: radius in src.w, amplitude in -src.z. */
            float band = max(r * 0.10, 6.0);
            float dr = abs(dist - r);
            float q = (dr * dr) / max(band * band, 1.0);
            if (q > 9.0) {
                continue;
            }
            fall = exp(-q);
            amp = -amp;
        } else {
            float r2 = r * r;
            float q = dist2 / r2;
            if (q > 7.5) {
                continue;
            }
            fall = exp(-q);
        }
        float inv_len = inversesqrt(max(dot(d, d), 1.0));
        vec2 dir = d * inv_len;
        impulse += dir * (amp * fall);
    }

    float dt = max(pc.p0.z, 0.0);
    vec2 accel = (-pc.p1.x * disp) + (pc.p1.y * lap) + (pc.p1.w * impulse);
    vel += accel * dt;
    vel *= exp(-pc.p1.z * dt);

    float max_vel = max(pc.p2.y, 1.0);
    float vlen = length(vel);
    if (vlen > max_vel) {
        vel *= (max_vel / vlen);
    }

    disp += vel * dt;
    float max_disp = max(pc.p2.x, 1.0);
    float dlen = length(disp);
    if (dlen > max_disp) {
        disp *= (max_disp / dlen);
    }

    float eps = max(pc.p2.z, 0.0);
    if (abs(vel.x) < eps) vel.x = 0.0;
    if (abs(vel.y) < eps) vel.y = 0.0;
    if (abs(disp.x) < eps) disp.x = 0.0;
    if (abs(disp.y) < eps) disp.y = 0.0;

    out_state = vec4(disp, vel);
}
