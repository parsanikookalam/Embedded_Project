#include "guard_state.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define GUARD_PATH "../data/guard_state.json"
#define GUARD_TMP "../data/guard_state.json.tmp"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_armed = 0;
static int g_loaded = 0;

static int read_file_unlocked(void)
{
    FILE *fp;
    char buf[128] = {0};
    fp = fopen(GUARD_PATH, "r");
    if (!fp)
        return 0;
    if (fread(buf, 1, sizeof(buf) - 1, fp) == 0) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (strstr(buf, "\"armed\":1") || strstr(buf, "\"armed\": true") ||
        strstr(buf, "\"armed\":true"))
        return 1;
    return 0;
}

static void write_file_unlocked(int armed)
{
    FILE *fp = fopen(GUARD_TMP, "w");
    if (!fp)
        return;
    fprintf(fp, "{\"armed\":%d}\n", armed ? 1 : 0);
    fclose(fp);
    if (rename(GUARD_TMP, GUARD_PATH) != 0) {
        fp = fopen(GUARD_PATH, "w");
        if (fp) {
            fprintf(fp, "{\"armed\":%d}\n", armed ? 1 : 0);
            fclose(fp);
        }
    }
}

int guard_is_armed(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    if (!g_loaded) {
        g_armed = read_file_unlocked();
        g_loaded = 1;
    }
    v = g_armed;
    pthread_mutex_unlock(&g_mu);
    return v;
}

int guard_set_armed(int armed)
{
    int v = armed ? 1 : 0;
    pthread_mutex_lock(&g_mu);
    g_armed = v;
    g_loaded = 1;
    write_file_unlocked(v);
    pthread_mutex_unlock(&g_mu);
    printf("[guard] %s\n", v ? "ARMED" : "disarmed");
    return v;
}

int guard_get_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 16)
        return -1;
    snprintf(buf, buflen, "{\"armed\":%s}", guard_is_armed() ? "true" : "false");
    return 0;
}
