#ifndef TELEMETRY_H
#define TELEMETRY_H

typedef struct {
    float cpu_temp;
    float mem_used_percent;
    long free_mem_kb;
    float cpu_usage_percent;
} SystemTelemetry;

int get_system_telemetry(SystemTelemetry *telemetry);

#endif