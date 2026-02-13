#include "planetarium_registry.h"

#include "systems/system_solace.h"
#include "systems/system_cinder.h"

#include <stddef.h>

static const planetary_system_def* k_systems[] = {
    &k_system_solace,
    &k_system_cinder
};

int planetarium_get_system_count(void) {
    return (int)(sizeof(k_systems) / sizeof(k_systems[0]));
}

const planetary_system_def* planetarium_get_system(int idx) {
    if (idx < 0 || idx >= planetarium_get_system_count()) {
        return NULL;
    }
    return k_systems[idx];
}
