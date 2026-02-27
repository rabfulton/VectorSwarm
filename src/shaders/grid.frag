#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_grid_state;

layout(push_constant) uniform GridPC {
    vec4 p0;      /* x=viewport_w, y=viewport_h, z=grid_dx, w=grid_dy */
    vec4 p1;      /* x=distort_gain, y=strain_gain, z=state_w, w=state_h */
    vec4 p2;      /* rgb=dim_color, w=intensity_scale */
    vec4 p3;      /* rgb=bright_color, w=line_boost */
    vec4 p4;      /* x=camera_x, y=camera_y, z=world_w, w=world_h */
} pc;

float line_band(float coord, float spacing) {
    float cell = coord / max(spacing, 1.0);
    float frac_v = fract(cell);
    float d = abs(frac_v - 0.5) * max(spacing, 1.0);
    float aa = max(fwidth(coord), 0.5);
    return 1.0 - smoothstep(0.55 + aa * 0.35, 1.65 + aa * 0.75, d);
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    vec2 uv = frag_px / vec2(max(pc.p0.x, 1.0), max(pc.p0.y, 1.0));
    vec4 st = texture(u_grid_state, uv);
    vec2 disp = st.xy * pc.p1.x;
    vec2 vel = st.zw;
    vec2 p = frag_px + disp;
    vec2 world = vec2(
        (pc.p4.x - pc.p4.z * 0.5) + p.x,
        (pc.p4.y - pc.p4.w * 0.5) + (pc.p0.y - p.y)
    );

    float gx = line_band(world.x, pc.p0.z);
    float gy = line_band(world.y, pc.p0.w);
    float line = max(gx, gy);
    if (line <= 0.001) {
        discard;
    }

    float strain = clamp(length(disp) / max(min(pc.p0.z, pc.p0.w) * 0.36, 1.0), 0.0, 1.0);
    strain = max(strain, clamp(length(vel) * pc.p1.y, 0.0, 1.0));
    vec3 col = mix(pc.p2.rgb, pc.p3.rgb, 0.22 + 0.78 * strain);
    float alpha = line * (0.25 + 0.55 * strain) * pc.p2.w * pc.p3.w;
    out_color = vec4(col, alpha);
}
