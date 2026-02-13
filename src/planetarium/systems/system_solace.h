#ifndef V_TYPE_PLANETARIUM_SYSTEM_SOLACE_H
#define V_TYPE_PLANETARIUM_SYSTEM_SOLACE_H

#include "../commander_nick_dialogues.h"
#include "../planetarium_types.h"

static const planet_def k_planets_solace[] = {
    {
        .id = 0,
        .display_name = "THETA PRI",
        .orbit_lane = 0,
        .lore = {
            .contract_title = "THETA PRI CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "RELAY SHELL OVERRUN BY SCAV DRONES.",
                "NICK: CLEAR LANES, RECOVER NAV SHARDS.",
                "PROFILE: INTERCEPTOR HEAVY, LOW ARMOR."
            },
            .mission_paragraphs = {
                "THETA PRIS RELAY SHELL HAS COLLAPSED INTO CHAOS. SCAV DRONES NOW CHOKE APPROACH LANES AND JAM CIVIL TRAFFIC. WE NEED A CLEAN CORRIDOR BEFORE THE NEXT CONVOY WINDOW.",
                "PRIMARY OBJECTIVE IS NAV SHARD RECOVERY FROM THREE BROKEN BEACONS. SECONDARY OBJECTIVE IS TO CUT DRONE REINFORCEMENT PATHS. EXPECT FAST CONTACTS AND LITTLE ARMOR ON TARGETS.",
                "HOLD FORMATION, PRESERVE FUEL, AND TAKE PRECISE SHOTS. THIS FIGHT REWARDS DISCIPLINE MORE THAN BRUTE FORCE. CLEAR IT CLEAN, AND THE WHOLE FRONT BREATHES EASIER."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 0
        }
    },
    {
        .id = 1,
        .display_name = "KELVIN ARC",
        .orbit_lane = 1,
        .lore = {
            .contract_title = "KELVIN ARC CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "FRACTURED MINING ARC, UNSTABLE GRIDS.",
                "NICK: STRIKE BEFORE DEFENSES RE ARM.",
                "PROFILE: MEDIUM THREAT, BURST FIGHTS."
            },
            .mission_paragraphs = {
                "KELVIN ARCS MINING RING IS FRACTURED AND SPITTING CHARGED DEBRIS. AUTO-TURRETS REBOOT IN STAGGERED CYCLES EVERY FEW MINUTES. HIT THEM DURING DARK PHASES BEFORE THE GRID REARMS.",
                "TARGET PRIORITY IS POWER RELAYS FEEDING THE DEFENSE NET. ONCE RELAYS DROP, ESCORT GROUPS LOSE COORDINATION QUICK. YOU WILL SEE BURSTY CONTACT WAVES, THEN SHORT QUIET GAPS.",
                "KEEP YOUR VECTOR CLEAN THROUGH BROKEN MINING SPINES. DO NOT CHASE STRAGGLERS INTO BLIND ANGLES. CONTROL THE CENTER AND LET THE FIGHT COME TO YOU."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 2
        }
    },
    {
        .id = 2,
        .display_name = "NOVA REACH",
        .orbit_lane = 2,
        .lore = {
            .contract_title = "NOVA REACH CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "TRADE SPINE DARK AFTER CONVOY LOSSES.",
                "NICK: REOPEN ESCORT CORRIDOR FAST.",
                "PROFILE: MIXED FORMATIONS, STEADY HEAT."
            },
            .mission_paragraphs = {
                "NOVA REACH IS A DEAD TRADE SPINE AFTER THREE CONVOY LOSSES. BEACON CHAINS ARE DARK, AND MERCHANTS ARE STRANDED OFF-LANE. WE REOPEN THE ESCORT CORRIDOR OR THIS SECTOR STARVES.",
                "EXPECT MIXED ENEMY FORMATIONS WITH ROTATING SCREEN SHIPS. THEY WILL TRY TO DRAG YOU LONG ENOUGH FOR HEAVY ARRIVALS. BREAK THEIR TIMELINE BY CUTTING LEAD ELEMENTS EARLY.",
                "MAINTAIN MID-RANGE PRESSURE AND PROTECT THE CORRIDOR AXIS. IF YOU OVEREXTEND, THEY WRAP AROUND YOUR REAR FAST. THIS IS A CONTROL FIGHT, NOT A CHASE."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 5
        }
    },
    {
        .id = 3,
        .display_name = "VANTA BELT",
        .orbit_lane = 3,
        .lore = {
            .contract_title = "VANTA BELT CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "DERELICT BELT MASKING HOT SIGNATURES.",
                "NICK: TRUST SCANS OVER VISUAL NOISE.",
                "PROFILE: AMBUSH VECTORS, COLLISION RISK."
            },
            .mission_paragraphs = {
                "VANTA BELT IS A DERELICT GRAVEYARD FULL OF FALSE RETURNS. THERMAL BLOOMS BOUNCE OFF HULL SCRAP AND MASK LIVE CONTACTS. YOU WILL FLY HALF-BLIND IF YOU TRUST RAW VISUALS.",
                "AMBUSH GROUPS FAVOR CROSSING ANGLES INSIDE DENSE DEBRIS. THEIR FIRST VOLLEY HITS HARD, THEN THEY BREAK APART. SURVIVE THE OPENING TEN SECONDS AND THE FIGHT TURNS.",
                "RUN TIGHT SCAN DISCIPLINE AND LEAVE ROOM TO EVADE. COLLISION IS AS LETHAL AS ANY ENEMY GUN OUT HERE. CALM CONTROL BEATS AGGRESSION IN THIS BELT."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 8
        }
    },
    {
        .id = 4,
        .display_name = "EMBER VEIL",
        .orbit_lane = 4,
        .lore = {
            .contract_title = "EMBER VEIL CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "THERMAL FRONTS AROUND OLD HABITATS.",
                "NICK: TARGET BLOOMS, AVOID DEAD ZONES.",
                "PROFILE: AGGRESSIVE PACKS, FAST WAVES."
            },
            .mission_paragraphs = {
                "EMBER VEILS HABITAT RING IS WRAPPED IN THERMAL SHEAR. HOT FRONTS HIDE ENEMY APPROACHES UNTIL THEY ARE ON TOP OF YOU. DEAD ZONES WILL BLIND BOTH TEAMS IF YOU DRIFT INTO THEM.",
                "HOSTILE PACKS HERE PUSH FAST AND TRY TO OVERWHELM SHIELDS. THEY CYCLE AGGRESSION IN SHORT, VIOLENT WAVES. PICK OFF LEADERS TO BREAK THEIR ATTACK RHYTHM.",
                "KEEP MOVING BETWEEN SAFE TEMPERATURE WINDOWS. IF YOU STALL IN A HOT POCKET, YOU LOSE INITIATIVE IMMEDIATELY. CONTROL HEAT, CONTROL THE MISSION."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 11
        }
    },
    {
        .id = 5,
        .display_name = "ORBITAL IX",
        .orbit_lane = 5,
        .lore = {
            .contract_title = "ORBITAL IX CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "OUTER PATROL RING FEEDING COMMAND.",
                "NICK: SEVER UPLINKS BEFORE REINFORCE.",
                "PROFILE: COMMAND ESCORTS, SHARP RETURN."
            },
            .mission_paragraphs = {
                "ORBITAL IX IS THE OUTER COMMAND PATROL RING. UPLINK NODES HERE FEED TARGETING DATA ACROSS THE WHOLE FRONT. CUT THESE LINKS AND THE ENEMY RESPONSE SLOWS EVERYWHERE.",
                "EXPECT HARD ESCORTS AROUND EACH RELAY CLUSTER. THEY WILL DISENGAGE, REFORM, THEN RETURN SHARPER. DO NOT LET THEM RESET FOR FREE.",
                "STRIKE THE UPLINKS FIRST, THEN CLEAN UP ESCORTS METHODICALLY. THIS IS A PRECISION CONTRACT WITH STRATEGIC PAYOFF. FINISH IT, AND THE BOSS GATE OPENS ON OUR TERMS."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 14
        }
    }
};

static const planetary_system_def k_system_solace = {
    .id = 0,
    .display_name = "SOLACE",
    .commander_name = "COMMANDER NICK",
    .commander_callsign = "NICK",
    .planets = k_planets_solace,
    .planet_count = (int)(sizeof(k_planets_solace) / sizeof(k_planets_solace[0])),
    .boss_gate_label = "BOSS GATE",
    .boss_gate_locked_text = "BOSS LOCKED: QUELL ALL SYSTEMS FIRST",
    .boss_gate_ready_text = "BOSS CONTRACT ARMED: LAUNCHING SORTIE",
    .marquee_text = "PLANETARIUM CONTRACT GRID  "
};

#endif
