#include "planetarium_validate.h"

#include "planetarium_registry.h"

#include "planetarium_types.h"

static int valid_nonempty(const char* s) {
    return (s && s[0] != '\0') ? 1 : 0;
}

int planetarium_validate_registry(FILE* out) {
    int ok = 1;
    const int system_count = planetarium_get_system_count();
    if (system_count <= 0 || system_count > PLANETARIUM_MAX_SYSTEMS) {
        if (out) {
            fprintf(out, "planetarium validation: invalid system_count=%d (max=%d)\n", system_count, PLANETARIUM_MAX_SYSTEMS);
        }
        ok = 0;
    }

    for (int si = 0; si < system_count; ++si) {
        const planetary_system_def* sys = planetarium_get_system(si);
        if (!sys) {
            if (out) {
                fprintf(out, "planetarium validation: system[%d] is null\n", si);
            }
            ok = 0;
            continue;
        }
        if (!valid_nonempty(sys->display_name)) {
            if (out) {
                fprintf(out, "planetarium validation: system[%d] display_name missing\n", si);
            }
            ok = 0;
        }
        if (!sys->planets || sys->planet_count <= 0 || sys->planet_count > PLANETARIUM_MAX_SYSTEMS) {
            if (out) {
                fprintf(out, "planetarium validation: system[%d] has invalid planet_count=%d\n", si, sys->planet_count);
            }
            ok = 0;
            continue;
        }
        if (!valid_nonempty(sys->boss_gate_label)) {
            if (out) {
                fprintf(out, "planetarium validation: system[%d] boss_gate_label missing\n", si);
            }
            ok = 0;
        }
        for (int pi = 0; pi < sys->planet_count; ++pi) {
            const planet_def* p = &sys->planets[pi];
            if (!valid_nonempty(p->display_name)) {
                if (out) {
                    fprintf(out, "planetarium validation: system[%d] planet[%d] display_name missing\n", si, pi);
                }
                ok = 0;
            }
            if (!valid_nonempty(p->lore.contract_title)) {
                if (out) {
                    fprintf(out, "planetarium validation: system[%d] planet[%d] contract_title missing\n", si, pi);
                }
                ok = 0;
            }
            if (p->lore.mission_paragraph_count < 1 || p->lore.mission_paragraph_count > 3) {
                if (out) {
                    fprintf(out, "planetarium validation: system[%d] planet[%d] mission_paragraph_count=%d invalid\n",
                            si, pi, p->lore.mission_paragraph_count);
                }
                ok = 0;
            }
            if (p->lore.commander_message_id < 0 || p->lore.commander_message_id >= 30) {
                if (out) {
                    fprintf(out, "planetarium validation: system[%d] planet[%d] commander_message_id=%d invalid\n",
                            si, pi, p->lore.commander_message_id);
                }
                ok = 0;
            }
            for (int li = 0; li < 3; ++li) {
                if (!valid_nonempty(p->lore.briefing_lines[li])) {
                    if (out) {
                        fprintf(out, "planetarium validation: system[%d] planet[%d] briefing_lines[%d] missing\n", si, pi, li);
                    }
                    ok = 0;
                }
            }
            for (int bi = 0; bi < p->lore.mission_paragraph_count && bi < 3; ++bi) {
                if (!valid_nonempty(p->lore.mission_paragraphs[bi])) {
                    if (out) {
                        fprintf(out, "planetarium validation: system[%d] planet[%d] mission_paragraphs[%d] missing\n",
                                si, pi, bi);
                    }
                    ok = 0;
                }
            }
        }
    }

    return ok;
}
