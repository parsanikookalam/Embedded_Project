#ifndef MQTT_PUB_H
#define MQTT_PUB_H

/* Part 3C: C Mosquitto publisher (QoS1 + LWT). */
void mqtt_pub_start(void);

/* Part 4.1: emergency alarm topic home/<id>/alarm */
int mqtt_publish_alarm(int count, float cpu_temp, long timestamp);

#endif
