#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_industry;

layout(push_constant) uniform IndustryPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=alpha_scale */
    vec4 p1; /* rgb=base_color, w=camera_x */
    vec4 p2; /* x=world_w, y=world_h, z=tile_aspect, w=reserved */
} pc;

void main() {
    vec2 frag_px = vec2(gl_FragCoord.xy);
    float vw = max(pc.p0.x, 1.0);
    float vh = max(pc.p0.y, 1.0);
    float y_from_bottom = vh - frag_px.y;
    float cam_x = pc.p1.w;
    float tile_aspect = max(pc.p2.z, 0.1);
    float layer_h_px[3] = float[3](vh * 0.32, vh * 0.40, vh * 0.50);
    float layer_parallax[3] = float[3](0.22, 0.44, 0.78);
    /* Far -> near: each nearer layer sits lower, all anchored from screen bottom. */
    float layer_base[3] = float[3](vh * 0.20, vh * 0.10, vh * 0.00);
    float layer_alpha[3] = float[3](0.20, 0.28, 0.36);

    float a_sum = 0.0;
    for (int i = 0; i < 3; ++i) {
        float h_px = layer_h_px[i];
        float w_px = h_px * tile_aspect;
        float world_x = cam_x * layer_parallax[i] + (frag_px.x - vw * 0.5);
        float u01 = fract(world_x / max(w_px, 1.0));
        float v01 = clamp((y_from_bottom - layer_base[i]) / max(h_px, 1.0), 0.0, 1.0);
        float mask = step(layer_base[i], y_from_bottom) * step(y_from_bottom, layer_base[i] + h_px);

        vec4 tex = texture(u_industry, vec2(u01, 1.0 - v01));
        float silhouette = tex.a;
        float a_layer = silhouette * layer_alpha[i] * mask;
        /* Non-additive compositing avoids overlap banding between parallax layers. */
        a_sum = max(a_sum, a_layer);
    }

    float alpha = clamp(a_sum * pc.p0.w, 0.0, 0.78);
    vec3 col = pc.p1.rgb;
    out_color = vec4(col, alpha);
}
