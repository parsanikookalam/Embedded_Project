#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>
#include "telemetry.h"

static int temp_ok(float c)
{
    return c > 5.0f && c < 115.0f;
}

/* sysfs temps are usually millidegrees Celsius */
static float raw_to_celsius(long raw)
{
    if (labs(raw) > 1000)
        return (float)raw / 1000.0f;
    return (float)raw;
}

static int score_sensor_name(const char *name)
{
    char buf[128];
    size_t i, n;

    if (!name || !*name)
        return 1;

    n = strlen(name);
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    for (i = 0; i < n; i++)
        buf[i] = (char)tolower((unsigned char)name[i]);
    buf[n] = '\0';

    /* Prefer CPU / SoC package sensors; deprioritize WIFI/GPU-only if possible */
    if (strstr(buf, "x86_pkg") || strstr(buf, "package") || strstr(buf, "tctl") ||
        strstr(buf, "tdie") || strstr(buf, "cpu-thermal") || strstr(buf, "cpu_thermal") ||
        strstr(buf, "soc-thermal") || strstr(buf, "soc_thermal") || strcmp(buf, "cpu") == 0 ||
        strstr(buf, "k10temp") || strstr(buf, "coretemp") || strstr(buf, "cpu_temp"))
        return 100;
    if (strstr(buf, "cpu") || strstr(buf, "soc") || strstr(buf, "acpi"))
        return 60;
    if (strstr(buf, "gpu") || strstr(buf, "wifi") || strstr(buf, "npu"))
        return 20;
    return 40;
}

static void consider(float c, int score, float *best_c, int *best_score)
{
    if (!temp_ok(c))
        return;
    if (score > *best_score || (score == *best_score && c > *best_c)) {
        *best_c = c;
        *best_score = score;
    }
}

static float read_temp_thermal_zones(void)
{
    DIR *d = opendir("/sys/class/thermal");
    struct dirent *ent;
    float best_c = -1.0f;
    int best_score = -1;

    if (!d)
        return -1.0f;

    while ((ent = readdir(d)) != NULL) {
        char type_path[256], temp_path[256], type_name[128];
        FILE *fp;
        long raw = 0;

        if (strncmp(ent->d_name, "thermal_zone", 12) != 0)
            continue;

        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", ent->d_name);
        snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%s/temp", ent->d_name);

        type_name[0] = '\0';
        fp = fopen(type_path, "r");
        if (fp) {
            if (fgets(type_name, sizeof(type_name), fp)) {
                size_t len = strlen(type_name);
                while (len > 0 && (type_name[len - 1] == '\n' || type_name[len - 1] == '\r'))
                    type_name[--len] = '\0';
            }
            fclose(fp);
        }

        fp = fopen(temp_path, "r");
        if (!fp)
            continue;
        if (fscanf(fp, "%ld", &raw) == 1)
            consider(raw_to_celsius(raw), score_sensor_name(type_name), &best_c, &best_score);
        fclose(fp);
    }
    closedir(d);
    return best_c;
}

static float read_temp_hwmon(void)
{
    DIR *d = opendir("/sys/class/hwmon");
    struct dirent *ent;
    float best_c = -1.0f;
    int best_score = -1;

    if (!d)
        return -1.0f;

    while ((ent = readdir(d)) != NULL) {
        char chip_path[256], chip_name[128];
        DIR *hd;
        struct dirent *hent;
        FILE *fp;

        if (strncmp(ent->d_name, "hwmon", 5) != 0)
            continue;

        snprintf(chip_path, sizeof(chip_path), "/sys/class/hwmon/%s", ent->d_name);
        chip_name[0] = '\0';
        {
            char name_path[288];
            snprintf(name_path, sizeof(name_path), "%s/name", chip_path);
            fp = fopen(name_path, "r");
            if (fp) {
                if (fgets(chip_name, sizeof(chip_name), fp)) {
                    size_t len = strlen(chip_name);
                    while (len > 0 && (chip_name[len - 1] == '\n' || chip_name[len - 1] == '\r'))
                        chip_name[--len] = '\0';
                }
                fclose(fp);
            }
        }

        hd = opendir(chip_path);
        if (!hd)
            continue;

        while ((hent = readdir(hd)) != NULL) {
            char input_path[320], label_path[320], label[128];
            long raw = 0;
            int score;
            const char *p;

            /* match temp*_input e.g. temp1_input */
            p = hent->d_name;
            if (strncmp(p, "temp", 4) != 0)
                continue;
            p += 4;
            if (!isdigit((unsigned char)*p))
                continue;
            while (isdigit((unsigned char)*p))
                p++;
            if (strcmp(p, "_input") != 0)
                continue;

            snprintf(input_path, sizeof(input_path), "%s/%s", chip_path, hent->d_name);
            /* label file shares prefix: temp1_input -> temp1_label */
            snprintf(label_path, sizeof(label_path), "%s/%.*s_label", chip_path,
                     (int)(strlen(hent->d_name) - 6), hent->d_name);

            label[0] = '\0';
            fp = fopen(label_path, "r");
            if (fp) {
                if (fgets(label, sizeof(label), fp)) {
                    size_t len = strlen(label);
                    while (len > 0 && (label[len - 1] == '\n' || label[len - 1] == '\r'))
                        label[--len] = '\0';
                }
                fclose(fp);
            }

            score = score_sensor_name(label[0] ? label : chip_name);
            /* bump package / Tctl labels further */
            if (label[0])
                score += score_sensor_name(label) / 4;

            fp = fopen(input_path, "r");
            if (!fp)
                continue;
            if (fscanf(fp, "%ld", &raw) == 1)
                consider(raw_to_celsius(raw), score, &best_c, &best_score);
            fclose(fp);
        }
        closedir(hd);
    }
    closedir(d);
    return best_c;
}

/*
 * WSL rarely exposes real CPU thermals via Linux sysfs.
 * Use project helper (absolute powershell paths) — works under systemd.
 * Cached because PowerShell is slow.
 */
static int looks_like_wsl(void)
{
    if (getenv("WSL_DISTRO_NAME") || getenv("WSL_INTEROP"))
        return 1;
    if (access("/mnt/c/Windows/System32", F_OK) == 0)
        return 1;
    if (access("/proc/sys/fs/binfmt_misc/WSLInterop", F_OK) == 0)
        return 1;
    return 0;
}

static float read_temp_file_path(const char *path)
{
    struct stat st;
    FILE *fp;
    char line[64];
    float v;
    time_t now;

    if (!path || !path[0])
        return -1.0f;
    if (stat(path, &st) != 0)
        return -1.0f;
    now = time(NULL);
    /* Stale → ignore (Windows task refreshes ~every 30s) */
    if (now > st.st_mtime && (now - st.st_mtime) > 120)
        return -1.0f;

    fp = fopen(path, "r");
    if (!fp)
        return -1.0f;
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1.0f;
    }
    fclose(fp);
    /* Accept 47.85 or locale 47,85 */
    {
        char *p;
        for (p = line; *p; p++) {
            if (*p == ',')
                *p = '.';
        }
    }
    if (sscanf(line, "%f", &v) == 1 && temp_ok(v))
        return v;
    return -1.0f;
}

static float read_temp_from_cache_files(void)
{
    const char *env = getenv("HOST_CPU_TEMP_FILE");
    DIR *users;
    struct dirent *ent;
    float t;
    char path[512];

    t = read_temp_file_path(env);
    if (temp_ok(t))
        return t;

    t = read_temp_file_path("/home/parsa/embedded_project/data/host_cpu_temp.txt");
    if (temp_ok(t))
        return t;

    /* /mnt/c/Users/<anyone>/AppData/Local/SmartGuard/cpu_temp.txt */
    users = opendir("/mnt/c/Users");
    if (users) {
        while ((ent = readdir(users)) != NULL) {
            if (ent->d_name[0] == '.')
                continue;
            snprintf(path, sizeof(path),
                     "/mnt/c/Users/%s/AppData/Local/SmartGuard/cpu_temp.txt",
                     ent->d_name);
            t = read_temp_file_path(path);
            if (temp_ok(t)) {
                closedir(users);
                return t;
            }
        }
        closedir(users);
    }
    return -1.0f;
}

/*
 * WSL: prefer Windows-updated cache file (reliable under systemd).
 * Fallback: scripts/host_cpu_temp.sh → powershell once.
 */
static float read_temp_windows_host(void)
{
    static float cached = -1.0f;
    static time_t cached_at = 0;
    time_t now = time(NULL);
    FILE *fp;
    char line[128];
    float best = -1.0f;
    const char *cmd;
    char cmdbuf[512];

    if (!looks_like_wsl())
        return -1.0f;

    best = read_temp_from_cache_files();
    if (temp_ok(best)) {
        cached = best;
        cached_at = now;
        return best;
    }

    /* Fallback: invoke helper script (slow) — keep a short memory cache */
    if (cached_at != 0 && (now - cached_at) < 2 && temp_ok(cached))
        return cached;

    cmd = getenv("HOST_CPU_TEMP_CMD");
    if (cmd && cmd[0])
        snprintf(cmdbuf, sizeof(cmdbuf), "%s", cmd);
    else
        snprintf(cmdbuf, sizeof(cmdbuf),
                 "/bin/bash /home/parsa/embedded_project/scripts/host_cpu_temp.sh");

    fp = popen(cmdbuf, "r");
    if (!fp)
        return cached;

    best = -1.0f;
    while (fgets(line, sizeof(line), fp)) {
        char *s = line;
        float v;
        char *p;
        while (*s && isspace((unsigned char)*s))
            s++;
        for (p = s; *p; p++) {
            if (*p == ',')
                *p = '.';
        }
        if (sscanf(s, "%f", &v) == 1 && temp_ok(v) && v > best)
            best = v;
    }
    pclose(fp);

    if (temp_ok(best)) {
        cached = best;
        cached_at = now;
        return best;
    }
    return cached;
}

static float read_cpu_temp(void)
{
    float t;

    /* WSL cache file first — does not need live PowerShell from systemd */
    if (looks_like_wsl()) {
        t = read_temp_from_cache_files();
        if (temp_ok(t))
            return t;
    }

    /* Prefer labeled hwmon (coretemp / k10temp) — real Orange Pi / Linux */
    t = read_temp_hwmon();
    if (temp_ok(t))
        return t;

    t = read_temp_thermal_zones();
    if (temp_ok(t))
        return t;

    t = read_temp_windows_host();
    if (temp_ok(t))
        return t;

    return -1.0f;
}

static void read_memory_info(float *mem_percent, long *free_kb)
{
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        unsigned long long total_ram = (unsigned long long)info.totalram * info.mem_unit;
        unsigned long long free_ram = (unsigned long long)info.freeram * info.mem_unit;
        unsigned long long buffer_ram = (unsigned long long)info.bufferram * info.mem_unit;
        unsigned long long actual_free = free_ram + buffer_ram;

        *free_kb = (long)(actual_free / 1024);

        if (total_ram > 0) {
            *mem_percent = 100.0f * (1.0f - ((float)actual_free / (float)total_ram));
        } else {
            *mem_percent = 0.0f;
        }
    } else {
        *mem_percent = 0.0f;
        *free_kb = 0;
    }
}

static float read_cpu_usage(void)
{
    static unsigned long long prev_user = 0, prev_nice = 0, prev_system = 0, prev_idle = 0;
    static unsigned long long prev_iowait = 0, prev_irq = 0, prev_softirq = 0, prev_steal = 0;

    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return 0.0f;

    char cpu_label[8];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    if (fscanf(fp, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
               cpu_label, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 9) {
        fclose(fp);
        return 0.0f;
    }
    fclose(fp);

    unsigned long long prev_idle_total = prev_idle + prev_iowait;
    unsigned long long current_idle_total = idle + iowait;

    unsigned long long prev_non_idle =
        prev_user + prev_nice + prev_system + prev_irq + prev_softirq + prev_steal;
    unsigned long long current_non_idle = user + nice + system + irq + softirq + steal;

    unsigned long long prev_total = prev_idle_total + prev_non_idle;
    unsigned long long current_total = current_idle_total + current_non_idle;

    unsigned long long total_diff = current_total - prev_total;
    unsigned long long idle_diff = current_idle_total - prev_idle_total;

    prev_user = user;
    prev_nice = nice;
    prev_system = system;
    prev_idle = idle;
    prev_iowait = iowait;
    prev_irq = irq;
    prev_softirq = softirq;
    prev_steal = steal;

    if (total_diff == 0)
        return 0.0f;

    return 100.0f * (float)(total_diff - idle_diff) / (float)total_diff;
}

int get_system_telemetry(SystemTelemetry *t)
{
    if (!t)
        return -1;
    t->cpu_temp = read_cpu_temp();
    read_memory_info(&(t->mem_used_percent), &(t->free_mem_kb));
    t->cpu_usage_percent = read_cpu_usage();
    return 0;
}
