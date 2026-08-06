#include "feature_flags.h"
#include "guard_state.h"
#include "camera_state.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define WD_PATH "../data/watchdog_state.json"
#define TH_PATH "../data/thermal_state.json"
#define WD_TMP "../data/watchdog_state.json.tmp"
#define TH_TMP "../data/thermal_state.json.tmp"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_wd = 1; /* default ON — Part 4 requirement */
static int g_th = 1;
static int g_wd_loaded = 0;
static int g_th_loaded = 0;

static int read_enabled(const char *path)
{
    FILE *fp;
    char buf[128] = {0};
    fp = fopen(path, "r");
    if (!fp)
        return 1; /* missing file → feature ON by default */
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

static void write_enabled(const char *path, const char *tmp, int on)
{
    FILE *fp = fopen(tmp, "w");
    if (!fp)
        return;
    fprintf(fp, "{\"enabled\":%d}\n", on ? 1 : 0);
    fclose(fp);
    if (rename(tmp, path) != 0) {
        fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "{\"enabled\":%d}\n", on ? 1 : 0);
            fclose(fp);
        }
    }
}

int watchdog_is_enabled(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    if (!g_wd_loaded) {
        g_wd = read_enabled(WD_PATH);
        g_wd_loaded = 1;
    }
    v = g_wd;
    pthread_mutex_unlock(&g_mu);
    return v;
}

int watchdog_set_enabled(int on)
{
    int v = on ? 1 : 0;
    pthread_mutex_lock(&g_mu);
    g_wd = v;
    g_wd_loaded = 1;
    write_enabled(WD_PATH, WD_TMP, v);
    pthread_mutex_unlock(&g_mu);
    printf("[watchdog] %s\n", v ? "ENABLED" : "disabled");
    return v;
}

int watchdog_get_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 16)
        return -1;
    snprintf(buf, buflen, "{\"enabled\":%s}", watchdog_is_enabled() ? "true" : "false");
    return 0;
}

int thermal_is_enabled(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    if (!g_th_loaded) {
        g_th = read_enabled(TH_PATH);
        g_th_loaded = 1;
    }
    v = g_th;
    pthread_mutex_unlock(&g_mu);
    return v;
}

int thermal_set_enabled(int on)
{
    int v = on ? 1 : 0;
    pthread_mutex_lock(&g_mu);
    g_th = v;
    g_th_loaded = 1;
    write_enabled(TH_PATH, TH_TMP, v);
    pthread_mutex_unlock(&g_mu);
    printf("[thermal] %s\n", v ? "ENABLED" : "disabled");
    return v;
}

int thermal_get_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 16)
        return -1;
    snprintf(buf, buflen, "{\"enabled\":%s}", thermal_is_enabled() ? "true" : "false");
    return 0;
}

int part4_status_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 64)
        return -1;
    snprintf(buf, buflen,
             "{\"guard\":%s,\"watchdog\":%s,\"thermal\":%s,\"camera\":%s}",
             guard_is_armed() ? "true" : "false",
             watchdog_is_enabled() ? "true" : "false",
             thermal_is_enabled() ? "true" : "false",
             camera_is_enabled() ? "true" : "false");
    return 0;
}
