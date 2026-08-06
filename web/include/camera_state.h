#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

#include <stddef.h>

/* Explicit camera power: OFF until camera_on (not tied to webpage viewers). */
int camera_is_enabled(void);
int camera_set_enabled(int enabled); /* 1=on, 0=off; returns new state */
int camera_get_json(char *buf, size_t buflen);

#endif
