#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_bary;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 df = fwidth(v_bary);
    vec3 aa = smoothstep(vec3(0.0), df * 1.2, v_bary);
    float edge = 1.0 - min(min(aa.x, aa.y), aa.z);
    if (edge <= 0.001) {
        discard;
    }
    out_color = vec4(v_color.rgb, v_color.a * edge);
}
