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
    float twinkle = 0.90 + 0.10 * sin(pc.p0.z * (0.9 + v_phase * 0.7) + v_phase * 41.0);
    float r = length(v_uv);
    float alpha = 0.0;
    float intensity = 0.0;
    float direct_gain = 1.0;
    float alpha_gain = 1.0;

    if (v_kind < 0.5) {
        if (r > 1.14) {
            discard;
        }
        float core = exp(-7.6 * r * r);
        float halo = exp(-2.4 * r * r);
        float spark = pow(max(0.0, 1.0 - r), 7.5);
        intensity = core * (0.56 + 1.18 * v_col.a * pc.p1.x) +
                    halo * (0.04 + 0.12 * pc.p0.w) +
                    spark * (0.06 + 0.28 * pc.p1.z) * twinkle;
        alpha = (core * 0.34 + halo * 0.06 + spark * 0.04) * clamp(v_col.a * 0.92, 0.0, 1.0);
        direct_gain = mix(pc.p1.x, pc.p0.w, 0.35);
        alpha_gain = mix(pc.p1.x, pc.p0.w, 0.20);
    } else if (v_kind < 1.5) {
        float ellipse = length(vec2(
            v_uv.x * (0.64 - 0.08 * clamp(v_stretch, 0.0, 2.0)),
            v_uv.y * 1.26
        ));
        if (ellipse > 1.20) {
            discard;
        }
        float glow = exp(-1.2 * ellipse * ellipse);
        float streak = exp(-4.6 * abs(v_uv.y)) * pow(clamp((v_uv.x + 1.0) * 0.5, 0.0, 1.0), 1.4);
        intensity = glow * (0.14 + 0.42 * pc.p1.y + 0.24 * v_col.a) +
                    streak * (0.08 + 0.20 * pc.p1.z) * twinkle;
        alpha = (glow * 0.06 + streak * 0.025) * clamp(v_col.a * 0.90, 0.0, 1.0);
        direct_gain = mix(pc.p1.x, pc.p0.w, 0.65);
        alpha_gain = mix(pc.p1.x, pc.p0.w, 0.50);
    } else {
        if (r > 1.46) {
            discard;
        }
        float cloud = exp(-0.78 * r * r);
        float inner = exp(-2.2 * r * r);
        intensity = cloud * (0.08 + 0.28 * pc.p0.w + 0.24 * v_col.a) +
                    inner * (0.04 + 0.12 * pc.p1.x);
        alpha = (cloud * 0.04 + inner * 0.015) * clamp(v_col.a * 0.90, 0.0, 1.0);
        direct_gain = mix(pc.p1.x, pc.p0.w, 0.85);
        alpha_gain = mix(pc.p1.x, pc.p0.w, 0.70);
    }

    intensity *= direct_gain;
    alpha *= clamp(alpha_gain, 0.20, 4.00);
    out_color = vec4(v_col.rgb * intensity, clamp(alpha, 0.0, 1.0));
}
