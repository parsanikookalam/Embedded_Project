#ifndef FEATURE_FLAGS_H
#define FEATURE_FLAGS_H

#include <stddef.h>

/* Part 4 soft switches (persisted). Guard uses guard_state.h separately. */
int watchdog_is_enabled(void);
int watchdog_set_enabled(int on);
int watchdog_get_json(char *buf, size_t buflen);

int thermal_is_enabled(void);
int thermal_set_enabled(int on);
int thermal_get_json(char *buf, size_t buflen);

/* Combined snapshot for dashboard. */
int part4_status_json(char *buf, size_t buflen);

#endif
