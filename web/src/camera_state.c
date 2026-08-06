#include "camera_state.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define CAMERA_PATH "../data/camera_state.json"
#define CAMERA_TMP "../data/camera_state.json.tmp"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_enabled = 0;
static int g_loaded = 0;

static int read_file_unlocked(void)
{
    FILE *fp;
    char buf[128] = {0};
    fp = fopen(CAMERA_PATH, "r");
    if (!fp)
        return 0;
    if (fread(buf, 1, sizeof(buf) - 1, fp) == 0) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (strstr(buf, "\"enabled\":1") || strstr(buf, "\"enabled\": true") ||
        strstr(buf, "\"enabled\":true"))
        return 1;
    return 0;
}

static void write_file_unlocked(int enabled)
{
    FILE *fp = fopen(CAMERA_TMP, "w");
    if (!fp)
        return;
    fprintf(fp, "{\"enabled\":%d}\n", enabled ? 1 : 0);
    fclose(fp);
    if (rename(CAMERA_TMP, CAMERA_PATH) != 0) {
        fp = fopen(CAMERA_PATH, "w");
        if (fp) {
            fprintf(fp, "{\"enabled\":%d}\n", enabled ? 1 : 0);
            fclose(fp);
        }
    }
}

int camera_is_enabled(void)
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

int camera_set_enabled(int enabled)
{
    int v = enabled ? 1 : 0;
    pthread_mutex_lock(&g_mu);
    g_enabled = v;
    g_loaded = 1;
    write_file_unlocked(v);
    pthread_mutex_unlock(&g_mu);
    printf("[camera] %s\n", v ? "ON" : "OFF");
    return v;
}

int camera_get_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 16)
        return -1;
    snprintf(buf, buflen, "{\"enabled\":%s}", camera_is_enabled() ? "true" : "false");
    return 0;
}
