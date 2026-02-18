# LevelDef Schema (Draft v1)

`LevelDef` is a hand-editable text config used to define level behavior without code changes.

- Directory: `data/levels/`
- Load order:
- `combat.cfg`
- `boids.cfg`
- `level_defender.cfg`
- `level_enemy_radar.cfg`
- `level_event_horizon.cfg`
- `level_event_horizon_legacy.cfg`
- `level_high_plains_drifter.cfg`
- `level_high_plains_drifter_2.cfg`
- `level_fog_of_war.cfg`
- Format: INI-like sections + `key=value`
- Comments: lines beginning with `#`

## Sections

- `[combat]`
- `[boid_profile NAME]`
- `[level LEVEL_STYLE_NAME]`

`LEVEL_STYLE_NAME` must be one of:

- `DEFENDER`
- `ENEMY_RADAR`
- `EVENT_HORIZON`
- `EVENT_HORIZON_LEGACY`
- `HIGH_PLAINS_DRIFTER`
- `HIGH_PLAINS_DRIFTER_2`
- `FOG_OF_WAR`

## `combat` Keys

- `swarm_armed_prob_start` float
- `swarm_armed_prob_end` float
- `swarm_spread_prob_start` float
- `swarm_spread_prob_end` float
- `progression_wave_weight` float
- `progression_score_weight` float
- `progression_level_weight` float
- `armed_probability_base_formation` float
- `armed_probability_base_swarm` float
- `armed_probability_base_kamikaze` float
- `armed_probability_progression_bonus_formation` float
- `armed_probability_progression_bonus_swarm` float
- `armed_probability_progression_bonus_kamikaze` float
- `fire_range_min` float
- `fire_range_max_base` float
- `fire_range_max_progression_bonus` float
- `aim_error_deg_start` float
- `aim_error_deg_end` float
- `cooldown_scale_start` float
- `cooldown_scale_end` float
- `projectile_speed_scale_start` float
- `projectile_speed_scale_end` float
- `spread_scale_start` float
- `spread_scale_end` float
- `weapon.pulse.*`, `weapon.spread.*`, `weapon.burst.*`:
- `cooldown_min_s`, `cooldown_max_s`, `burst_count`, `burst_gap_s`
- `projectiles_per_shot`, `spread_deg`, `projectile_speed`
- `projectile_ttl_s`, `projectile_radius`, `aim_lead_s`

## `boid_profile` Keys

- `wave_name` string
- `count` int
- `sep_w` float
- `ali_w` float
- `coh_w` float
- `avoid_w` float
- `goal_w` float
- `sep_r` float
- `ali_r` float
- `coh_r` float
- `goal_amp` float
- `goal_freq` float
- `wander_w` float
- `wander_freq` float
- `steer_drag` float
- `max_speed` float
- `accel` float
- `radius_min` float
- `radius_max` float
- `spawn_x01` float
- `spawn_x_span` float
- `spawn_y01` float
- `spawn_y_span` float

## `level` Keys

- `render_style` enum:
- `defender`
- `cylinder`
- `drifter`
- `drifter_shaded`
- `fog`
- `wave_mode` enum: `normal` or `boid_only`
- `spawn_mode` enum:
- `sequenced_clear` (spawn next wave only when clear + cooldown elapsed)
- `timed` (spawn on interval regardless of clear state)
- `timed_sequenced` (spawn on interval when clear)
- `spawn_interval_s` float (required for `timed` and `timed_sequenced`)
- `default_boid_profile` profile name
- `wave_cooldown_initial_s` float
- `wave_cooldown_between_s` float
- `bidirectional_spawns` int (`0`/`1`; when `1`, cylinder levels can spawn waves from either direction)
- `cylinder_double_swarm_chance` float (`0..1`, chance to spawn mirrored second swarm on cylinder levels)
- `exit_enabled` int (`0`/`1`) for side-scrolling level portal
- `exit_x01` float (portal x in world-width units; `1.0` = one screen width)
- `exit_y01` float (portal y normalized by world height)
- `boid_cycle` comma-separated profile names (used for `boid_only` wave cycling)
- `wave_cycle` comma-separated wave pattern tokens (used for `normal` mode):
- `sine_snake`
- `v_formation`
- `swarm`
- `kamikaze`
- `sine.count`
- `sine.start_x01`
- `sine.spacing_x`
- `sine.home_y01`
- `sine.phase_step`
- `sine.form_amp`
- `sine.form_freq`
- `sine.break_delay_base`
- `sine.break_delay_step`
- `sine.max_speed`
- `sine.accel`
- `v.count`
- `v.start_x01`
- `v.spacing_x`
- `v.home_y01`
- `v.home_y_step`
- `v.phase_step`
- `v.form_amp`
- `v.form_freq`
- `v.break_delay_min`
- `v.break_delay_rand`
- `v.max_speed`
- `v.accel`
- `kamikaze.count`
- `kamikaze.start_x01`
- `kamikaze.spacing_x`
- `kamikaze.y_margin`
- `kamikaze.max_speed`
- `kamikaze.accel`
- `kamikaze.radius_min`
- `kamikaze.radius_max`
- `searchlight` repeated CSV rows

### `searchlight` CSV Field Order

`anchor_x01,anchor_y01,length_h01,half_angle_deg,sweep_center_deg,sweep_amplitude_deg,sweep_speed,sweep_phase_deg,sweep_motion,source_type,source_radius,clear_grace_s,fire_interval_s,projectile_speed,projectile_ttl_s,projectile_radius,aim_jitter_deg`

- `sweep_motion`: `pendulum`, `linear`, `spin`
- `source_type`: `dome`, `orb`

## Notes

- Each level file should contain only one `[level ...]` section.
- Keep profile names in level files aligned with names defined in `boids.cfg`.
- The loader is strict: missing level files or missing required keys fail leveldef validation.
