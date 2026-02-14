#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_pos;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    vec4 color;
    vec4 params; /* x=viewport_width, y=viewport_height, z=intensity, w=hue_shift */
    vec4 tune;   /* x=brightness, y=opacity, z=normal_variation, w=depth_fade */
} pc;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
    vec3 dpdx = dFdx(v_pos);
    vec3 dpdy = dFdy(v_pos);
    vec3 n = normalize(cross(dpdx, dpdy));

    float zf = clamp(v_pos.z, 0.0, 1.0);
    float slope = clamp(1.0 - abs(n.z), 0.0, 1.0);
    float ridge = pow(slope, 0.78);
    float nx = clamp(0.5 + 0.5 * n.x, 0.0, 1.0);
    float ny = clamp(0.5 + 0.5 * n.y, 0.0, 1.0);

    float normal_term = mix(1.0, mix(nx, ny, 0.35), clamp(pc.tune.z, 0.0, 1.5));
    float depth_term = 1.0 - zf * clamp(pc.tune.w, 0.0, 1.8);
    float light = (0.58 + ridge * 0.42) * normal_term * depth_term;
    light *= clamp(pc.tune.x, 0.2, 2.5);

    vec3 color = max(v_color.rgb * light, vec3(0.0));
    vec3 hsv = rgb2hsv(max(color, vec3(1e-5)));
    hsv.x = fract(hsv.x + pc.params.w);
    hsv.y = clamp(hsv.y * (0.45 + 0.35 * clamp(pc.tune.z, 0.0, 1.5)), 0.0, 1.0);
    out_color = vec4(hsv2rgb(hsv), clamp(pc.tune.y, 0.0, 1.0));
}
