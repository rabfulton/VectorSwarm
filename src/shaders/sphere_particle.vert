#version 450

layout(location = 0) in vec4 in_seed0; /* xyz=local normal, w=variant */
layout(location = 1) in vec4 in_seed1; /* xyz=t0, w=seed */
layout(location = 2) in vec4 in_seed2; /* xyz=t1, w=noise0 */
layout(location = 3) in vec4 in_seed3; /* x=noise1, y=noise2, z=pocket, w=dither */
layout(location = 4) in vec4 in_seed4; /* x=shell_base, y=height_t, z=stream_t, w=mode */

layout(location = 0) out vec4 v_col;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out float v_kind;
layout(location = 3) out float v_phase;
layout(location = 4) out float v_stretch;

layout(push_constant) uniform Push {
    vec4 p0; /* x=viewport_width, y=viewport_height, z=time_s, w=glow_gain */
    vec4 p1; /* x=core_gain, y=rim_gain, z=twinkle_gain, w=reserved */
    vec4 p2; /* x=center_x, y=center_y, z=sphere_radius_px, w=dpi */
    vec4 p3; /* sphere orientation quaternion wxyz */
    vec4 top;
    vec4 mid;
    vec4 low;
    vec4 rim;
} pc;

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.yzw, v);
    return v + q.x * t + cross(q.yzw, t);
}

vec3 palette_color(float warm_t, float lift_t) {
    vec3 base;
    if (warm_t < 0.5) {
        base = mix(pc.top.rgb, pc.mid.rgb, warm_t * 2.0);
    } else {
        base = mix(pc.mid.rgb, pc.low.rgb, (warm_t - 0.5) * 2.0);
    }
    return clamp(mix(base, pc.rim.rgb, lift_t * vec3(0.28, 0.24, 0.34)), 0.0, 1.0);
}

vec3 terrain_solar_color(float height_t, float rough_t) {
    vec3 deep = vec3(0.08, 0.14, 0.58);
    vec3 shelf = vec3(0.28, 0.10, 0.70);
    vec3 c0;
    vec3 c1;
    float t;
    float band_t = clamp(height_t * 0.84 + rough_t * 0.16, 0.0, 1.0);

    if (band_t < 0.20) {
        c0 = deep;
        c1 = shelf;
        t = band_t / 0.20;
    } else if (band_t < 0.48) {
        c0 = shelf;
        c1 = pc.mid.rgb;
        t = (band_t - 0.20) / 0.28;
    } else if (band_t < 0.76) {
        c0 = pc.mid.rgb;
        c1 = pc.low.rgb;
        t = (band_t - 0.48) / 0.28;
    } else if (band_t < 0.93) {
        c0 = pc.low.rgb;
        c1 = mix(pc.low.rgb, pc.top.rgb, 0.45);
        t = (band_t - 0.76) / 0.17;
    } else {
        c0 = mix(pc.low.rgb, pc.top.rgb, 0.45);
        c1 = pc.top.rgb;
        t = (band_t - 0.93) / 0.07;
    }

    return clamp(mix(c0, c1, t), 0.0, 1.0);
}

vec3 cme_hot_color(float height_t, float heat_t, float bright_t, float excursion_t) {
    vec3 deep = vec3(0.82, 0.14, 0.02);
    vec3 orange = vec3(1.00, 0.34, 0.06);
    vec3 gold = vec3(1.00, 0.74, 0.18);
    vec3 whitehot = pc.top.rgb;
    float band_t = clamp(height_t * 0.38 + heat_t * 0.32 + bright_t * 0.18 + excursion_t * 0.12, 0.0, 1.0);

    if (band_t < 0.34) {
        return mix(deep, orange, band_t / 0.34);
    }
    if (band_t < 0.76) {
        return mix(orange, gold, (band_t - 0.34) / 0.42);
    }
    return mix(gold, whitehot, (band_t - 0.76) / 0.24);
}

vec3 desaturate_color(vec3 c, float amount) {
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    return mix(c, vec3(luma), clamp(amount, 0.0, 1.0));
}

void main() {
    vec2 corner;
    if (gl_VertexIndex == 0) corner = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) corner = vec2(1.0, -1.0);
    else if (gl_VertexIndex == 2) corner = vec2(-1.0, 1.0);
    else corner = vec2(1.0, 1.0);

    vec3 raw_local = in_seed0.xyz;
    vec3 raw_t0 = in_seed1.xyz;
    vec3 raw_t1 = in_seed2.xyz;
    vec3 local = normalize(raw_local);
    vec3 t0 = normalize(raw_t0);
    vec3 t1 = normalize(raw_t1);
    float variant = in_seed0.w;
    float seed = in_seed1.w;
    float noise0 = in_seed2.w;
    float noise1 = in_seed3.x;
    float noise2 = in_seed3.y;
    float pocket = in_seed3.z;
    float dither = in_seed3.w;
    float shell_base = in_seed4.x;
    float height_t = in_seed4.y;
    float stream_t = in_seed4.z;
    float stream_mode = in_seed4.w;
    float rough_t = clamp(noise0 * 0.42 + noise1 * 0.34 + pocket * 0.24, 0.0, 1.0);

    if (variant >= 3.5) {
        vec3 plume_view = quat_rotate(pc.p3, raw_local);
        vec3 vel_view = quat_rotate(pc.p3, raw_t0);
        vec2 screen = pc.p2.xy + plume_view.xy * pc.p2.z;
        vec2 axis2 = normalize(vel_view.xy);
        float front2 = clamp(plume_view.z, 0.0, 1.0);
        float radius2;
        float alpha2;
        float heat_t = clamp(noise0, 0.0, 1.0);
        float size_t = clamp(noise1, 0.0, 1.0);
        float bright_t = clamp(noise2, 0.0, 1.0);
        float age_t = clamp(stream_t, 0.0, 1.0);
        float excursion_t = clamp(stream_mode, 0.0, 1.0);
        vec3 base2;

        if (length(axis2) < 1e-4) {
            axis2 = normalize(plume_view.xy);
        }
        if (length(axis2) < 1e-4) {
            axis2 = vec2(1.0, 0.0);
        }

        if (plume_view.z <= 0.0 || pocket <= 0.001) {
            gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
            v_col = vec4(0.0);
            v_uv = vec2(0.0);
            v_kind = 0.0;
            v_phase = seed;
            v_stretch = 0.0;
            return;
        }

        radius2 = pc.p2.w * (0.72 + 0.82 * size_t + 0.72 * bright_t + 0.64 * excursion_t);
        alpha2 = clamp(
            pocket *
            (0.18 + 0.22 * front2 + 0.18 * bright_t) *
            (1.0 - 0.20 * age_t),
            0.01,
            0.14
        );
        base2 = cme_hot_color(height_t, heat_t, bright_t, excursion_t);
        base2 = mix(base2, pc.top.rgb, 0.04 + 0.14 * bright_t * (1.0 - age_t));
        base2 *= 0.66 + 0.18 * bright_t + 0.10 * heat_t;

        {
            vec2 perp = vec2(-axis2.y, axis2.x);
            float tail_scale = 1.0 + 0.04 * size_t;
            float head_scale = 1.0 + 0.08 * bright_t;
            float cross_scale = 0.98 + 0.06 * excursion_t;
            float along = corner.x * radius2 * ((corner.x < 0.0) ? tail_scale : head_scale);
            float across = corner.y * radius2 * cross_scale;
            vec2 p = screen + axis2 * along + perp * across;
            vec2 ndc;
            ndc.x = (p.x / pc.p0.x) * 2.0 - 1.0;
            ndc.y = 1.0 - (p.y / pc.p0.y) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            v_col = vec4(base2, alpha2);
            v_uv = vec2(corner.x * ((corner.x < 0.0) ? tail_scale : head_scale), corner.y * cross_scale);
            v_kind = 0.0;
            v_phase = seed;
            v_stretch = 0.02 + 0.06 * bright_t;
            return;
        }
    }

    float jitter_u = (noise1 - 0.5) * 0.020;
    float jitter_v = (noise2 - 0.5) * 0.020;
    float plasma_phase0 = pc.p0.z * (0.028 + 0.010 * noise0) + seed * 31.0 + noise1 * 9.0;
    float plasma_phase1 = pc.p0.z * (0.022 + 0.012 * pocket) + seed * 17.0 + noise2 * 13.0;
    float plasma_u = sin(plasma_phase0) * cos(plasma_phase1 * 0.7);
    float plasma_v = cos(plasma_phase1) * sin(plasma_phase0 * 0.6);
    float plasma_amp = 0.0035 + 0.0065 * pocket + 0.0030 * noise0;
    vec3 warped_local = normalize(
        local +
        t0 * (jitter_u + plasma_u * plasma_amp) +
        t1 * (jitter_v + plasma_v * plasma_amp)
    );
    vec3 dir_view = quat_rotate(pc.p3, warped_local);
    float front = clamp(dir_view.z, 0.0, 1.0);
    float horizon = 1.0 - front;
    float rim_mask = smoothstep(0.24, 0.92, horizon);
    float static_shell_lift = shell_base * 1.46 +
                              pocket * 0.022 +
                              max(0.0, noise2 - 0.58) * 0.030;
    float plasma_lift = (0.003 + 0.010 * pocket) * (0.5 + 0.5 * plasma_u * plasma_v);
    float shell_lift = static_shell_lift +
                       plasma_lift +
                       rim_mask * (0.036 + 0.088 * noise0);
    vec2 surface_xy = dir_view.xy;
    float warm_t = clamp(dir_view.y * 0.5 + 0.5, 0.0, 1.0);
    float lift_t = clamp((shell_lift + 0.06) / 0.16, 0.0, 1.0);
    vec3 base_col = palette_color(warm_t, lift_t);
    vec2 screen = pc.p2.xy + surface_xy * pc.p2.z;
    float grad_u = stream_t;
    float grad_v = stream_mode;
    vec3 pseudo_local = normalize(warped_local - t0 * grad_u * 0.24 - t1 * grad_v * 0.24);
    vec3 pseudo_view = quat_rotate(pc.p3, pseudo_local);
    vec3 light_dir = normalize(vec3(-0.55, 0.42, 0.72));
    float diffuse = clamp(dot(pseudo_view, light_dir) * 0.5 + 0.5, 0.0, 1.0);
    float shade = clamp(0.08 + 0.92 * pow(diffuse, 1.45), 0.08, 1.0);
    float ridge_mask = smoothstep(
        0.74,
        0.96,
        noise0 * 0.58 + pocket * 0.28 + height_t * 0.14 + max(0.0, noise2 - 0.35) * 0.10
    );
    float ridge_hot = ridge_mask *
                      smoothstep(0.54, 0.94, diffuse) *
                      step(dither, 0.06 + ridge_mask * 0.16);
    vec3 ridge_col = mix(pc.low.rgb, pc.top.rgb, 0.28 + 0.56 * height_t);

    vec2 flow = normalize(vec2(
        dir_view.y * 0.58 + (noise1 - 0.5) * 0.75,
        -dir_view.x * 0.58 + (noise2 - 0.5) * 0.75
    ));
    if (length(flow) < 1e-4) {
        flow = vec2(1.0, 0.0);
    }

    float radius = 0.0;
    float alpha = 0.0;
    float stretch = 0.0;
    vec2 axis = flow;
    float visual_kind = 0.0;
    bool hidden = (dir_view.z <= 0.0);
    float size_metric = 0.0;

    if (variant < 0.5) {
        hidden = hidden || (dither > (0.78 + pocket * 0.10 - lift_t * 0.04 + ridge_hot * 0.05));
        radius = pc.p2.w * (2.35 + 1.80 * pow(front, 0.32) + 0.62 * pocket + 0.82 * lift_t);
        size_metric = clamp((radius / max(pc.p2.w, 0.001) - 1.2) / 3.3, 0.0, 1.0);
        base_col = terrain_solar_color(height_t, rough_t);
        base_col *= (0.16 + 0.60 * shade) * (0.92 + 0.18 * (0.5 + 0.5 * plasma_u));
        base_col = mix(base_col, ridge_col, ridge_hot * 0.92);
        alpha = clamp(0.03 + 0.16 * pow(front, 0.58) + 0.08 * pocket + 0.06 * lift_t + ridge_hot * 0.18, 0.03, 0.40);
        stretch = 0.01 + 0.03 * pocket;
        visual_kind = 0.0;
    } else if (variant < 1.5) {
        radius = pc.p2.w * (5.2 + 4.2 * pocket + 3.2 * horizon + 1.6 * lift_t);
        size_metric = clamp((radius / max(pc.p2.w, 0.001) - 1.5) / 6.4, 0.0, 1.0);
        base_col = mix(terrain_solar_color(height_t, rough_t), pc.top.rgb, 0.06 + 0.08 * smoothstep(0.84, 1.0, height_t));
        base_col *= (0.08 + 0.36 * shade) * (0.88 + 0.22 * (0.5 + 0.5 * plasma_v));
        alpha = clamp(0.012 + 0.030 * pocket * (0.24 + 0.76 * front), 0.01, 0.06);
        stretch = 0.06 + 0.05 * pocket;
        visual_kind = 2.0;
    } else if (variant < 2.5) {
        float clump = pc.p2.w * (0.8 + 1.8 * pocket);
        screen += vec2(flow.y, -flow.x) * clump * vec2(noise1 - 0.5, noise2 - 0.5) * 1.7;
        radius = pc.p2.w * (1.46 + 1.18 * pocket + 0.34 * lift_t);
        size_metric = clamp((radius / max(pc.p2.w, 0.001) - 0.8) / 2.8, 0.0, 1.0);
        base_col = mix(terrain_solar_color(height_t, rough_t), pc.top.rgb, 0.05);
        base_col *= (0.12 + 0.44 * shade) * (0.90 + 0.18 * (0.5 + 0.5 * plasma_u * plasma_v));
        base_col = mix(base_col, ridge_col, ridge_hot * 0.32);
        alpha = clamp(0.03 + 0.07 * pocket, 0.02, 0.12);
        stretch = 0.01;
        visual_kind = 0.0;
    } else if (variant < 3.5) {
        float radial_len = length(surface_xy);
        vec2 radial = (radial_len > 1e-4) ? (surface_xy / radial_len) : vec2(1.0, 0.0);
        vec2 tangent = vec2(-radial.y, radial.x);
        float corona = rim_mask *
                       smoothstep(0.42, 0.86, pocket) *
                       smoothstep(0.18, 0.74, noise1);
        float erupt_boost = 0.0;
        float erupt_cluster = smoothstep(0.78, 0.96, pocket * 0.56 + noise0 * 0.24 + noise1 * 0.20);
        hidden = hidden || (corona <= 0.14);
        axis = normalize(radial + tangent * vec2((noise1 - 0.5) * 0.92, (noise2 - 0.5) * 0.92));
        if (length(axis) < 1e-4) {
            axis = radial;
        }
        screen += radial * (pc.p2.w * (0.004 + 0.012 * corona * (0.45 + 0.55 * pocket) + 0.002 * lift_t));
        screen += tangent * ((seed - 0.5) * (5.0 + 7.0 * corona));
        {
            float erupt_seed = fract(seed * 53.17 + noise0 * 11.0 + noise2 * 7.0);
            if (erupt_seed > 0.76 && erupt_cluster > 0.08) {
                float erupt_phase = fract(pc.p0.z * (0.090 + 0.070 * noise1) + erupt_seed * 17.0);
                float pulse = smoothstep(0.00, 0.06, erupt_phase) * (1.0 - smoothstep(0.10, 0.58, erupt_phase));
                float eject = pulse * erupt_cluster * (0.030 + 0.090 * corona + 0.055 * pocket);
                float fan = (fract(seed * 97.1 + noise1 * 13.0 + noise2 * 5.0) - 0.5);
                screen += radial * (pc.p2.w * eject);
                screen += tangent * (pc.p2.w * fan * (0.004 + 0.018 * erupt_cluster + 0.020 * pulse));
                axis = normalize(radial + tangent * fan * (0.10 + 1.20 * erupt_cluster + 0.90 * pulse));
                erupt_boost = pulse * erupt_cluster;
                alpha += pulse * erupt_cluster * 0.16;
            }
        }
        radius = pc.p2.w * (2.6 + 3.6 * pocket + 5.8 * corona + 1.0 * lift_t + 2.2 * erupt_boost);
        size_metric = clamp((radius / max(pc.p2.w, 0.001) - 1.1) / 6.8, 0.0, 1.0);
        base_col = mix(
            terrain_solar_color(height_t, rough_t),
            pc.top.rgb,
            0.10 + 0.10 * corona + 0.20 * erupt_boost
        );
        base_col *= 0.18 + 0.52 * shade;
        alpha = clamp(0.02 + 0.10 * corona * (0.35 + 0.65 * pocket) + 0.08 * erupt_boost, 0.01, 0.18);
        stretch = 0.42 + 0.58 * corona + erupt_boost * 1.10;
        visual_kind = 1.0;
    }

    if (hidden || radius <= 0.0 || alpha <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        v_col = vec4(0.0);
        v_uv = vec2(0.0);
        v_kind = 0.0;
        v_phase = seed;
        v_stretch = 0.0;
        return;
    }

    if (variant < 3.5) {
        base_col = desaturate_color(base_col, 0.18);
    }

    vec2 axis_n = normalize(axis);
    vec2 perp = vec2(-axis_n.y, axis_n.x);
    float tail_scale = 1.0;
    float head_scale = 1.0;
    float cross_scale = 1.0;

    if (visual_kind < 0.5) {
        tail_scale = 1.0 + stretch * 0.20;
        head_scale = 1.0 + stretch * 0.34;
        cross_scale = 1.0 - stretch * 0.10;
    } else if (visual_kind < 1.5) {
        tail_scale = 1.0 + stretch * 0.25;
        head_scale = 1.0 + stretch * 1.20;
        cross_scale = 0.72;
    } else {
        tail_scale = 1.0 + stretch * 0.28;
        head_scale = 1.0 + stretch * 0.52;
        cross_scale = 1.06 + stretch * 0.10;
    }

    float along = corner.x * radius * ((corner.x < 0.0) ? tail_scale : head_scale);
    float across = corner.y * radius * cross_scale;
    vec2 p = screen + axis_n * along + perp * across;
    vec2 ndc;
    ndc.x = (p.x / pc.p0.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (p.y / pc.p0.y) * 2.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    v_col = vec4(base_col, alpha);
    v_uv = vec2(corner.x * ((corner.x < 0.0) ? tail_scale : head_scale), corner.y * cross_scale);
    v_kind = visual_kind;
    v_phase = seed;
    v_stretch = stretch;
}
