#ifndef V_TYPE_PLANETARIUM_SYSTEM_CINDER_H
#define V_TYPE_PLANETARIUM_SYSTEM_CINDER_H

#include "../commander_nick_dialogues.h"
#include "../planetarium_types.h"

static const planet_def k_planets_cinder[] = {
    {
        .id = 0,
        .display_name = "ASH CROWN",
        .orbit_lane = 0,
        .lore = {
            .contract_title = "ASH CROWN CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "VOLCANIC ORBIT STATIONS NOW HOSTILE.",
                "NICK: STRIP TURRETS BEFORE CORE RUN.",
                "PROFILE: CLOSE DENSITY, HIGH HEAT."
            },
            .mission_paragraphs = {
                "ASH CROWN'S ORBIT STATIONS HAVE TURNED INTO GUN NESTS.\\nPLASMA VENTS FLASH HOT AND FORCE CONSTANT VECTOR CHANGES.\\nYOU WILL WORK CLOSE AND FAST IN A TIGHT KILL ZONE.",
                "FIRST TARGETS ARE OUTER TURRET CROWNS FEEDING THE CORE SHIELD.\\nONCE THOSE GO DARK, STRIKE WINDOWS OPEN BRIEFLY.\\nTIME YOUR RUNS OR YOU EAT THE FULL BATTERY.",
                "KEEP HEAT LOAD MANAGEABLE AND DO NOT PARK ON TARGET.\\nTHIS CONTRACT REWARDS SHORT ATTACK CYCLES AND CLEAN EXITS.\\nBURN SMART AND THE CROWN COLLAPSES."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 6
        }
    },
    {
        .id = 1,
        .display_name = "EMBER SPINE",
        .orbit_lane = 1,
        .lore = {
            .contract_title = "EMBER SPINE CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "REFINERY BELTS FEED ENEMY DRONE FABS.",
                "NICK: CUT FUEL LINES, BREAK CADENCE.",
                "PROFILE: SWARM WAVES, FAST RESPAWN."
            },
            .mission_paragraphs = {
                "EMBER SPINE REFINERY BELTS FEED THE DRONE FABRICATION GRID.\\nAS LONG AS FUEL FLOWS, SWARM WAVES REBUILD TOO FAST.\\nWE CUT LOGISTICS HERE OR FIGHT FOREVER.",
                "PRIORITIZE PIPELINE JUNCTIONS AND STORAGE MANIFOLDS.\\nEACH CUT REDUCES BOTH ENEMY COUNT AND RESPOND RATE.\\nEXPECT RAPID COUNTER-RAIDS AFTER EVERY SUCCESSFUL STRIKE.",
                "DO NOT GET FIXATED ON ENDLESS DRONE CHAFF.\\nKILL THE SOURCE, NOT JUST THE SYMPTOMS.\\nBREAK THEIR CADENCE AND THE FIELD STABILIZES."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 15
        }
    },
    {
        .id = 2,
        .display_name = "GLASS EXPANSE",
        .orbit_lane = 2,
        .lore = {
            .contract_title = "GLASS EXPANSE CONTRACT",
            .status_pending = "PENDING",
            .status_quelled = "QUELLED",
            .briefing_lines = {
                "REFRACTIVE FIELDS DISTORT LONG SHOTS.",
                "NICK: HOLD FORMATION, BAIT FLANKS.",
                "PROFILE: ELITE ESCORTS, HEAVY BURSTS."
            },
            .mission_paragraphs = {
                "GLASS EXPANSE IS A REFRACTIVE KILLBOX WHERE LONG SHOTS LIE.\\nTARGET ECHOES SPLIT, DRIFT, AND SNAP BACK WITHOUT WARNING.\\nONLY CLOSE VERIFICATION PREVENTS WASTED VOLLEYS.",
                "ELITE ESCORTS WILL FEINT WIDE TO DRAW YOUR ANGLE OFF.\\nWHEN YOU OVERCOMMIT, HEAVY BURSTS ARRIVE ON THE TRUE VECTOR.\\nHOLD FORMATION AND FORCE THEM INTO KNOWN LANES.",
                "THIS CONTRACT IS ABOUT PATIENCE UNDER PRESSURE.\\nTRACK, CONFIRM, THEN FIRE WITH INTENT.\\nDISCIPLINE HERE OPENS THE CINDER BOSS CORRIDOR."
            },
            .mission_paragraph_count = 3,
            .commander_message_id = 19
        }
    }
};

static const planetary_system_def k_system_cinder = {
    .id = 1,
    .display_name = "CINDER",
    .commander_name = "COMMANDER NICK",
    .commander_callsign = "NICK",
    .planets = k_planets_cinder,
    .planet_count = (int)(sizeof(k_planets_cinder) / sizeof(k_planets_cinder[0])),
    .boss_gate_label = "BOSS GATE",
    .boss_gate_locked_text = "CINDER BOSS LOCKED: QUELL ALL PLANETS",
    .boss_gate_ready_text = "CINDER BOSS CONTRACT READY",
    .marquee_text = "CINDER FRONT CONTRACT GRID  "
};

#endif
