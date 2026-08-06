/*
 * Part 3B — person-detection email alerts (C + libcurl SMTP).
 * Debounce: at most one email per EMAIL_DEBOUNCE_SEC (default 30).
 * Secrets come from config.env — never hardcode.
 */

#include "email_alert.h"
#include "persons_state.h"
#include "telemetry.h"
#include "guard_state.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CFG_PATH "../config.env"
#define SNAP_JPG "../data/latest_detection.jpg"

static char g_smtp_url[256] = "smtps://smtp.gmail.com:465";
static char g_smtp_user[128] = "";
static char g_smtp_pass[128] = "";
static char g_mail_from[128] = "";
static char g_mail_to[128] = "";
static char g_student_id[64] = "unknown";
static int g_debounce_sec = 30;
static int g_enabled = 0;

static time_t g_last_sent = 0;
static int g_warned_cfg = 0;

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

static void load_email_config(void)
{
    FILE *fp = fopen(CFG_PATH, "r");
    char line[512];
    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        trim_crlf(line);
        if (line[0] == '#' || line[0] == '\0')
            continue;
        if (strncmp(line, "SMTP_URL=", 9) == 0) {
            snprintf(g_smtp_url, sizeof(g_smtp_url), "%s", line + 9);
            strip_quotes(g_smtp_url);
        } else if (strncmp(line, "SMTP_USER=", 10) == 0) {
            snprintf(g_smtp_user, sizeof(g_smtp_user), "%s", line + 10);
            strip_quotes(g_smtp_user);
        } else if (strncmp(line, "SMTP_PASS=", 10) == 0) {
            snprintf(g_smtp_pass, sizeof(g_smtp_pass), "%s", line + 10);
            strip_quotes(g_smtp_pass);
        } else if (strncmp(line, "EMAIL_FROM=", 11) == 0) {
            snprintf(g_mail_from, sizeof(g_mail_from), "%s", line + 11);
            strip_quotes(g_mail_from);
        } else if (strncmp(line, "EMAIL_TO=", 9) == 0) {
            snprintf(g_mail_to, sizeof(g_mail_to), "%s", line + 9);
            strip_quotes(g_mail_to);
        } else if (strncmp(line, "EMAIL_DEBOUNCE_SEC=", 19) == 0) {
            g_debounce_sec = atoi(line + 19);
            if (g_debounce_sec < 5)
                g_debounce_sec = 5;
        } else if (strncmp(line, "EMAIL_ENABLED=", 14) == 0) {
            g_enabled = (atoi(line + 14) != 0);
        } else if (strncmp(line, "STUDENT_ID=", 11) == 0) {
            snprintf(g_student_id, sizeof(g_student_id), "%s", line + 11);
            strip_quotes(g_student_id);
        }
    }
    fclose(fp);

    if (g_mail_from[0] == '\0' && g_smtp_user[0] != '\0')
        snprintf(g_mail_from, sizeof(g_mail_from), "%s", g_smtp_user);

    if (g_enabled && g_smtp_user[0] && g_smtp_pass[0] && g_mail_to[0] && g_mail_from[0])
        return;

    g_enabled = 0;
}

/* --- base64 --- */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *data, size_t len, size_t *out_len)
{
    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(olen + 1);
    size_t i, j;
    if (!out)
        return NULL;
    for (i = 0, j = 0; i < len;) {
        unsigned int a = i < len ? data[i++] : 0;
        unsigned int b = i < len ? data[i++] : 0;
        unsigned int c = i < len ? data[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }
    {
        size_t mod = len % 3;
        if (mod)
            for (i = 0; i < 3 - mod; i++)
                out[olen - 1 - i] = '=';
    }
    out[olen] = '\0';
    if (out_len)
        *out_len = olen;
    return out;
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *buf;
    long sz;
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    sz = ftell(fp);
    if (sz <= 0 || sz > 8 * 1024 * 1024) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *len = (size_t)sz;
    return buf;
}

struct upload_state {
    const char *data;
    size_t len;
    size_t pos;
};

static size_t payload_read(char *ptr, size_t size, size_t nmemb, void *userp)
{
    struct upload_state *st = (struct upload_state *)userp;
    size_t room = size * nmemb;
    size_t left = st->len - st->pos;
    size_t n = left < room ? left : room;
    if (n == 0)
        return 0;
    memcpy(ptr, st->data + st->pos, n);
    st->pos += n;
    return n;
}

static int send_alert_email(int count, long timestamp, float cpu_temp)
{
    CURL *curl;
    CURLcode res;
    struct curl_slist *recipients = NULL;
    struct upload_state up;
    char *payload = NULL;
    size_t payload_cap = 0;
    size_t payload_len = 0;
    unsigned char *jpg = NULL;
    size_t jpg_len = 0;
    char *jpg_b64 = NULL;
    size_t b64_len = 0;
    char boundary[64];
    char when[64];
    int rc = -1;
    time_t now = time(NULL);

    if (!g_enabled)
        return -1;
    if (g_last_sent != 0 && (now - g_last_sent) < g_debounce_sec) {
        printf("[email] debounce: next allowed in %ld s\n",
               (long)(g_debounce_sec - (now - g_last_sent)));
        return 0;
    }

    snprintf(boundary, sizeof(boundary), "SmartGuard_%ld", (long)now);
    {
        struct tm tmv;
        localtime_r(&timestamp, &tmv);
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);
    }

    jpg = read_file(SNAP_JPG, &jpg_len);
    if (jpg)
        jpg_b64 = base64_encode(jpg, jpg_len, &b64_len);

    payload_cap = 4096 + (jpg_b64 ? b64_len + b64_len / 76 + 256 : 0);
    payload = (char *)malloc(payload_cap);
    if (!payload)
        goto cleanup;

    payload_len = (size_t)snprintf(
        payload, payload_cap,
        "From: Smart Guard <%s>\r\n"
        "To: <%s>\r\n"
        "Subject: [Smart Guard %s] Person detected (count=%d)\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"%s\"\r\n"
        "\r\n"
        "--%s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Transfer-Encoding: 7bit\r\n"
        "\r\n"
        "Smart Guard person alert\r\n"
        "Student ID : %s\r\n"
        "Persons    : %d\r\n"
        "Timestamp  : %s (%ld)\r\n"
        "CPU Temp   : %.2f C\r\n"
        "\r\n"
        "Debounce window: %d seconds (max 1 email per window).\r\n"
        "\r\n",
        g_mail_from, g_mail_to, g_student_id, count, boundary, boundary,
        g_student_id, count, when, timestamp, cpu_temp, g_debounce_sec);

    if (jpg_b64 && b64_len > 0) {
        size_t i;
        payload_len += (size_t)snprintf(
            payload + payload_len, payload_cap - payload_len,
            "--%s\r\n"
            "Content-Type: image/jpeg; name=\"latest_detection.jpg\"\r\n"
            "Content-Transfer-Encoding: base64\r\n"
            "Content-Disposition: attachment; filename=\"latest_detection.jpg\"\r\n"
            "\r\n",
            boundary);
        for (i = 0; i < b64_len; i += 76) {
            size_t chunk = (b64_len - i > 76) ? 76 : (b64_len - i);
            if (payload_len + chunk + 4 >= payload_cap)
                break;
            memcpy(payload + payload_len, jpg_b64 + i, chunk);
            payload_len += chunk;
            payload[payload_len++] = '\r';
            payload[payload_len++] = '\n';
            payload[payload_len] = '\0';
        }
    } else {
        payload_len += (size_t)snprintf(
            payload + payload_len, payload_cap - payload_len,
            "(No detection image available yet — open the dashboard stream once.)\r\n"
            "\r\n");
    }

    payload_len += (size_t)snprintf(payload + payload_len, payload_cap - payload_len,
                                    "--%s--\r\n", boundary);

    curl = curl_easy_init();
    if (!curl)
        goto cleanup;

    up.data = payload;
    up.len = payload_len;
    up.pos = 0;

    recipients = curl_slist_append(NULL, g_mail_to);

    curl_easy_setopt(curl, CURLOPT_URL, g_smtp_url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, g_smtp_user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, g_smtp_pass);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, g_mail_from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_read);
    curl_easy_setopt(curl, CURLOPT_READDATA, &up);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)payload_len);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[email] send failed: %s\n", curl_easy_strerror(res));
    } else {
        g_last_sent = now;
        printf("[email] alert sent to %s (count=%d temp=%.1f)\n", g_mail_to, count,
               cpu_temp);
        rc = 0;
    }

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

cleanup:
    free(payload);
    free(jpg_b64);
    free(jpg);
    return rc;
}

static void *email_alert_thread(void *arg)
{
    (void)arg;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    load_email_config();

    if (!g_enabled) {
        if (!g_warned_cfg) {
            printf("[email] disabled — set EMAIL_ENABLED=1 and Gmail SMTP_* in config.env\n");
            g_warned_cfg = 1;
        }
    } else {
        printf("[email] enabled → %s (debounce %ds)\n", g_mail_to, g_debounce_sec);
    }

    while (1) {
        PersonSnapshot snap;
        SystemTelemetry tel;
        float temp = -1.0f;

        /* Reload config occasionally so enabling email doesn't need full rebuild */
        static int ticks = 0;
        if ((ticks++ % 15) == 0)
            load_email_config();

        /* Part 3 normal alerts when guard is OFF. Guard-armed alerts are Part 4. */
        if (g_enabled && !guard_is_armed() && read_persons_snapshot(&snap) == 0 &&
            snap.count >= 1) {
            if (get_system_telemetry(&tel) == 0)
                temp = tel.cpu_temp;
            send_alert_email(snap.count, snap.timestamp ? snap.timestamp : (long)time(NULL),
                             temp);
        }
        sleep(2);
    }
    return NULL;
}

int email_alert_test_send(void)
{
    SystemTelemetry tel;
    float temp = -1.0f;
    time_t saved;

    load_email_config();
    if (!g_enabled) {
        fprintf(stderr, "[email] test failed: EMAIL_ENABLED=0 or SMTP fields empty\n");
        return -1;
    }
    if (get_system_telemetry(&tel) == 0)
        temp = tel.cpu_temp;

    /* Bypass debounce for explicit test */
    saved = g_last_sent;
    g_last_sent = 0;
    {
        int rc = send_alert_email(1, (long)time(NULL), temp);
        if (rc != 0)
            g_last_sent = saved;
        return rc;
    }
}

int email_send_event(const char *subject, const char *body_text, int attach_jpg)
{
    CURL *curl;
    CURLcode res;
    struct curl_slist *recipients = NULL;
    struct upload_state up;
    char *payload = NULL;
    size_t payload_cap = 0;
    size_t payload_len = 0;
    unsigned char *jpg = NULL;
    size_t jpg_len = 0;
    char *jpg_b64 = NULL;
    size_t b64_len = 0;
    char boundary[64];
    int rc = -1;
    time_t now = time(NULL);

    load_email_config();
    if (!g_enabled)
        return -1;

    snprintf(boundary, sizeof(boundary), "SmartGuardEvt_%ld", (long)now);
    if (attach_jpg) {
        jpg = read_file(SNAP_JPG, &jpg_len);
        if (jpg)
            jpg_b64 = base64_encode(jpg, jpg_len, &b64_len);
    }

    payload_cap = 8192 + (jpg_b64 ? b64_len + b64_len / 76 + 256 : 0);
    payload = (char *)malloc(payload_cap);
    if (!payload)
        goto cleanup;

    payload_len = (size_t)snprintf(
        payload, payload_cap,
        "From: Smart Guard <%s>\r\n"
        "To: <%s>\r\n"
        "Subject: %s\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"%s\"\r\n"
        "\r\n"
        "--%s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "%s\r\n"
        "\r\n",
        g_mail_from, g_mail_to, subject ? subject : "[Smart Guard]", boundary, boundary,
        body_text ? body_text : "");

    if (jpg_b64 && b64_len > 0) {
        size_t i;
        payload_len += (size_t)snprintf(
            payload + payload_len, payload_cap - payload_len,
            "--%s\r\n"
            "Content-Type: image/jpeg; name=\"latest_detection.jpg\"\r\n"
            "Content-Transfer-Encoding: base64\r\n"
            "Content-Disposition: attachment; filename=\"latest_detection.jpg\"\r\n"
            "\r\n",
            boundary);
        for (i = 0; i < b64_len; i += 76) {
            size_t chunk = (b64_len - i > 76) ? 76 : (b64_len - i);
            if (payload_len + chunk + 4 >= payload_cap)
                break;
            memcpy(payload + payload_len, jpg_b64 + i, chunk);
            payload_len += chunk;
            payload[payload_len++] = '\r';
            payload[payload_len++] = '\n';
            payload[payload_len] = '\0';
        }
    }

    payload_len += (size_t)snprintf(payload + payload_len, payload_cap - payload_len,
                                    "--%s--\r\n", boundary);

    curl = curl_easy_init();
    if (!curl)
        goto cleanup;

    up.data = payload;
    up.len = payload_len;
    up.pos = 0;
    recipients = curl_slist_append(NULL, g_mail_to);
    curl_easy_setopt(curl, CURLOPT_URL, g_smtp_url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, g_smtp_user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, g_smtp_pass);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, g_mail_from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_read);
    curl_easy_setopt(curl, CURLOPT_READDATA, &up);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)payload_len);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "[email] event send failed: %s\n", curl_easy_strerror(res));
    else {
        printf("[email] event sent: %s\n", subject ? subject : "(no subject)");
        rc = 0;
    }
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

cleanup:
    free(payload);
    free(jpg_b64);
    free(jpg);
    return rc;
}

void email_alert_start(void)
{
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, email_alert_thread, NULL) != 0)
        fprintf(stderr, "[email] failed to start alert thread\n");
    pthread_attr_destroy(&attr);
}
