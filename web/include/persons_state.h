#ifndef PERSONS_STATE_H
#define PERSONS_STATE_H

#include <stddef.h>

typedef struct {
    int count;
    long timestamp;
} PersonSnapshot;

typedef struct {
    PersonSnapshot items[5];
    int count;
} PersonHistory;

typedef struct {
    long total_human_events;
    int stored;
    int capacity;
} BlackBoxStats;

int read_persons_snapshot(PersonSnapshot *out);
int read_persons_history(PersonHistory *out);
int read_blackbox_stats(BlackBoxStats *out);

#endif
