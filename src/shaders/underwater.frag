#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform UnderwaterPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=density */
    vec4 p1; /* rgb=primary_dim, w=caustic_strength */
    vec4 p2; /* rgb=secondary, w=haze_alpha */
    vec4 p3; /* x=world_origin_x, y=world_origin_y, z=caustic_scale, w=current_speed */
    vec4 p4; /* x=bubble_rate, y=palette_shift, z=world_w, w=world_h */
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

float bubble_field(vec2 world_px, float t, float bubble_rate) {
    float d = 0.0;
    const int emit_n = 7;
    for (int i = 0; i < emit_n; ++i) {
        float fi = float(i);
        float ex = (fi + 0.35) / float(emit_n);
        ex += (hash12(vec2(fi, 7.31)) - 0.5) * 0.05;
        float cycle = 6.0 + fi * 0.77;
        float phase = fract((t * (0.10 + 0.03 * fi) * max(bubble_rate, 0.0)) + hash12(vec2(fi, 11.2)));
        float y01 = 1.0 - phase;
        float x01 = ex + sin((phase * 7.1 + fi * 1.37) * 3.14159265) * (0.012 + 0.008 * fi);
        vec2 c = vec2(x01 * pc.p4.z, y01 * pc.p4.w);
        vec2 delta = world_px - c;
        float r = mix(6.0, 18.0, hash12(vec2(fi, floor(t / cycle) + 3.0)));
        float dd = length(delta);
        float ring = exp(-pow((dd - r) / max(r * 0.20, 1.0), 2.0));
        d += 0.22 * ring;
    }
    return clamp(d, 0.0, 1.0);
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

    float n0 = fbm(n_uv * vec2(5.0, 2.2) + flow);
    float n1 = fbm(n_uv * vec2(9.8, 4.0) - flow * 1.55 + vec2(6.1, -3.7));
    float haze = smoothstep(0.28, 0.86, n0 * 0.72 + n1 * 0.28);
    haze *= density;

    float ca = fbm(n_uv * vec2(14.0, 8.0) + vec2(t * 0.15, -t * 0.08) * cur);
    ca = pow(clamp(ca, 0.0, 1.0), 2.2);
    ca *= max(pc.p1.w, 0.0) * density;

    float bubbles = bubble_field(world_px, t, pc.p4.x) * density;
    float shade = clamp(0.18 + haze * 0.75 + ca * 0.60, 0.0, 1.0);

    vec3 col_a = pc.p1.rgb;
    vec3 col_b = pc.p2.rgb;
    float shift = pc.p4.y;
    vec3 col = mix(col_a, col_b, clamp(shade + shift * 0.20, 0.0, 1.0));
    col += vec3(0.55, 0.72, 0.88) * (0.24 * bubbles + 0.18 * ca);

    float alpha = clamp((0.07 + 0.20 * haze + 0.14 * ca + 0.18 * bubbles) * max(pc.p2.w, 0.0), 0.0, 0.55);
    out_color = vec4(col, alpha);
}
