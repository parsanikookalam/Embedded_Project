#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "telemetry.h"
#include "http_server.h"

#define BUFFER_SIZE 4096
#define HTTP_PORT 80
#define HTTPS_PORT 443

char env_student_id[32] = "Unknown";
char env_student_name[64] = "Unknown";

static void load_config(void) {
    FILE *fp = fopen("../config.env", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "STUDENT_ID=", 11) == 0) sscanf(line, "STUDENT_ID=%[^\n]", env_student_id);
            else if (strncmp(line, "STUDENT_NAME=", 13) == 0) sscanf(line, "STUDENT_NAME=%[^\n]", env_student_name);
        }
        fclose(fp);
    }
}

// ---------------------------------------------------------
// PORT 80 HANDLER (301 REDIRECT)
// ---------------------------------------------------------
static void handle_http_redirect(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    read(client_fd, buffer, BUFFER_SIZE - 1);

    char *response = "HTTP/1.1 301 Moved Permanently\r\nLocation: https://127.0.0.1/\r\nConnection: close\r\n\r\n";
    write(client_fd, response, strlen(response));
    close(client_fd);
}

static void start_http_redirect_server(void) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("HTTP Bind failed. Did you run with sudo?");
        exit(EXIT_FAILURE);
    }
    listen(server_fd, 10);
    printf("HTTP Redirect Server running on port %d ...\n", HTTP_PORT);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) >= 0) {
            handle_http_redirect(new_socket);
        }
    }
}

// ---------------------------------------------------------
// PORT 443 HANDLER (HTTPS & TELEMETRY)
// ---------------------------------------------------------
static void handle_https_client(SSL *ssl) {
    char buffer[BUFFER_SIZE] = {0};
    SSL_read(ssl, buffer, BUFFER_SIZE - 1);

    if (strstr(buffer, "GET /api/v1/telemetry") != NULL) {
        SystemTelemetry t;
        get_system_telemetry(&t);
        char json_response[512];
        int body_len = snprintf(json_response, sizeof(json_response),
            "{\"cpu_temp\": %.2f, \"free_mem_kb\": %ld, \"mem_used_percent\": %.2f, \"cpu_usage_percent\": %.2f}",
            t.cpu_temp, t.free_mem_kb, t.mem_used_percent, t.cpu_usage_percent);

        char http_header[256];
        snprintf(http_header, sizeof(http_header),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", body_len);

        SSL_write(ssl, http_header, strlen(http_header));
        SSL_write(ssl, json_response, body_len);
    } 
    else if (strstr(buffer, "GET /api/v1/config") != NULL) {
        char json_response[256];
        int body_len = snprintf(json_response, sizeof(json_response),
            "{\"id\": \"%s\", \"name\": \"%s\"}", env_student_id, env_student_name);

        char http_header[256];
        snprintf(http_header, sizeof(http_header),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", body_len);

        SSL_write(ssl, http_header, strlen(http_header));
        SSL_write(ssl, json_response, body_len);
    }
    else if (strstr(buffer, "GET /api/v1/persons") != NULL) {
        char json_response[] = "{\"count\": 0, \"timestamp\": 1700000000}";
        char http_header[256];
        snprintf(http_header, sizeof(http_header),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(json_response));

        SSL_write(ssl, http_header, strlen(http_header));
        SSL_write(ssl, json_response, strlen(json_response));
    }
    else {
        FILE *fp = fopen("www/index.html", "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char *file_buf = malloc(file_size);
            fread(file_buf, 1, file_size, fp);
            fclose(fp);

            char http_header[256];
            snprintf(http_header, sizeof(http_header),
                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", file_size);

            SSL_write(ssl, http_header, strlen(http_header));
            SSL_write(ssl, file_buf, file_size);
            free(file_buf);
        }
    }
}

void start_http_server(int ignore_port) {
    load_config();

    // Fork the process to run Port 80 and Port 443 simultaneously
    pid_t pid = fork();
    if (pid == 0) {
        start_http_redirect_server();
        exit(0);
    }

    // Parent Process: Initialize OpenSSL for Port 443
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());

    if (SSL_CTX_use_certificate_file(ctx, "www/server.crt", SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, "www/server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(HTTPS_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("HTTPS Bind failed. Did you run with sudo?");
        exit(EXIT_FAILURE);
    }
    listen(server_fd, 10);
    printf("HTTPS Server running on port %d ...\n", HTTPS_PORT);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) >= 0) {
            SSL *ssl = SSL_new(ctx);
            SSL_set_fd(ssl, new_socket);
            if (SSL_accept(ssl) > 0) {
                handle_https_client(ssl);
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(new_socket);
        }
    }
    SSL_CTX_free(ctx);
}