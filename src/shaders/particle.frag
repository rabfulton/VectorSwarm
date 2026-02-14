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

void main() {
    float alpha = 0.0;
    float intensity = 0.0;
    if (v_kind > 1.5) {
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
        /* Extra emissive lobe to mimic phosphor bloom without another pass. */
        float bloomish = exp(-3.2 * r) * (0.45 + 0.55 * core);
        float tx = clamp(-v_uv.x, 0.0, 1.0);
        float streak = exp(-12.0 * abs(v_uv.y)) * tx * tx * (0.25 + 0.75 * v_trail);
        float core_gain = max(pc.params.z, 0.05);
        intensity = core * (2.35 + 0.85 * v_heat) * core_gain +
                    halo * 0.30 +
                    streak * (1.45 * pc.params.w) +
                    bloomish * 1.25;
        alpha = core * 1.00 + halo * 0.20 + streak * 0.34 + bloomish * 0.20;
    } else {
        float d = abs(v_uv.x) + abs(v_uv.y);
        if (d > 1.0) {
            discard;
        }
        float core = 1.0 - smoothstep(0.00, 0.22, d);
        float halo = 1.0 - smoothstep(0.10, 0.85, d);
        float bloomish = exp(-2.7 * d) * (0.42 + 0.58 * core);
        float tx = clamp(-v_uv.x, 0.0, 1.0);
        float streak = exp(-10.0 * abs(v_uv.y)) * tx * tx * (0.20 + 0.70 * v_trail);
        float core_gain = max(pc.params.z, 0.05);
        intensity = core * (2.10 + 0.70 * v_heat) * core_gain +
                    halo * 0.28 +
                    streak * (1.25 * pc.params.w) +
                    bloomish * 1.10;
        alpha = core * 0.98 + halo * 0.18 + streak * 0.31 + bloomish * 0.18;
    }
    /* Heat-shift sparks from white-yellow toward molten orange over life. */
    vec3 hot = vec3(1.00, 0.96, 0.78);
    vec3 cool = vec3(1.00, 0.62, 0.25);
    vec3 heat_tint = mix(cool, hot, clamp(v_heat, 0.0, 1.0));
    alpha = clamp(alpha, 0.0, 1.0) * v_col.a;
    out_color = vec4(v_col.rgb * heat_tint * intensity, alpha);
}
