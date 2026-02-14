#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_pos;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 dpdx = dFdx(v_pos);
    vec3 dpdy = dFdy(v_pos);
    vec3 n = normalize(cross(dpdx, dpdy));

    float slope = clamp(1.0 - abs(n.z), 0.0, 1.0);
    float ridge = pow(slope, 0.75);
    float depth_fade = 1.0 - clamp(v_pos.z, 0.0, 1.0) * 0.72;

    vec3 debug_hue = vec3(
        0.35 + 0.65 * ridge,
        0.30 + 0.70 * (1.0 - clamp(v_pos.z, 0.0, 1.0)),
        0.25 + 0.75 * (0.5 + 0.5 * n.y)
    );
    vec3 color = v_color.rgb * (0.42 + ridge * 0.58) * debug_hue;
    out_color = vec4(color, 1.0);
}
