#ifndef V_TYPE_PLANETARIUM_REGISTRY_H
#define V_TYPE_PLANETARIUM_REGISTRY_H

#include "planetarium_types.h"

int planetarium_get_system_count(void);
const planetary_system_def* planetarium_get_system(int idx);

#endif
