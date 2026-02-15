#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform FogPC {
    vec4 p0;      /* x=viewport_w, y=viewport_h, z=time_s, w=alpha_scale */
    vec4 p1;      /* rgb=primary_dim, w=density_scale */
    vec4 p2;      /* rgb=secondary, w=emitter_count */
    vec4 p3;      /* x=world_origin_x, y=world_origin_y, z=noise_scale, w=flow_scale */
    vec4 emit[4]; /* x=sx, y=sy, z=radius_px, w=power */
} pc;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i + vec2(0.0, 0.0));
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 4; ++i) {
        v += a * noise2(p);
        p = m * p * 1.45;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.x, gl_FragCoord.y);
    float t = pc.p0.z;

    vec2 world_px = pc.p3.xy + frag_px;
    vec2 world_uv = world_px / vec2(pc.p0.x, pc.p0.y);
    float noise_scale = max(pc.p3.z, 0.05);
    float flow_scale = max(pc.p3.w, 0.0);
    vec2 flow = vec2(t * 0.035, -t * 0.026) * flow_scale;
    vec2 noise_uv = world_uv * noise_scale;
    float n0 = fbm(noise_uv * vec2(5.8, 2.8) + flow);
    float n1 = fbm(noise_uv * vec2(11.2, 4.7) - flow * 1.7 + vec2(8.2, -4.4));
    float n = n0 * 0.72 + n1 * 0.28;
    float dens = smoothstep(0.36, 0.80, n);
    dens *= 0.60 + 0.40 * sin(world_uv.y * 3.14159265);
    dens *= pc.p1.w;

    float light = 0.0;
    int ecount = int(pc.p2.w + 0.5);
    ecount = clamp(ecount, 0, 4);
    for (int i = 0; i < ecount; ++i) {
        vec2 d = frag_px - pc.emit[i].xy;
        float r = max(pc.emit[i].z, 1.0);
        light += pc.emit[i].w * exp(-dot(d, d) / (r * r));
    }
    light = clamp(light, 0.0, 1.6);

    float fog_mask = smoothstep(0.02, 0.14, dens);
    float light_in_fog = light * fog_mask;
    float tint = clamp(dens * 0.7 + light_in_fog * 0.5, 0.0, 1.0);
    vec3 col = mix(pc.p1.rgb, pc.p2.rgb, tint);
    float alpha = clamp((dens * 0.24 + light_in_fog * 0.10) * pc.p0.w, 0.0, 0.36);
    out_color = vec4(col, alpha);
}
