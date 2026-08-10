/*
 * Smart Guard C HTTPS server (Parts 1–2)
 * Target: WSL (ports from config.env) or Orange Pi (80/443).
 *
 * Logic here: redirect, TLS, HTML, stream proxy, telemetry, persons,
 * history, extensible /command. Vision stays in Python.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "telemetry.h"
#include "persons_state.h"
#include "email_alert.h"
#include "guard_state.h"
#include "camera_state.h"
#include "detection_state.h"
#include "vision_state.h"
#include "feature_flags.h"
#include "http_server.h"

#define BUFFER_SIZE 8192

static int g_http_port = 8080;
static int g_https_port = 8443;
static int g_stream_port = 5000;
static char g_stream_host[64] = "127.0.0.1";
static char g_target[32] = "wsl";

char env_student_id[32] = "402102657";
char env_student_name[64] = "Parsa Nikookalam";

static void strip_quotes(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                    (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

static void load_config(void) {
    FILE *fp = fopen("../config.env", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        if (strncmp(line, "STUDENT_ID=", 11) == 0) {
            sscanf(line, "STUDENT_ID=%31[^\n]", env_student_id);
            trim_crlf(env_student_id);
            strip_quotes(env_student_id);
        } else if (strncmp(line, "STUDENT_NAME=", 13) == 0) {
            sscanf(line, "STUDENT_NAME=%63[^\n]", env_student_name);
            trim_crlf(env_student_name);
            strip_quotes(env_student_name);
        } else if (strncmp(line, "HTTP_PORT=", 10) == 0) {
            g_http_port = atoi(line + 10);
        } else if (strncmp(line, "HTTPS_PORT=", 11) == 0) {
            g_https_port = atoi(line + 11);
        } else if (strncmp(line, "STREAM_PORT=", 12) == 0) {
            g_stream_port = atoi(line + 12);
        } else if (strncmp(line, "STREAM_HOST=", 12) == 0) {
            sscanf(line, "STREAM_HOST=%63[^\n]", g_stream_host);
            trim_crlf(g_stream_host);
            strip_quotes(g_stream_host);
        } else if (strncmp(line, "TARGET=", 7) == 0) {
            sscanf(line, "TARGET=%31[^\n]", g_target);
            trim_crlf(g_target);
            strip_quotes(g_target);
        }
    }
    fclose(fp);

    if (g_http_port <= 0) g_http_port = 8080;
    if (g_https_port <= 0) g_https_port = 8443;
    if (g_stream_port <= 0) g_stream_port = 5000;
}

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

static void send_ssl_response(SSL *ssl, int status, const char *status_text,
                              const char *content_type, const char *body) {
    char header[512];
    int body_len = (int)strlen(body);
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "\r\n",
                     status, status_text, content_type, body_len);
    SSL_write(ssl, header, n);
    SSL_write(ssl, body, body_len);
}

static const char *find_body(const char *req) {
    const char *p = strstr(req, "\r\n\r\n");
    return p ? p + 4 : NULL;
}

static int content_length_of(const char *req)
{
    const char *p = req;
    while (p && *p) {
        if ((p[0] == 'C' || p[0] == 'c') && strncasecmp(p, "Content-Length:", 15) == 0)
            return atoi(p + 15);
        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }
    return -1;
}

/* httpx/Swagger often split headers/body across TLS records — gather with timeout. */
static int ssl_read_http_request(SSL *ssl, char *buf, size_t bufsz)
{
    size_t total = 0;
    int fd;
    struct timeval tv = {.tv_sec = 1, .tv_usec = 500000};

    if (!ssl || !buf || bufsz < 64)
        return -1;
    buf[0] = '\0';
    fd = SSL_get_fd(ssl);
    if (fd >= 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (total < bufsz - 1) {
        int n = SSL_read(ssl, buf + total, (int)(bufsz - 1 - total));
        if (n > 0) {
            total += (size_t)n;
            buf[total] = '\0';
            char *sep = strstr(buf, "\r\n\r\n");
            if (!sep)
                continue;
            size_t header_len = (size_t)(sep - buf) + 4;
            int cl = content_length_of(buf);
            if (cl < 0)
                cl = 0;
            if (header_len + (size_t)cl > bufsz - 1)
                cl = (int)(bufsz - 1 - header_len);
            if (total >= header_len + (size_t)cl)
                return (int)total;
            continue;
        }
        /* timeout / EOF: use whatever we already have */
        break;
    }
    return total > 0 ? (int)total : -1;
}

static int extract_json_cmd(const char *body, char *cmd_out, size_t cmd_sz) {
    if (!body || !cmd_out || cmd_sz == 0) return -1;
    const char *p = strstr(body, "\"cmd\"");
    if (!p) return -1;
    p = strchr(p + 5, '"');
    if (!p) return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cmd_sz)
        cmd_out[i++] = *p++;
    cmd_out[i] = '\0';
    return i > 0 ? 0 : -1;
}

static int path_match(const char *req, const char *method, const char *path) {
    char needle[128];
    snprintf(needle, sizeof(needle), "%s %s", method, path);
    if (strstr(req, needle) != NULL) return 1;

    char upper[128];
    size_t n = strlen(needle);
    if (n >= sizeof(upper)) return 0;
    for (size_t i = 0; i <= n; i++) {
        char c = needle[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        upper[i] = c;
    }
    return strstr(req, upper) != NULL;
}

static void handle_stream_proxy(SSL *ssl) {
    int upstream = socket(AF_INET, SOCK_STREAM, 0);
    if (upstream < 0) {
        send_ssl_response(ssl, 502, "Bad Gateway", "application/json",
                          "{\"error\":\"stream_unavailable\"}");
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_stream_port);
    if (inet_pton(AF_INET, g_stream_host, &addr.sin_addr) <= 0) {
        close(upstream);
        send_ssl_response(ssl, 502, "Bad Gateway", "application/json",
                          "{\"error\":\"bad_stream_host\"}");
        return;
    }

    if (connect(upstream, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(upstream);
        send_ssl_response(ssl, 502, "Bad Gateway", "application/json",
                          "{\"error\":\"detector_not_running\"}");
        return;
    }

    char req[256];
    snprintf(req, sizeof(req),
             "GET /video_feed HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
             g_stream_host, g_stream_port);
    if (write(upstream, req, strlen(req)) < 0) {
        close(upstream);
        send_ssl_response(ssl, 502, "Bad Gateway", "application/json",
                          "{\"error\":\"stream_request_failed\"}");
        return;
    }

    fprintf(stderr, "[stream] HTTPS client connected — proxying MJPEG /api/v1/stream\n");
    fflush(stderr);

    char buf[BUFFER_SIZE];
    ssize_t n;
    int headers_sent = 0;
    char header_acc[2048];
    size_t header_len = 0;
    int client_gone = 0;

    while ((n = read(upstream, buf, sizeof(buf))) > 0) {
        if (!headers_sent) {
            if (header_len + (size_t)n >= sizeof(header_acc)) {
                if (SSL_write(ssl, header_acc, (int)header_len) <= 0 ||
                    SSL_write(ssl, buf, (int)n) <= 0) {
                    client_gone = 1;
                    break;
                }
                headers_sent = 1;
                continue;
            }
            memcpy(header_acc + header_len, buf, (size_t)n);
            header_len += (size_t)n;
            header_acc[header_len] = '\0';

            char *sep = strstr(header_acc, "\r\n\r\n");
            if (!sep) continue;

            size_t raw_hdr = (size_t)(sep - header_acc);
            char out_hdr[512];
            int hn = snprintf(out_hdr, sizeof(out_hdr),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                              "Cache-Control: no-cache\r\n"
                              "Connection: close\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "\r\n");
            if (SSL_write(ssl, out_hdr, hn) <= 0) {
                client_gone = 1;
                break;
            }

            size_t body_off = raw_hdr + 4;
            if (body_off < header_len) {
                if (SSL_write(ssl, header_acc + body_off, (int)(header_len - body_off)) <= 0) {
                    client_gone = 1;
                    break;
                }
            }
            headers_sent = 1;
        } else if (SSL_write(ssl, buf, (int)n) <= 0) {
            client_gone = 1;
            break;
        }
    }
    close(upstream);
    if (client_gone) {
        fprintf(stderr,
                "[stream] HTTPS client DISCONNECTED — stream session ended "
                "(browser closed or WSL↔Windows/network link lost)\n");
    } else {
        fprintf(stderr, "[stream] HTTPS stream proxy ended (upstream closed)\n");
    }
    fflush(stderr);
}

static void handle_command(SSL *ssl, const char *req) {
    const char *body = find_body(req);
    char cmd[64] = {0};

    /* Body may be missing if client split the TLS records — search whole request. */
    if (extract_json_cmd(body, cmd, sizeof(cmd)) != 0 &&
        extract_json_cmd(req, cmd, sizeof(cmd)) != 0) {
        send_ssl_response(ssl, 400, "Bad Request", "application/json",
                          "{\"error\":\"missing_cmd\",\"hint\":\"{\\\"cmd\\\":\\\"reboot\\\"}\"}");
        return;
    }

    if (strcmp(cmd, "reboot") == 0) {
        if (strcmp(g_target, "wsl") == 0) {
            send_ssl_response(ssl, 200, "OK", "application/json",
                              "{\"status\":\"accepted\",\"cmd\":\"reboot\","
                              "\"mode\":\"wsl_soft\",\"note\":\"marked only; no host reboot\"}");
            FILE *fp = fopen("../data/reboot_requested", "w");
            if (fp) {
                fprintf(fp, "%ld\n", (long)time(NULL));
                fclose(fp);
            }
            return;
        }
        send_ssl_response(ssl, 200, "OK", "application/json",
                          "{\"status\":\"accepted\",\"cmd\":\"reboot\"}");
        if (fork() == 0) {
            sleep(1);
            sync(); /* flush disks before reboot (Orange Pi path) */
            execlp("reboot", "reboot", (char *)NULL);
            _exit(1);
        }
        return;
    }

    if (strcmp(cmd, "test_email") == 0) {
        int rc = email_alert_test_send();
        if (rc == 0) {
            send_ssl_response(ssl, 200, "OK", "application/json",
                              "{\"status\":\"sent\",\"cmd\":\"test_email\"}");
        } else {
            send_ssl_response(ssl, 500, "Error", "application/json",
                              "{\"status\":\"failed\",\"cmd\":\"test_email\","
                              "\"hint\":\"check EMAIL_* in config.env and journalctl -u web_server\"}");
        }
        return;
    }

    if (strcmp(cmd, "guard_on") == 0 || strcmp(cmd, "guard_arm") == 0) {
        int a = guard_set_armed(1);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"guard_on\",\"armed\":%s}",
                 a ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "guard_off") == 0 || strcmp(cmd, "guard_disarm") == 0) {
        int a = guard_set_armed(0);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"guard_off\",\"armed\":%s}",
                 a ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "camera_on") == 0) {
        int e = camera_set_enabled(1);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"camera_on\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "camera_off") == 0) {
        int e = camera_set_enabled(0);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"camera_off\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "detection_on") == 0 || strcmp(cmd, "detect_on") == 0) {
        int e = detection_set_enabled(1);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"detection_on\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "detection_off") == 0 || strcmp(cmd, "detect_off") == 0) {
        int e = detection_set_enabled(0);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"detection_off\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "watchdog_on") == 0) {
        int e = watchdog_set_enabled(1);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"watchdog_on\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "watchdog_off") == 0) {
        int e = watchdog_set_enabled(0);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"watchdog_off\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "thermal_on") == 0) {
        int e = thermal_set_enabled(1);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"thermal_on\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(cmd, "thermal_off") == 0) {
        int e = thermal_set_enabled(0);
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"thermal_off\",\"enabled\":%s}",
                 e ? "true" : "false");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    /* Part 3-3: dynamic resolution / FPS (no detector restart) */
    if (strncmp(cmd, "resolution_", 11) == 0) {
        int size = atoi(cmd + 11);
        int v = vision_set_yolo_input(size);
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"%s\",\"yolo_input\":%d,\"target_fps\":%d}",
                 cmd, v, vision_get_target_fps());
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }
    if (strncmp(cmd, "fps_", 4) == 0) {
        int fps = atoi(cmd + 4);
        int v = vision_set_target_fps(fps);
        char json[160];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"cmd\":\"%s\",\"yolo_input\":%d,\"target_fps\":%d}",
                 cmd, vision_get_yolo_input(), v);
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    char resp[640];
    snprintf(resp, sizeof(resp),
             "{\"error\":\"unknown_cmd\",\"cmd\":\"%s\","
             "\"supported\":[\"reboot\",\"test_email\",\"guard_on\",\"guard_off\","
             "\"camera_on\",\"camera_off\",\"detection_on\",\"detection_off\","
             "\"watchdog_on\",\"watchdog_off\","
             "\"thermal_on\",\"thermal_off\","
             "\"resolution_160\",\"resolution_256\",\"resolution_320\","
             "\"resolution_480\",\"resolution_640\","
             "\"fps_10\",\"fps_15\",\"fps_24\",\"fps_30\"]}",
             cmd);
    send_ssl_response(ssl, 400, "Bad Request", "application/json", resp);
}

static void handle_https_client(SSL *ssl) {
    char buffer[BUFFER_SIZE] = {0};
    int n = ssl_read_http_request(ssl, buffer, sizeof(buffer));
    if (n <= 0) return;

    if (strncmp(buffer, "OPTIONS ", 8) == 0) {
        const char *hdr =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        SSL_write(ssl, hdr, (int)strlen(hdr));
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/stream") ||
        path_match(buffer, "HEAD", "/api/v1/stream")) {
        if (strncmp(buffer, "HEAD ", 5) == 0) {
            const char *hdr =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";
            SSL_write(ssl, hdr, (int)strlen(hdr));
            return;
        }
        handle_stream_proxy(ssl);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/telemetry")) {
        SystemTelemetry t;
        get_system_telemetry(&t);
        char json[512];
        snprintf(json, sizeof(json),
                 "{\"cpu_temp\": %.2f, \"free_mem_kb\": %ld, "
                 "\"mem_used_percent\": %.2f, \"cpu_usage_percent\": %.2f}",
                 t.cpu_temp, t.free_mem_kb, t.mem_used_percent, t.cpu_usage_percent);
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/config")) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"id\": \"%s\", \"name\": \"%s\"}", env_student_id, env_student_name);
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/persons")) {
        PersonSnapshot snap;
        read_persons_snapshot(&snap);
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"count\": %d, \"timestamp\": %ld}", snap.count, snap.timestamp);
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/history")) {
        PersonHistory hist;
        read_persons_history(&hist);
        char json[1024];
        size_t off = 0;
        off += (size_t)snprintf(json + off, sizeof(json) - off, "{\"records\":[");
        for (int i = 0; i < hist.count; i++) {
            off += (size_t)snprintf(json + off, sizeof(json) - off,
                                    "%s{\"count\":%d,\"timestamp\":%ld}",
                                    (i ? "," : ""),
                                    hist.items[i].count,
                                    hist.items[i].timestamp);
        }
        snprintf(json + off, sizeof(json) - off, "]}");
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/guard")) {
        char json[64];
        guard_get_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/camera")) {
        char json[64];
        camera_get_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/detection")) {
        char json[64];
        detection_get_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/vision")) {
        char json[96];
        vision_get_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/watchdog")) {
        char json[64];
        watchdog_get_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/thermal")) {
        char json[64];
        thermal_get_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/part4")) {
        char json[192];
        part4_status_json(json, sizeof(json));
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "GET", "/api/v1/blackbox")) {
        BlackBoxStats st;
        read_blackbox_stats(&st);
        char json[192];
        snprintf(json, sizeof(json),
                 "{\"total_human_events\":%ld,\"stored\":%d,\"capacity\":%d}",
                 st.total_human_events, st.stored, st.capacity);
        send_ssl_response(ssl, 200, "OK", "application/json", json);
        return;
    }

    if (path_match(buffer, "POST", "/api/v1/command")) {
        handle_command(ssl, buffer);
        return;
    }

    /* Static HTML pages from www/ */
    {
        const char *page = NULL;
        if (path_match(buffer, "GET", "/") || path_match(buffer, "GET", "/index.html"))
            page = "www/index.html";
        else if (path_match(buffer, "GET", "/stream") || path_match(buffer, "GET", "/stream.html"))
            page = "www/stream.html";

        if (page) {
            FILE *fp = fopen(page, "r");
            if (!fp) {
                send_ssl_response(ssl, 404, "Not Found", "text/plain", "page missing");
                return;
            }
            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *file_buf = malloc((size_t)file_size + 1);
            if (!file_buf) {
                fclose(fp);
                send_ssl_response(ssl, 500, "Internal Server Error", "text/plain", "oom");
                return;
            }
            if (fread(file_buf, 1, (size_t)file_size, fp) != (size_t)file_size) {
                free(file_buf);
                fclose(fp);
                send_ssl_response(ssl, 500, "Internal Server Error", "text/plain", "read_failed");
                return;
            }
            fclose(fp);
            file_buf[file_size] = '\0';
            send_ssl_response(ssl, 200, "OK", "text/html", file_buf);
            free(file_buf);
            return;
        }
    }

    send_ssl_response(ssl, 404, "Not Found", "text/plain", "not found");
}

static void *http_redirect_thread(void *arg)
{
    int new_socket = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE] = {0};
    (void)read(new_socket, buffer, BUFFER_SIZE - 1);

    char host[128] = "127.0.0.1";
    char *h = strstr(buffer, "Host:");
    if (!h)
        h = strstr(buffer, "host:");
    if (h) {
        sscanf(h, "%*[^:]: %127s", host);
        char *colon = strchr(host, ':');
        if (colon)
            *colon = '\0';
    }

    char response[320];
    if (g_https_port == 443) {
        snprintf(response, sizeof(response),
                 "HTTP/1.1 301 Moved Permanently\r\n"
                 "Location: https://%s/\r\n"
                 "Connection: close\r\n\r\n",
                 host);
    } else {
        snprintf(response, sizeof(response),
                 "HTTP/1.1 301 Moved Permanently\r\n"
                 "Location: https://%s:%d/\r\n"
                 "Connection: close\r\n\r\n",
                 host, g_https_port);
    }
    (void)write(new_socket, response, strlen(response));
    close(new_socket);
    return NULL;
}

static void *start_http_redirect_server(void *unused)
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    (void)unused;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)g_http_port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("HTTP bind failed");
        return NULL;
    }
    listen(server_fd, 32);
    printf("HTTP redirect :%d -> HTTPS :%d\n", g_http_port, g_https_port);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
            continue;

        pthread_t th;
        pthread_attr_t attr;
        int *fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) {
            close(new_socket);
            continue;
        }
        *fd_ptr = new_socket;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, http_redirect_thread, fd_ptr) != 0) {
            close(new_socket);
            free(fd_ptr);
        }
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

typedef struct {
    int fd;
    SSL_CTX *ctx;
} https_client_arg_t;

static void *https_client_thread(void *arg)
{
    https_client_arg_t *ca = (https_client_arg_t *)arg;
    int fd = ca->fd;
    SSL_CTX *ctx = ca->ctx;
    free(ca);

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        close(fd);
        return NULL;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) > 0)
        handle_https_client(ssl);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    return NULL;
}

void start_http_server(int ignore_port) {
    (void)ignore_port;
    load_config();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);

    /* One process + threads. Do NOT fork() after mqtt/email/part4 threads start. */
    {
        pthread_t http_th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&http_th, &attr, start_http_redirect_server, NULL) != 0)
            fprintf(stderr, "failed to start HTTP redirect thread\n");
        pthread_attr_destroy(&attr);
    }

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_certificate_file(ctx, "www/server.crt", SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, "www/server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)g_https_port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("HTTPS bind failed");
        exit(EXIT_FAILURE);
    }
    listen(server_fd, 64);
    printf("HTTPS :%d  student=%s target=%s (threaded)\n", g_https_port, env_student_id,
           g_target);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
            continue;

        https_client_arg_t *ca = malloc(sizeof(*ca));
        if (!ca) {
            close(new_socket);
            continue;
        }
        ca->fd = new_socket;
        ca->ctx = ctx;

        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, https_client_thread, ca) != 0) {
            close(new_socket);
            free(ca);
        }
        pthread_attr_destroy(&attr);
    }
}
