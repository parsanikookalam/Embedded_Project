/*
 * Part 4 — Anti-theft / Smart Guard features:
 *  1) Guard mode (when ARMED): person edge → email+photo + MQTT home/<id>/alarm
 *  2) Software watchdog (when ENABLED): no new frame >30s while capturing
 *     → email + MQTT home/<id>/watchdog + systemctl restart human_detector
 *  3) Adaptive thermal (when ENABLED): CPU ≥ threshold → cut YOLO res/FPS
 *     → email + MQTT home/<id>/thermal
 */

#include "features_part4.h"
#include "email_alert.h"
#include "feature_flags.h"
#include "guard_state.h"
#include "mqtt_pub.h"
#include "persons_state.h"
#include "telemetry.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CFG_PATH "../config.env"
#define HEARTBEAT_PATH "../data/vision_heartbeat.json"
#define THERMAL_CTRL_PATH "../data/thermal_control.json"

static float g_thermal_on = 80.0f;
static float g_thermal_off = 78.0f;
static int g_watchdog_sec = 30;
static char g_student_id[64] = "402102657";

static void trim_crlf(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

static void strip_quotes(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static void load_part4_cfg(void)
{
    FILE *fp = fopen(CFG_PATH, "r");
    char line[256];
    if (!fp)
        return;
    while (fgets(line, sizeof(line), fp)) {
        trim_crlf(line);
        if (strncmp(line, "THERMAL_TEMP_C=", 15) == 0)
            g_thermal_on = (float)atof(line + 15);
        else if (strncmp(line, "THERMAL_CLEAR_C=", 16) == 0)
            g_thermal_off = (float)atof(line + 16);
        else if (strncmp(line, "WATCHDOG_SEC=", 13) == 0)
            g_watchdog_sec = atoi(line + 13);
        else if (strncmp(line, "STUDENT_ID=", 11) == 0) {
            snprintf(g_student_id, sizeof(g_student_id), "%s", line + 11);
            strip_quotes(g_student_id);
        }
    }
    fclose(fp);
    if (g_watchdog_sec < 10)
        g_watchdog_sec = 10;
    if (g_student_id[0] == '\0')
        snprintf(g_student_id, sizeof(g_student_id), "402102657");
}

static void write_thermal_control(int level, float temp)
{
    /* Cut YOLO work and hard-cap stream FPS when hot (level 2 → ~5 FPS). */
    int yolo = 640;
    int detect_every = 1;
    int target_fps = 0; /* 0 = do not override vision FPS */
    if (level >= 2) {
        yolo = 416;
        detect_every = 4;
        target_fps = 5;
    } else if (level == 1) {
        yolo = 640;
        detect_every = 2;
        target_fps = 12;
    }
    FILE *fp = fopen(THERMAL_CTRL_PATH ".tmp", "w");
    if (!fp)
        return;
    fprintf(fp,
            "{\"throttle_level\":%d,\"cpu_temp\":%.2f,\"yolo_input\":%d,"
            "\"detect_every\":%d,\"target_fps\":%d,\"frame_sleep_ms\":0}\n",
            level, temp, yolo, detect_every, target_fps);
    fclose(fp);
    if (rename(THERMAL_CTRL_PATH ".tmp", THERMAL_CTRL_PATH) != 0) {
        fp = fopen(THERMAL_CTRL_PATH, "w");
        if (!fp)
            return;
        fprintf(fp,
                "{\"throttle_level\":%d,\"cpu_temp\":%.2f,\"yolo_input\":%d,"
                "\"detect_every\":%d,\"target_fps\":%d,\"frame_sleep_ms\":0}\n",
                level, temp, yolo, detect_every, target_fps);
        fclose(fp);
    }
}

static int read_heartbeat(long *ts_out, char *mode_out, size_t mode_sz)
{
    FILE *fp = fopen(HEARTBEAT_PATH, "r");
    char buf[256] = {0};
    char *p;
    long ts = 0;
    if (!fp)
        return -1;
    if (fread(buf, 1, sizeof(buf) - 1, fp) == 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    p = strstr(buf, "\"ts\"");
    if (p)
        sscanf(p, "\"ts\"%*[^0-9-]%ld", &ts);
    if (mode_out && mode_sz) {
        mode_out[0] = '\0';
        p = strstr(buf, "\"mode\"");
        if (p) {
            char tmp[32] = {0};
            if (sscanf(p, "\"mode\"%*[^\"]\"%31[^\"]\"", tmp) == 1)
                snprintf(mode_out, mode_sz, "%s", tmp);
        }
    }
    if (ts_out)
        *ts_out = ts;
    return ts > 0 ? 0 : -1;
}

static void *part4_thread(void *arg)
{
    int prev_count = 0;
    int throttle_level = 0;
    time_t last_guard_mail = 0;
    time_t last_watch_mail = 0;
    time_t last_thermal_mail = 0;
    time_t last_thermal_write = 0;
    (void)arg;

    load_part4_cfg();
    write_thermal_control(0, 0);
    printf("[part4] guard/watchdog/thermal coordinator up (thermal>=%.0fC wd=%ds)\n",
           g_thermal_on, g_watchdog_sec);

    while (1) {
        PersonSnapshot snap;
        SystemTelemetry tel;
        float temp = -1.0f;
        long now = (long)time(NULL);
        int count = 0;
        long hb_ts = 0;
        char mode[32] = {0};
        static int ticks = 0;

        if ((ticks++ % 60) == 0)
            load_part4_cfg();

        if (read_persons_snapshot(&snap) == 0)
            count = snap.count;
        if (get_system_telemetry(&tel) == 0)
            temp = tel.cpu_temp;

        /* ---- 4.1 Guard / anti-theft ----
         * Fast alarm on any INCREASE in person count (0→1, 1→2, 2→3, …).
         * Part 3 separately emails ~every 30s while persons≥1 (debounced).
         */
        if (guard_is_armed() && count > prev_count) {
            if (now - last_guard_mail >= 2) {
                char body[512];
                char subj[160];
                snprintf(body, sizeof(body),
                         "GUARD ALARM (anti-theft)\nStudent ID: %s\n"
                         "Persons: %d → %d (increase)\nCPU temp: %.2f C\n"
                         "Timestamp: %ld\nMQTT topic: home/%s/alarm\n",
                         g_student_id, prev_count, count, temp, now, g_student_id);
                snprintf(subj, sizeof(subj),
                         "[Smart Guard] GUARD ALARM — person increase (%s)", g_student_id);
                email_send_event(subj, body, 1);
                mqtt_publish_alarm(count, temp, now);
                last_guard_mail = now;
                printf("[part4] GUARD alarm %d → %d\n", prev_count, count);
            }
        }
        prev_count = count;

        /* ---- 4.3 Software watchdog ---- */
        if (watchdog_is_enabled() && read_heartbeat(&hb_ts, mode, sizeof(mode)) == 0) {
            long age = (hb_ts > 0) ? (now - hb_ts) : 0;
            /* Camera intended ON (not idle) but no successful frame for >WATCHDOG_SEC */
            int watching = (strcmp(mode, "idle") != 0);
            if (watching && hb_ts > 0 && age > g_watchdog_sec && age < 3600) {
                if (now - last_watch_mail >= 45) {
                    char body[640];
                    snprintf(body, sizeof(body),
                             "WARNING — camera tampering / no frames\n"
                             "Student ID: %s\n"
                             "No new camera frame for >%d seconds (mode=%s).\n"
                             "Last heartbeat ts=%ld age=%lds\n"
                             "Restarting human_detector service.\n"
                             "MQTT topic: home/%s/watchdog\n",
                             g_student_id, g_watchdog_sec, mode, hb_ts, age, g_student_id);
                    email_send_event("[Smart Guard] WARNING — camera tampering", body, 0);
                    mqtt_publish_watchdog(mode, age, now);
                    last_watch_mail = now;
                    {
                        int rc = system("systemctl restart human_detector >/dev/null 2>&1");
                        printf("[part4] watchdog email+mqtt+restart rc=%d age=%ld mode=%s\n", rc,
                               age, mode);
                    }
                }
            }
        }

        /* ---- 4.4 Adaptive thermal ---- */
        if (thermal_is_enabled() && temp > 0) {
            int want = throttle_level;
            if (temp >= g_thermal_on)
                want = (temp >= g_thermal_on + 8.0f) ? 2 : 1;
            else if (temp <= g_thermal_off)
                want = 0;

            if (want != throttle_level) {
                throttle_level = want;
                write_thermal_control(throttle_level, temp);
                last_thermal_write = now;
                printf("[part4] thermal level=%d temp=%.1f\n", throttle_level, temp);
                /* Email on enter / level-up into throttle (rate-limited); MQTT below is continuous */
                if (throttle_level > 0 && now - last_thermal_mail >= 60) {
                    char body[512];
                    snprintf(body, sizeof(body),
                             "Adaptive thermal management\nCPU temp=%.2f C (threshold %.0f C).\n"
                             "Throttle level=%d — lower detect rate; FPS capped when very hot.\n"
                             "MQTT topic: home/%s/thermal (publishes every second while hot)\n",
                             temp, g_thermal_on, throttle_level, g_student_id);
                    email_send_event("[Smart Guard] Thermal throttle active", body, 0);
                    last_thermal_mail = now;
                }
            } else if (now - last_thermal_write >= 15) {
                write_thermal_control(throttle_level, temp);
                last_thermal_write = now;
            }

            /* MQTT: every poll while temp stays above enter threshold (not only once) */
            if (temp >= g_thermal_on) {
                int lvl = throttle_level > 0 ? throttle_level : 1;
                mqtt_publish_thermal(lvl, temp, now);
            }
        } else if (!thermal_is_enabled() && throttle_level != 0) {
            throttle_level = 0;
            write_thermal_control(0, temp > 0 ? temp : 0);
            last_thermal_write = now;
            printf("[part4] thermal disabled → throttle cleared\n");
        }

        sleep(1); /* 1s poll — faster guard edge than 2s */
    }
    return NULL;
}

void part4_start(void)
{
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, part4_thread, NULL) != 0)
        fprintf(stderr, "[part4] failed to start thread\n");
    pthread_attr_destroy(&attr);
}
