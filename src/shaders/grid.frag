#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform GridPC {
    vec4 p0;      /* x=viewport_w, y=viewport_h, z=camera_x, y=camera_y */
    vec4 p1;      /* x=world_w, y=world_h, z=grid_dx, w=grid_dy */
    vec4 p2;      /* rgb=dim_color, w=intensity_scale */
    vec4 p3;      /* rgb=bright_color, w=source_count */
    vec4 src[4];  /* x=world_x, y=world_y, z=amp_px, w=radius_px */
} pc;

float line_band(float coord, float spacing) {
    float cell = coord / spacing;
    float frac_v = fract(cell);
    float d = abs(frac_v - 0.5) * spacing;
    float aa = max(fwidth(coord), 0.5);
    return 1.0 - smoothstep(0.9 + aa * 0.4, 2.3 + aa * 0.9, d);
}

void main() {
    vec2 frag_px = gl_FragCoord.xy;
    float viewport_h = pc.p0.y;
    vec2 world = vec2(
        (pc.p0.z - pc.p1.x * 0.5) + frag_px.x,
        (pc.p0.w - pc.p1.y * 0.5) + (viewport_h - frag_px.y)
    );

    vec2 disp = world;
    int src_n = clamp(int(pc.p3.w + 0.5), 0, 4);
    for (int i = 0; i < src_n; ++i) {
        vec2 d = disp - pc.src[i].xy;
        float r = max(pc.src[i].w, 1.0);
        float q = dot(d, d) / (r * r);
        float w = pc.src[i].z / (1.0 + q * 2.8 + q * q * 0.9);
        float inv_len = inversesqrt(max(dot(d, d), 1.0));
        disp += d * (inv_len * w);
    }

    float gx = line_band(disp.x, max(pc.p1.z, 1.0));
    float gy = line_band(disp.y, max(pc.p1.w, 1.0));
    float line = max(gx, gy);
    if (line <= 0.001) {
        discard;
    }

    float stretch = clamp(length(disp - world) / max(min(pc.p1.z, pc.p1.w) * 0.30, 1.0), 0.0, 1.0);
    vec3 col = mix(pc.p2.rgb, pc.p3.rgb, 0.28 + 0.72 * stretch);
    float alpha = line * (0.34 + 0.50 * stretch) * pc.p2.w;
    out_color = vec4(col, alpha);
}
