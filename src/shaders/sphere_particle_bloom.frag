#version 450

layout(location = 0) in vec4 v_col;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in float v_kind;
layout(location = 3) in float v_phase;
layout(location = 4) in float v_stretch;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 p0; /* x=viewport_width, y=viewport_height, z=time_s, w=glow_gain */
    vec4 p1; /* x=core_gain, y=rim_gain, z=twinkle_gain, w=reserved */
    vec4 p2;
    vec4 p3;
    vec4 top;
    vec4 mid;
    vec4 low;
    vec4 rim;
} pc;

void main() {
    float twinkle = 0.92 + 0.08 * sin(pc.p0.z * (0.8 + v_phase * 0.6) + v_phase * 31.0);
    float r = length(v_uv);
    float alpha = 0.0;
    float intensity = 0.0;
    float direct_gain = 1.0;
    float alpha_gain = 1.0;

    if (v_kind < 0.5) {
        if (r > 1.30) {
            discard;
        }
        float halo = exp(-1.55 * r * r);
        float core = exp(-4.2 * r * r);
        intensity = halo * (0.05 + 0.16 * pc.p0.w + 0.12 * v_col.a) +
                    core * (0.02 + 0.06 * pc.p1.x) +
                    twinkle * 0.03;
        alpha = (halo * 0.025 + core * 0.008) * clamp(v_col.a * 0.80, 0.0, 1.0);
        direct_gain = mix(pc.p1.x, pc.p0.w, 0.55);
        alpha_gain = mix(pc.p1.x, pc.p0.w, 0.45);
    } else if (v_kind < 1.5) {
        float ellipse = length(vec2(
            v_uv.x * (0.70 - 0.06 * clamp(v_stretch, 0.0, 2.0)),
            v_uv.y * 1.14
        ));
        if (ellipse > 1.50) {
            discard;
        }
        float glow = exp(-0.92 * ellipse * ellipse);
        float streak = exp(-2.8 * abs(v_uv.y)) * pow(clamp((v_uv.x + 1.0) * 0.5, 0.0, 1.0), 1.1);
        intensity = glow * (0.06 + 0.20 * pc.p1.y + 0.12 * v_col.a) +
                    streak * (0.04 + 0.10 * pc.p1.z) * twinkle;
        alpha = (glow * 0.025 + streak * 0.015) * clamp(v_col.a * 0.80, 0.0, 1.0);
        direct_gain = mix(pc.p1.x, pc.p0.w, 0.70);
        alpha_gain = mix(pc.p1.x, pc.p0.w, 0.60);
    } else {
        if (r > 1.72) {
            discard;
        }
        float cloud = exp(-0.42 * r * r);
        intensity = cloud * (0.03 + 0.12 * pc.p0.w + 0.08 * v_col.a);
        alpha = cloud * 0.02 * clamp(v_col.a, 0.0, 1.0);
        direct_gain = mix(pc.p1.x, pc.p0.w, 0.85);
        alpha_gain = mix(pc.p1.x, pc.p0.w, 0.75);
    }

    intensity *= direct_gain;
    alpha *= clamp(alpha_gain, 0.10, 4.00);
    out_color = vec4(v_col.rgb * intensity, clamp(alpha, 0.0, 1.0));
}
