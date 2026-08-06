#include "http_server.h"
#include "email_alert.h"
#include "mqtt_pub.h"
#include "features_part4.h"

int main(void) {
    email_alert_start();
    mqtt_pub_start();
    part4_start();
    start_http_server(0);
    return 0;
}
