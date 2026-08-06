#ifndef GUARD_STATE_H
#define GUARD_STATE_H

#include <stddef.h>

/* Part 4.1 — anti-theft / guard mode (persisted). */
int guard_is_armed(void);
int guard_set_armed(int armed); /* 1=on, 0=off; returns new state */
int guard_get_json(char *buf, size_t buflen);

#endif
