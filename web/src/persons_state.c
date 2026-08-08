#include "persons_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define PERSONS_JSON_PATH "../data/persons.json"
#define HISTORY_DB_PATH "../data/history.db"
#define BLACKBOX_CAPACITY 500

int read_persons_snapshot(PersonSnapshot *out)
{
    if (!out)
        return -1;
    out->count = 0;
    out->timestamp = 0;

    FILE *fp = fopen(PERSONS_JSON_PATH, "r");
    if (!fp)
        return -1;

    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0)
        return -1;

    int count = 0;
    long ts = 0;
    char *p = strstr(buf, "\"count\"");
    if (p)
        sscanf(p, "\"count\"%*[^0-9-]%d", &count);
    p = strstr(buf, "\"timestamp\"");
    if (p)
        sscanf(p, "\"timestamp\"%*[^0-9-]%ld", &ts);

    out->count = count;
    out->timestamp = ts;
    return 0;
}

int read_persons_history(PersonHistory *out)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(HISTORY_DB_PATH, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return -1;
    }

    const char *sql =
        "SELECT count, timestamp FROM detections "
        "ORDER BY id DESC LIMIT 5;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < 5) {
        out->items[i].count = sqlite3_column_int(stmt, 0);
        out->items[i].timestamp = (long)sqlite3_column_int64(stmt, 1);
        i++;
    }
    out->count = i;

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

int read_blackbox_stats(BlackBoxStats *out)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->capacity = BLACKBOX_CAPACITY;

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(HISTORY_DB_PATH, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        return -1;
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='total_human_events';", -1,
                           &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->total_human_events = (long)sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM detections;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->stored = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return 0;
}
