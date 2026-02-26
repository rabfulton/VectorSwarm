#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform ArcPC {
    vec4 p0;        /* x=viewport_w, y=viewport_h, z=time_s, w=intensity */
    vec4 color_dim; /* rgb */
    vec4 color_hot; /* rgb */
    vec4 seg;       /* x0,y0,x1,y1 */
    vec4 p1;        /* x=radius_px, y=jag_px, z=seed, w=reserved */
} pc;

float hash11(float x) {
    return fract(sin(x * 127.1 + 311.7) * 43758.5453123);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash11(i.x + i.y * 57.0);
    float b = hash11((i.x + 1.0) + i.y * 57.0);
    float c = hash11(i.x + (i.y + 1.0) * 57.0);
    float d = hash11((i.x + 1.0) + (i.y + 1.0) * 57.0);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 m = mat2(1.7, 1.2, -1.2, 1.7);
    for (int i = 0; i < 4; ++i) {
        v += a * noise2(p);
        p = m * p * 1.45;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 frag = vec2(gl_FragCoord.x, gl_FragCoord.y);
    vec2 a = pc.seg.xy;
    vec2 b = pc.seg.zw;
    vec2 ab = b - a;
    float ab2 = max(dot(ab, ab), 1.0);
    float t = clamp(dot(frag - a, ab) / ab2, 0.0, 1.0);
    vec2 base = a + ab * t;
    vec2 perp = normalize(vec2(-ab.y, ab.x));

    float n0 = fbm(vec2(t * 13.0 + pc.p1.z, pc.p0.z * 13.5 + pc.p1.z * 0.37));
    float n1 = fbm(vec2(t * 23.0 - pc.p1.z * 0.6, -pc.p0.z * 19.0 + pc.p1.z * 0.91));
    float jag = (n0 - 0.5) * pc.p1.y + (n1 - 0.5) * (pc.p1.y * 0.65);
    vec2 center = base + perp * jag;

    float d = length(frag - center);
    float radius = max(pc.p1.x, 1.0);
    float core = exp(-(d * d) / max(radius * radius * 0.08, 1e-4));
    float halo = exp(-(d * d) / max(radius * radius * 1.6, 1e-4));
    float edge = smoothstep(0.0, 0.05, t) * smoothstep(0.0, 0.08, 1.0 - t);
    float flicker = 0.86 + 0.14 * sin(pc.p0.z * 41.0 + t * 33.0 + pc.p1.z * 7.0);
    float intensity = clamp(pc.p0.w, 0.0, 4.0);

    float alpha = (core * 0.92 + halo * 0.36) * edge * flicker * intensity;
    vec3 rgb = mix(pc.color_dim.rgb, pc.color_hot.rgb, clamp(core * 1.25, 0.0, 1.0));
    out_color = vec4(rgb * (0.72 + 0.28 * halo) * intensity, clamp(alpha, 0.0, 1.0));
}
