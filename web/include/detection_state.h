#ifndef DETECTION_STATE_H
#define DETECTION_STATE_H

#include <stddef.h>

/* YOLO/face detection: when OFF, stream may still show camera frames without AI. */
int detection_is_enabled(void);
int detection_set_enabled(int enabled); /* 1=on, 0=off; returns new state */
int detection_get_json(char *buf, size_t buflen);

#endif
