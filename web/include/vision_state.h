#ifndef VISION_STATE_H
#define VISION_STATE_H

#include <stddef.h>

/* Part 3-3: dynamic YOLO input size + target FPS (shared with Python detector). */
int vision_get_yolo_input(void);
int vision_get_target_fps(void);
int vision_set_yolo_input(int size);   /* 320 / 480 / 640 */
int vision_set_target_fps(int fps);    /* e.g. 10..30 */
int vision_get_json(char *buf, size_t buflen);

#endif
