# VectorSwarm Shipyard Plan (Living Document)

## 1. Intent

Create a **between-mission hub** called the `Shipyard` that supports two player styles:

- `Arcade Mode`: fast, one-click return to action.
- `Campaign Mode`: optional depth via mission selection, lore, contracts, and strategic progression.

This document is intentionally a **living plan**. We keep direction stable but stay flexible about implementation details, UI wording, pacing, and feature priorities as playtesting reveals better ideas.

---

## 2. Design Principles

1. **Action First**
   - Never block fast players behind extra menus.
   - `Continue Sortie` should always be obvious and immediate.

2. **Depth Optional**
   - Lore, contract branching, and strategy should be available but never mandatory in Arcade.

3. **Single Visual Language**
   - Keep the DefconDraw CRT/vector identity consistent across gameplay and Shipyard screens.

4. **Readable + Expressive**
   - Information density can be high, but hierarchy must remain clear.
   - Prioritize meters, dials, and diagrams over text walls.

5. **Systems That Feed Gameplay**
   - Stats/achievements should influence mission availability, rewards, and boss access.

---

## 3. Core Terminology

- `Threat Index`: replacement for score-like progression metric.
- `Contract`: mission card with objective, risk profile, and reward modifiers.
- `Sortie`: one mission run.
- `Commander Nick`: mission-briefing character (portrait + short dispatch text).

---

## 4. Mode Definitions

## 4.1 Arcade Mode

Primary promise: minimal friction, high momentum.

Flow:
1. Enter Shipyard.
2. See quick summary panel.
3. Press `Continue Sortie` (default focused action).
4. Launch next mission.

Optional:
- Open Planetarium / Contracts / Acoustics / Loadout, but no requirement.

Progression:
- Mostly linear wave progression with occasional milestone unlocks.
- Boss sorties unlocked by thresholds (Threat Index, quota, accuracy, survival).

## 4.2 Campaign Mode

Primary promise: strategic progression and world context.

Flow:
1. Enter Shipyard.
2. Open Planetarium.
3. Select planet/sector node.
4. Review Contract + Commander Nick briefing.
5. Accept and launch sortie.

Progression:
- Network of sectors with changing threat levels.
- Ignored sectors escalate over time.
- Completed sectors can unlock routes, modifiers, and special encounters.
- Boss missions become explicit contracts in high-pressure sectors.

---

## 5. Shipyard Information Architecture

Main Shipyard page should link to:

- `Continue Sortie` (prominent)
- `Planetarium` (mission selection / strategic map)
- `Loadout Bay` (handling + controls + ship upgrades)
- `Acoustics Bay` (existing synth controls for fire/thrust)
- `Records` (performance history, achievements, lore logs)

Recommended main layout:

- Left: navigation/actions (`Continue`, `Planetarium`, `Loadout`, `Acoustics`, `Records`)
- Center: ship schematic + current configuration summary
- Right: telemetry stack (`Threat Index`, Accuracy, Quota pace, Survival streak, recent milestones)
- Bottom ticker: Commander Nick dispatch / system chatter / unlock notices

---

## 6. Planetarium + Contracts

Planetarium should be the visual centerpiece of Campaign mode and optional in Arcade.

Interaction concept:
- Select a planet/node.
- Open a contract card overlay:
  - Contract codename
  - Threat class
  - Mission objective
  - Hazards/modifiers
  - Reward modifiers
  - Briefing text from Commander Nick

Asset plan:
- Use DefconDraw image/SVG support for planet symbols, faction marks, contract icons.
- Integrate `nick.jpg` portrait in briefing panel.

Contract examples:
- Intercept swarm route
- Defend relay spine
- Deep orbit recon (limited visibility)
- Elite hunt (mini-boss)

---

## 7. Progression + Gating Model

Use a hybrid progression model:

- Arcade:
  - Fast mission chain.
  - Optional contract reroutes.
- Campaign:
  - Node-based branching with persistent threat state.

Boss access logic (initial concept):
- Unlock when 2+ thresholds are met:
  - `Threat Index` minimum
  - Accuracy minimum
  - Survival streak minimum
  - Sector pressure minimum (Campaign only)

This gives skilled players access without forcing perfect play.

---

## 8. Stats to Track (Immediate)

Per-sortie:
- shots_fired
- shots_hit
- kills
- damage_taken
- survival_time_s
- mission_clear_state

Derived:
- accuracy = shots_hit / max(shots_fired, 1)
- threat_delta (per mission)
- performance_band (A/B/C etc, optional later)

Persistent:
- lifetime Threat Index
- rolling accuracy
- boss clear count
- citations earned

---

## 9. Achievement/Citation System (Phase 2)

Working framing: `Citations` (fits style better than generic achievements).

Examples:
- Precision Interceptor: high accuracy in a full sortie.
- Last Frame Standing: clear sortie at low vitality.
- Swarm Breaker: destroy N swarm targets rapidly.
- Silent Orbit: complete objective with minimal damage.

Rewards:
- visual badges in Records
- optional cosmetic vector trims
- selective gameplay unlocks (light touch, avoid power creep)

---

## 10. UX Pacing Rules

1. Shipyard actions should complete in <= 2 inputs for common loop.
2. Lore is short-form by default (1-3 lines), expandable on demand.
3. Avoid modal dead ends; every page has clear `Launch` or `Back`.
4. Keep input parity: keyboard-first, pointer-supported.

---

## 11. Technical Plan (Phased)

## Phase 1: Foundation (MVP)

- Add `game_mode`: `ARCADE` / `CAMPAIGN`.
- Add Shipyard app state with page routing.
- Implement main Shipyard screen:
  - Continue Sortie
  - Links to Acoustics + placeholder Loadout + placeholder Planetarium
- Add stat collection for accuracy and Threat Index.

## Phase 2: Planetarium + Contracts

- Implement node map screen and contract overlay.
- Add contract data model and selection flow.
- Add Commander Nick briefing panel using portrait image.

## Phase 3: Progression Logic

- Arcade quick chain.
- Campaign sector pressure and branching mission generation.
- Boss contract gating by thresholds.

## Phase 4: Records + Citations

- Records page with trend charts/dials.
- Citation unlock and display framework.

## Phase 5: Iteration Passes

- Rebalance metrics and unlock thresholds.
- Refine wording, mission flavor, and UI density.
- Improve visual storytelling with SVG/icon pass.

---

## 12. Data Model Sketch

```c
typedef enum game_mode {
    GAME_MODE_ARCADE = 0,
    GAME_MODE_CAMPAIGN = 1
} game_mode;

typedef struct combat_stats {
    int shots_fired;
    int shots_hit;
    int kills;
    int damage_taken;
    float survival_time_s;
} combat_stats;

typedef struct progression_state {
    int threat_index;
    float rolling_accuracy;
    int boss_unlock_flags;
    int citations_mask;
} progression_state;

typedef struct mission_contract {
    int id;
    int sector_id;
    int threat_class;
    int objective_type;
    float reward_mult;
    float risk_mult;
    char title[64];
    char briefing[256];
} mission_contract;
```

Data structure details can evolve during implementation.

---

## 13. Open Questions (Deliberately Unresolved)

- Exact formula for Threat Index gain/loss?
- Should mission failure reduce Threat Index or only stall growth?
- How many simultaneous contracts should appear in Campaign?
- Should Loadout adjustments be free or limited by a resource?
- Are citations purely cosmetic, or partially gameplay-affecting?

These are intentionally open and should be answered through playtesting.

---

## 14. Next Practical Steps

1. Implement Shipyard state shell with `Continue Sortie`.
2. Add persistent stat tracking fields for accuracy + Threat Index.
3. Build Planetarium placeholder screen with 3 dummy contracts.
4. Add Commander Nick portrait panel wiring.
5. Playtest loop timing for Arcade and Campaign entry flows.

