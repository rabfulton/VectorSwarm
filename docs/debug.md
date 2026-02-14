# Debug Controls

## Terrain Tuning (High Plains Drifter 2)

Terrain tuning controls are disabled by default.

Enable them by setting:

`VTYPE_TERRAIN_TUNING=1`

Example:

```bash
VTYPE_TERRAIN_TUNING=1 ./build/VectorSwarm
```

When enabled, controls are active only on `HIGH_PLAINS_DRIFTER_2`, and only when no menu overlay is open.

### Numpad Controls

- `KP7 / KP4`: hue shift up/down
- `KP8 / KP5`: brightness up/down
- `KP9 / KP6`: alpha (opacity) up/down
- `KP2 / KP1`: normal variation up/down
- `KP3 / KP0`: depth fade up/down
- `KP*`: toggle terrain tuning HUD
- `KP.`: reset terrain tuning values to defaults
- `KP Enter`: print current values to stdout (for hardcoding)

### Notes

- The HUD line shows live values while tuning is enabled.
- `KP Enter` prints both human-readable values and a hardcode-ready line for `pc.params[3]` / `pc.tune[]`.

## Particle Tuning / Trace

Particle trace and live explosion-particle tuning are disabled by default.

Enable them by setting:

`VTYPE_PARTICLE_TRACE=1`

Example:

```bash
VTYPE_PARTICLE_TRACE=1 ./build/VectorSwarm
```

When enabled, the game prints periodic particle stats to stdout and numpad tuning keys are active during gameplay (when no menu overlay is open).

### Numpad Controls

- `KP7 / KP4`: particle core gain up/down
- `KP8 / KP5`: particle trail gain up/down
- `KP9 / KP6`: particle heat cooling up/down
- `KP*`: toggle particle tuning HUD
- `KP.`: reset particle tuning defaults
- `KP Enter`: print current particle tuning values to stdout

### Notes

- Particle tuning keys are checked before terrain tuning keys.
- If both `VTYPE_PARTICLE_TRACE=1` and `VTYPE_TERRAIN_TUNING=1` are set, overlapping numpad keys control particle tuning first.
