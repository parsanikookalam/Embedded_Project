#include "detection_state.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define DET_PATH "../data/detection_state.json"
#define DET_TMP "../data/detection_state.json.tmp"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_enabled = 1; /* default ON — detection runs when camera is ON */
static int g_loaded = 0;

static int read_file_unlocked(void)
{
    FILE *fp;
    char buf[128] = {0};
    fp = fopen(DET_PATH, "r");
    if (!fp)
        return 1; /* missing → enabled */
    if (fread(buf, 1, sizeof(buf) - 1, fp) == 0) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    if (strstr(buf, "\"enabled\":0") || strstr(buf, "\"enabled\": false") ||
        strstr(buf, "\"enabled\":false"))
        return 0;
    return 1;
}

static void write_file_unlocked(int enabled)
{
    FILE *fp = fopen(DET_TMP, "w");
    if (!fp)
        return;
    fprintf(fp, "{\"enabled\":%d}\n", enabled ? 1 : 0);
    fclose(fp);
    if (rename(DET_TMP, DET_PATH) != 0) {
        fp = fopen(DET_PATH, "w");
        if (fp) {
            fprintf(fp, "{\"enabled\":%d}\n", enabled ? 1 : 0);
            fclose(fp);
        }
    }
}

int detection_is_enabled(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    if (!g_loaded) {
        g_enabled = read_file_unlocked();
        g_loaded = 1;
    }
    v = g_enabled;
    pthread_mutex_unlock(&g_mu);
    return v;
}

int detection_set_enabled(int enabled)
{
    int v = enabled ? 1 : 0;
    pthread_mutex_lock(&g_mu);
    g_enabled = v;
    g_loaded = 1;
    write_file_unlocked(v);
    pthread_mutex_unlock(&g_mu);
    printf("[detection] %s\n", v ? "ENABLED" : "disabled (no YOLO/face)");
    return v;
}

int detection_get_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 16)
        return -1;
    snprintf(buf, buflen, "{\"enabled\":%s}", detection_is_enabled() ? "true" : "false");
    return 0;
}
