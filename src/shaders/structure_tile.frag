#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(push_constant) uniform TilePC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=dst_x_px, w=dst_y_px */
    vec4 p1; /* x=dst_w_px, y=dst_h_px, z=alpha, w=unused */
    vec4 p2; /* xy=uv00, zw=uv10 */
    vec4 p3; /* xy=uv11, zw=uv01 */
} pc;

void main() {
    vec4 texel = texture(u_atlas, v_uv);
    out_color = vec4(texel.rgb, texel.a * pc.p1.z);
}
