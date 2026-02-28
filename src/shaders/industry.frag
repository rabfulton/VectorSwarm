#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_industry;

layout(push_constant) uniform IndustryPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=alpha_scale */
    vec4 p1; /* rgb=base_color, w=camera_x */
    vec4 p2; /* x=world_w, y=world_h, z=tile_aspect, w=reserved */
} pc;

float sample_alpha(vec2 uv) {
    return texture(u_industry, uv).a;
}

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    float vw = max(pc.p0.x, 1.0);
    float vh = max(pc.p0.y, 1.0);
    float y_from_bottom = vh - frag_px.y;
    float cam_x = pc.p1.w;
    float tile_aspect = max(pc.p2.z, 0.1);
    float layer_h_px[3] = float[3](vh * 0.24, vh * 0.31, vh * 0.38);
    float layer_parallax[3] = float[3](0.22, 0.44, 0.78);
    /* Far -> near: each nearer layer sits lower, all anchored from screen bottom. */
    float layer_base[3] = float[3](vh * 0.12, vh * 0.05, vh * -0.05);
    float cov_near = 0.0;
    float edge_near = 0.0;
    float glint_near = 0.0;
    float grad_near = 0.0;
    float layer_u_near = 1.0;
    ivec2 ts = textureSize(u_industry, 0);
    vec2 texel = 1.0 / vec2(max(ts.x, 1), max(ts.y, 1));
    /* Nearest-only compositing prevents back layers from showing through front pixels. */
    for (int i = 2; i >= 0; --i) {
        float h_px = layer_h_px[i];
        float w_px = h_px * tile_aspect;
        float world_x = cam_x * layer_parallax[i] + (frag_px.x - vw * 0.5);
        float u01 = fract(world_x / max(w_px, 1.0));
        float v01 = clamp((y_from_bottom - layer_base[i]) / max(h_px, 1.0), 0.0, 1.0);
        float mask = step(layer_base[i], y_from_bottom) * step(y_from_bottom, layer_base[i] + h_px);

        vec2 uv = vec2(u01, 1.0 - v01);
        float a0 = sample_alpha(uv);
        float axp = sample_alpha(uv + vec2(texel.x, 0.0));
        float axm = sample_alpha(uv - vec2(texel.x, 0.0));
        float ayp = sample_alpha(uv + vec2(0.0, texel.y));
        float aym = sample_alpha(uv - vec2(0.0, texel.y));
        float edge = clamp((abs(axp - axm) + abs(ayp - aym)) * 1.6, 0.0, 1.0);
        float top_edge = clamp(a0 - aym, 0.0, 1.0);
        float left_edge = clamp(a0 - axm, 0.0, 1.0);

        float silhouette = step(0.5, a0);
        float coverage = silhouette * mask;
        if (cov_near < 0.5 && coverage > 0.5) {
            cov_near = 1.0;
            edge_near = edge;
            glint_near = max(top_edge * 1.0, left_edge * 0.72);
            grad_near = v01;
            layer_u_near = float(i) * 0.5;
        }
    }

    float alpha = step(0.5, cov_near) * clamp(pc.p0.w, 0.0, 1.0);
    vec3 base = pc.p1.rgb;
    float distance_dim = 0.58 + 0.42 * layer_u_near; /* far darker, near brighter */
    vec3 col = base * (0.18 + 0.72 * grad_near) * distance_dim +
               vec3(0.09, 0.11, 0.09) * edge_near * 0.26 * distance_dim +
               vec3(0.22, 0.26, 0.22) * glint_near * 0.28 * distance_dim;
    out_color = vec4(col * alpha, alpha);
}
