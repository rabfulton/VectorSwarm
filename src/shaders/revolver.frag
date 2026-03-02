#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 0) uniform sampler2D u_industry;
layout(push_constant) uniform RevolverPC {
    vec4 p0; /* x=viewport_w, y=viewport_h, z=time_s, w=alpha_scale */
    vec4 p1; /* rgb=base_color, w=camera_x */
    vec4 p2; /* x=world_w, y=world_h, z=tile_aspect, w=reserved */
} pc;

const float PI        = 3.14159265359;
const float TWO_PI    = 6.28318530718;
const float INNER_S   = 0.90;
const float CENTRAL_S = 0.25;
const float CYL_BRIGHTNESS = 1.24;

/* Sample a cylindrical ring wall at this pixel.
   sin_t  : sin(theta) for this ring — used for both depth/y-scale and U coord.
   front  : true = front half of cylinder (depth > 0.5)
   Returns true and writes col_out when an opaque texel is hit. */
bool ring_sample(float sin_t, bool front,
                 float sy_game, float cy,
                 float ring_y_base, float H,
                 vec3 base_col, float tiles, float scroll, float dim_extra, float base_up_fade,
                 out vec4 col_out)
{
    float cos_t   = sqrt(max(1.0 - sin_t * sin_t, 0.0));
    float depth   = front ? (cos_t * 0.5 + 0.5) : (0.5 - cos_t * 0.5);
    float z       = front ? cos_t : -cos_t; /* camera-space ring depth: back=-1, front=+1 */
    float persp   = 2.4 / (2.4 - z * 1.0);  /* reciprocal perspective, softened below */
    float y_scale = mix(1.0, persp, 0.55);
    y_scale       = clamp(y_scale, 0.52, 1.08);
    float base_y  = cy + (ring_y_base - cy) * y_scale;
    float top_y   = base_y + H * y_scale;
    if (sy_game < base_y || sy_game > top_y) return false;
    float v = 1.0 - (sy_game - base_y) / (top_y - base_y);
    float up01 = clamp((sy_game - base_y) / max(top_y - base_y, 1.0e-5), 0.0, 1.0);

    float theta_u;
    if (front) {
        theta_u = asin(sin_t);
    } else {
        theta_u = (sin_t >= 0.0) ? (PI - asin(sin_t)) : (-PI - asin(sin_t));
    }
    float u = fract(fract(theta_u / TWO_PI + 0.5 + scroll) * tiles);
    if (texture(u_industry, vec2(u, v)).a < 0.5) return false;
    float dim = (0.50 + 0.50 * depth) * dim_extra;
    float fade = mix(1.0, up01, clamp(base_up_fade, 0.0, 1.0));
    float top_boost = 1.0 + 0.40 * up01;
    col_out = vec4(base_col * (0.25 + 0.75 * v) * dim * fade * top_boost * CYL_BRIGHTNESS, 1.0);
    return true;
}

void main() {
    float vw          = max(pc.p0.x, 1.0);
    float vh          = max(pc.p0.y, 1.0);
    float camera_x    = pc.p1.w;
    float tile_aspect = max(pc.p2.z, 0.1);
    bool front_only   = (pc.p2.w >= 0.5);
    vec3  base_col    = pc.p1.rgb;
    float alpha_scale = clamp(pc.p0.w, 0.0, 1.0);

    float sx      = gl_FragCoord.x;
    float sy_game = vh - gl_FragCoord.y;  /* game-space y: 0 at bottom */
    float cx      = vw * 0.5;
    float cy      = vh * 0.5;
    float radius  = vw * 0.485;
    float period  = vw * 2.4;

    float ring_y_base = vh * 0.06;   /* world-y baseline of outer/inner rings */
    float H           = vh * 0.25;   /* wall height for outer/inner rings */
    float ring_y_cen  = vh * 0.06;   /* baseline of central ring (can go off-screen) */
    float H_cen       = vh * 0.55;   /* central ring is taller */

    float tiles_outer = period / (H * tile_aspect);
    float tiles_inner = tiles_outer * INNER_S;
    float tiles_cen   = 2.0;  /* double texture wrap around central ring */

    float scroll_outer = camera_x / period;
    float scroll_inner = camera_x * INNER_S / period;
    float scroll_cen   = camera_x * CENTRAL_S * 2 / period;

    float sin_outer = (sx - cx) / radius;
    float sin_inner = (sx - cx) / (radius * INNER_S);
    float sin_cen   = (sx - cx) / (radius * CENTRAL_S);

    bool out_ok = (abs(sin_outer) <= 1.0);
    bool in_ok  = (abs(sin_inner) <= 1.0);
    bool cen_ok = (abs(sin_cen)   <= 1.0);

    /* Draw order: back to front (painter's algorithm).
       Each ring uses its own sin_t for both depth/y-scale and U coord,
       so the seam where front and back halves meet is always smooth. */
    vec4 result = vec4(0.0);
    vec4 hit;

    /* central ring */
    if (cen_ok && ring_sample(sin_cen, true, sy_game, cy,
            ring_y_cen, H_cen, base_col, tiles_cen, scroll_cen, 1.45, 1.0, hit))
        result = hit;
    /* inner ring */
    if (in_ok && ring_sample(sin_inner, true, sy_game, cy,
            ring_y_base, H * 1.5, base_col, tiles_inner, scroll_inner, 0.85, 1.0, hit))
        result = hit;
    /* outer ring */
    if (out_ok && ring_sample(sin_outer, true, sy_game, cy,
            ring_y_base, H, base_col, tiles_outer, scroll_outer, 1.40, 1.0, hit))
        result = hit;

    if (result.a < 0.5) discard;
    out_color = result * alpha_scale;
}
