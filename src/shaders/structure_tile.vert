#version 450

layout(location = 0) out vec2 v_uv;

layout(push_constant) uniform TilePC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=dst_x_px, w=dst_y_px */
    vec4 p1; /* x=dst_w_px, y=dst_h_px, z=alpha, w=unused */
    vec4 p2; /* xy=uv00, zw=uv10 */
    vec4 p3; /* xy=uv11, zw=uv01 */
} pc;

void main() {
    vec2 pos_lut[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );
    vec2 uv_lut[6] = vec2[](
        vec2(pc.p2.x, pc.p2.y),
        vec2(pc.p2.z, pc.p2.w),
        vec2(pc.p3.x, pc.p3.y),
        vec2(pc.p2.x, pc.p2.y),
        vec2(pc.p3.x, pc.p3.y),
        vec2(pc.p3.z, pc.p3.w)
    );
    vec2 local = pos_lut[gl_VertexIndex];
    vec2 dst_px = vec2(pc.p0.z, pc.p0.w) + local * vec2(pc.p1.x, pc.p1.y);
    vec2 ndc = vec2(
        (dst_px.x / max(pc.p0.x, 1.0)) * 2.0 - 1.0,
        1.0 - (dst_px.y / max(pc.p0.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = uv_lut[gl_VertexIndex];
}
