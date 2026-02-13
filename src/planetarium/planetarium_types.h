#ifndef V_TYPE_PLANETARIUM_TYPES_H
#define V_TYPE_PLANETARIUM_TYPES_H

#ifndef PLANETARIUM_MAX_SYSTEMS
#define PLANETARIUM_MAX_SYSTEMS 8
#endif

typedef struct planet_lore_block {
    const char* contract_title;
    const char* status_pending;
    const char* status_quelled;
    const char* briefing_lines[3];
    const char* mission_paragraphs[3];
    int mission_paragraph_count;
    int commander_message_id;
} planet_lore_block;

typedef struct planet_def {
    int id;
    const char* display_name;
    int orbit_lane;
    planet_lore_block lore;
} planet_def;

typedef struct planetary_system_def {
    int id;
    const char* display_name;
    const char* commander_name;
    const char* commander_callsign;
    const planet_def* planets;
    int planet_count;
    const char* boss_gate_label;
    const char* boss_gate_locked_text;
    const char* boss_gate_ready_text;
    const char* marquee_text;
} planetary_system_def;

#endif
