/*
 * Part 3C — MQTT publisher (C / libmosquitto).
 * Topics:
 *   home/<STUDENT_ID>/persons
 *   home/<STUDENT_ID>/telemetry
 * Payload JSON includes count, cpu_temp, timestamp.
 * QoS = 1, LWT on home/<STUDENT_ID>/status → "offline"
 */

#include "mqtt_pub.h"
#include "persons_state.h"
#include "telemetry.h"

#include <mosquitto.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CFG_PATH "../config.env"

static char g_host[128] = "127.0.0.1";
static int g_port = 1883;
static char g_user[128] = "smartguard";
static char g_pass[128] = "smartguard";
static char g_student[64] = "402102657";
static int g_enabled = 1;
static int g_interval_sec = 2;

static char g_topic_persons[160];
static char g_topic_telem[160];
static char g_topic_status[160];

static struct mosquitto *g_mosq = NULL;
static volatile int g_connected = 0;

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

static void load_mqtt_config(void)
{
    FILE *fp = fopen(CFG_PATH, "r");
    char line[512];
    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        trim_crlf(line);
        if (line[0] == '#' || line[0] == '\0')
            continue;
        if (strncmp(line, "MQTT_ENABLED=", 13) == 0) {
            g_enabled = atoi(line + 13) != 0;
        } else if (strncmp(line, "MQTT_HOST=", 10) == 0) {
            snprintf(g_host, sizeof(g_host), "%s", line + 10);
            strip_quotes(g_host);
        } else if (strncmp(line, "MQTT_PORT=", 10) == 0) {
            g_port = atoi(line + 10);
        } else if (strncmp(line, "MQTT_USER=", 10) == 0) {
            snprintf(g_user, sizeof(g_user), "%s", line + 10);
            strip_quotes(g_user);
        } else if (strncmp(line, "MQTT_PASS=", 10) == 0) {
            snprintf(g_pass, sizeof(g_pass), "%s", line + 10);
            strip_quotes(g_pass);
        } else if (strncmp(line, "MQTT_INTERVAL_SEC=", 18) == 0) {
            g_interval_sec = atoi(line + 18);
            if (g_interval_sec < 1)
                g_interval_sec = 1;
        } else if (strncmp(line, "STUDENT_ID=", 11) == 0) {
            snprintf(g_student, sizeof(g_student), "%s", line + 11);
            strip_quotes(g_student);
        }
    }
    fclose(fp);

    snprintf(g_topic_persons, sizeof(g_topic_persons), "home/%s/persons", g_student);
    snprintf(g_topic_telem, sizeof(g_topic_telem), "home/%s/telemetry", g_student);
    snprintf(g_topic_status, sizeof(g_topic_status), "home/%s/status", g_student);
}

static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    (void)obj;
    if (rc == 0) {
        g_connected = 1;
        printf("[mqtt] connected → %s:%d as %s\n", g_host, g_port, g_user);
        mosquitto_publish(mosq, NULL, g_topic_status, 6, "online", 1, true);
    } else {
        g_connected = 0;
        fprintf(stderr, "[mqtt] connect failed rc=%d (%s)\n", rc, mosquitto_strerror(rc));
    }
}

static void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    (void)mosq;
    (void)obj;
    g_connected = 0;
    fprintf(stderr, "[mqtt] disconnected rc=%d\n", rc);
}

static int publish_json(struct mosquitto *mosq, int count, float temp, long ts)
{
    char payload[256];
    int n = snprintf(payload, sizeof(payload),
                     "{\"count\":%d,\"cpu_temp\":%.2f,\"timestamp\":%ld}", count, temp, ts);
    int rc1, rc2;
    if (n <= 0 || n >= (int)sizeof(payload))
        return -1;

    rc1 = mosquitto_publish(mosq, NULL, g_topic_persons, (int)strlen(payload), payload, 1, false);
    rc2 = mosquitto_publish(mosq, NULL, g_topic_telem, (int)strlen(payload), payload, 1, false);
    if (rc1 != MOSQ_ERR_SUCCESS || rc2 != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[mqtt] publish error persons=%s telem=%s\n",
                mosquitto_strerror(rc1), mosquitto_strerror(rc2));
        return -1;
    }
    return 0;
}

static void *mqtt_thread(void *arg)
{
    int rc;
    (void)arg;

    load_mqtt_config();
    if (!g_enabled) {
        printf("[mqtt] disabled (MQTT_ENABLED=0)\n");
        return NULL;
    }

    mosquitto_lib_init();
    g_mosq = mosquitto_new(NULL, true, NULL);
    if (!g_mosq) {
        fprintf(stderr, "[mqtt] mosquitto_new failed\n");
        return NULL;
    }

    mosquitto_username_pw_set(g_mosq, g_user, g_pass);
    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_disconnect_callback_set(g_mosq, on_disconnect);
    mosquitto_will_set(g_mosq, g_topic_status, 7, "offline", 1, true);

    printf("[mqtt] connecting %s:%d topics %s | %s | LWT %s\n", g_host, g_port,
           g_topic_persons, g_topic_telem, g_topic_status);

    rc = mosquitto_connect(g_mosq, g_host, g_port, 30);
    if (rc != MOSQ_ERR_SUCCESS)
        fprintf(stderr, "[mqtt] mosquitto_connect: %s\n", mosquitto_strerror(rc));

    mosquitto_loop_start(g_mosq);

    while (1) {
        PersonSnapshot snap;
        SystemTelemetry tel;
        int count = 0;
        float temp = -1.0f;
        long ts = (long)time(NULL);

        static int ticks = 0;
        if ((ticks++ % 30) == 0)
            load_mqtt_config();

        if (read_persons_snapshot(&snap) == 0) {
            count = snap.count;
            if (snap.timestamp > 0)
                ts = snap.timestamp;
        }
        if (get_system_telemetry(&tel) == 0)
            temp = tel.cpu_temp;

        if (g_connected)
            publish_json(g_mosq, count, temp, ts);

        sleep(g_interval_sec);
    }

    return NULL;
}

void mqtt_pub_start(void)
{
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, mqtt_thread, NULL) != 0)
        fprintf(stderr, "[mqtt] failed to start thread\n");
    pthread_attr_destroy(&attr);
}

int mqtt_publish_alarm(int count, float cpu_temp, long timestamp)
{
    char topic[160];
    char payload[256];
    int n;
    int rc;

    if (!g_mosq || !g_connected)
        return -1;

    snprintf(topic, sizeof(topic), "home/%s/alarm", g_student);
    n = snprintf(payload, sizeof(payload),
                 "{\"alarm\":true,\"count\":%d,\"cpu_temp\":%.2f,\"timestamp\":%ld}", count,
                 cpu_temp, timestamp);
    if (n <= 0 || n >= (int)sizeof(payload))
        return -1;

    rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[mqtt] alarm publish failed: %s\n", mosquitto_strerror(rc));
        return -1;
    }
    printf("[mqtt] ALARM → %s\n", topic);
    return 0;
}

int mqtt_publish_watchdog(const char *mode, long age_sec, long timestamp)
{
    char topic[160];
    char payload[320];
    int n;
    int rc;
    const char *m = mode ? mode : "unknown";

    if (!g_mosq || !g_connected)
        return -1;

    snprintf(topic, sizeof(topic), "home/%s/watchdog", g_student);
    n = snprintf(payload, sizeof(payload),
                 "{\"watchdog\":true,\"event\":\"camera_tampering\",\"mode\":\"%s\","
                 "\"age_sec\":%ld,\"timestamp\":%ld}",
                 m, age_sec, timestamp);
    if (n <= 0 || n >= (int)sizeof(payload))
        return -1;

    rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[mqtt] watchdog publish failed: %s\n", mosquitto_strerror(rc));
        return -1;
    }
    printf("[mqtt] WATCHDOG → %s\n", topic);
    return 0;
}

int mqtt_publish_thermal(int level, float cpu_temp, long timestamp)
{
    char topic[160];
    char payload[280];
    int n;
    int rc;

    if (!g_mosq || !g_connected)
        return -1;

    snprintf(topic, sizeof(topic), "home/%s/thermal", g_student);
    n = snprintf(payload, sizeof(payload),
                 "{\"thermal\":true,\"throttle_level\":%d,\"cpu_temp\":%.2f,\"timestamp\":%ld}",
                 level, cpu_temp, timestamp);
    if (n <= 0 || n >= (int)sizeof(payload))
        return -1;

    rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[mqtt] thermal publish failed: %s\n", mosquitto_strerror(rc));
        return -1;
    }
    printf("[mqtt] THERMAL → %s level=%d\n", topic, level);
    return 0;
}
