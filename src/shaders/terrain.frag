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

    float alpha = (0.06 + ridge * 0.28) * depth_fade;
    alpha = clamp(alpha, 0.04, 0.38);

    vec3 color = v_color.rgb * (0.48 + ridge * 0.52);
    out_color = vec4(color, alpha * v_color.a);
}
