#version 450

layout(location = 0) in vec4 v_col;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in float v_kind;
layout(location = 3) in float v_trail;
layout(location = 4) in float v_heat;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 params; /* x=viewport_width, y=viewport_height, z=core_gain, w=trail_gain */
} pc;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise21(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += a * noise21(p);
        p = p * 2.03 + vec2(13.7, 9.2);
        a *= 0.5;
    }
    return v;
}

void main() {
    float alpha = 0.0;
    float intensity = 0.0;
    if (v_kind > 2.5) {
        float r = length(v_uv);
        if (r > 1.0) {
            discard;
        }
        vec2 puv = v_uv * 2.7 + vec2(v_heat * 11.3, v_heat * 8.1);
        float n = fbm(puv);
        float n2 = fbm(puv * 1.9 + vec2(3.7, 5.1));
        float edge = r + (n - 0.5) * 0.72;
        float core = 1.0 - smoothstep(0.06, 0.70, edge);
        float wisps = 1.0 - smoothstep(0.22, 1.06, edge + (n2 - 0.5) * 0.58);
        intensity = core * 0.90 + wisps * 0.62;
        alpha = (core * 0.26 + wisps * 0.20) * (0.65 + 0.35 * n);
    } else if (v_kind > 1.5) {
        float r = length(v_uv);
        if (r > 1.0) {
            discard;
        }
        float core = 1.0 - smoothstep(0.00, 0.26, r);
        float halo = 1.0 - smoothstep(0.08, 1.00, r);
        intensity = core * 2.20 + halo * 0.90;
        alpha = core * 0.98 + halo * 0.42;
    } else if (v_kind < 0.5) {
        float r = length(v_uv);
        if (r > 1.0) {
            discard;
        }
        /* Phosphor-like spark: hot tight core + short molten trailing burn. */
        float core = 1.0 - smoothstep(0.00, 0.22, r);
        float halo = 1.0 - smoothstep(0.08, 0.75, r);
        float tx = clamp(-v_uv.x, 0.0, 1.0);
        float streak = exp(-12.0 * abs(v_uv.y)) * tx * tx * (0.25 + 0.75 * v_trail);
        float core_gain = 0.55 + 0.90 * max(pc.params.z, 0.0);
        intensity = core * (2.35 + 0.85 * v_heat) * core_gain +
                    halo * 0.30 +
                    streak * (1.45 * pc.params.w);
        alpha = core * 1.00 + halo * 0.20 + streak * 0.34;
    } else {
        float d = abs(v_uv.x) + abs(v_uv.y);
        if (d > 1.0) {
            discard;
        }
        float core = 1.0 - smoothstep(0.00, 0.22, d);
        float halo = 1.0 - smoothstep(0.10, 0.85, d);
        float tx = clamp(-v_uv.x, 0.0, 1.0);
        float streak = exp(-10.0 * abs(v_uv.y)) * tx * tx * (0.20 + 0.70 * v_trail);
        float core_gain = 0.55 + 0.90 * max(pc.params.z, 0.0);
        intensity = core * (2.10 + 0.70 * v_heat) * core_gain +
                    halo * 0.28 +
                    streak * (1.25 * pc.params.w);
        alpha = core * 0.98 + halo * 0.18 + streak * 0.31;
    }
    /* Heat-shift sparks from white-yellow toward molten orange over life. */
    vec3 hot = vec3(1.00, 0.96, 0.78);
    vec3 cool = vec3(1.00, 0.62, 0.25);
    vec3 heat_tint = mix(cool, hot, clamp(v_heat, 0.0, 1.0));
    alpha = clamp(alpha, 0.0, 1.0) * v_col.a;
    out_color = vec4(v_col.rgb * heat_tint * intensity, alpha);
}
