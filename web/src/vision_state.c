#include "vision_state.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VISION_PATH "../data/vision_control.json"
#define VISION_TMP "../data/vision_control.json.tmp"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_yolo = 640;
static int g_fps = 24;
static int g_loaded = 0;

static int clamp_yolo(int s)
{
    /* Snap to supported Part 3-3 levels (incl. low 160/256). */
    if (s <= 160)
        return 160;
    if (s <= 256)
        return 256;
    if (s <= 320)
        return 320;
    if (s <= 480)
        return 480;
    return 640;
}

static int clamp_fps(int f)
{
    if (f < 1)
        return 1;
    if (f > 60)
        return 60;
    return f;
}

static void write_file_unlocked(int yolo, int fps)
{
    FILE *fp = fopen(VISION_TMP, "w");
    if (!fp)
        return;
    fprintf(fp, "{\"yolo_input\":%d,\"target_fps\":%d}\n", yolo, fps);
    fclose(fp);
    if (rename(VISION_TMP, VISION_PATH) != 0) {
        fp = fopen(VISION_PATH, "w");
        if (fp) {
            fprintf(fp, "{\"yolo_input\":%d,\"target_fps\":%d}\n", yolo, fps);
            fclose(fp);
        }
    }
}

static void read_file_unlocked(void)
{
    FILE *fp;
    char buf[256] = {0};
    char *p;
    fp = fopen(VISION_PATH, "r");
    if (!fp) {
        g_yolo = 640;
        g_fps = 24;
        write_file_unlocked(g_yolo, g_fps);
        return;
    }
    if (fread(buf, 1, sizeof(buf) - 1, fp) == 0) {
        fclose(fp);
        return;
    }
    fclose(fp);
    p = strstr(buf, "\"yolo_input\"");
    if (p) {
        p = strchr(p, ':');
        if (p)
            g_yolo = clamp_yolo(atoi(p + 1));
    }
    p = strstr(buf, "\"target_fps\"");
    if (p) {
        p = strchr(p, ':');
        if (p)
            g_fps = clamp_fps(atoi(p + 1));
    }
}

static void ensure_loaded(void)
{
    if (!g_loaded) {
        read_file_unlocked();
        g_loaded = 1;
    }
}

int vision_get_yolo_input(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    ensure_loaded();
    v = g_yolo;
    pthread_mutex_unlock(&g_mu);
    return v;
}

int vision_get_target_fps(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    ensure_loaded();
    v = g_fps;
    pthread_mutex_unlock(&g_mu);
    return v;
}

int vision_set_yolo_input(int size)
{
    int v;
    pthread_mutex_lock(&g_mu);
    ensure_loaded();
    g_yolo = clamp_yolo(size);
    write_file_unlocked(g_yolo, g_fps);
    v = g_yolo;
    pthread_mutex_unlock(&g_mu);
    printf("[vision] yolo_input=%d\n", v);
    return v;
}

int vision_set_target_fps(int fps)
{
    int v;
    pthread_mutex_lock(&g_mu);
    ensure_loaded();
    g_fps = clamp_fps(fps);
    write_file_unlocked(g_yolo, g_fps);
    v = g_fps;
    pthread_mutex_unlock(&g_mu);
    printf("[vision] target_fps=%d\n", v);
    return v;
}

int vision_get_json(char *buf, size_t buflen)
{
    if (!buf || buflen < 32)
        return -1;
    snprintf(buf, buflen, "{\"yolo_input\":%d,\"target_fps\":%d}", vision_get_yolo_input(),
             vision_get_target_fps());
    return 0;
}
