#ifndef EMAIL_ALERT_H
#define EMAIL_ALERT_H

void email_alert_start(void);
int email_alert_test_send(void);

/* Generic event email; attach_jpg uses ../data/latest_detection.jpg when available. */
int email_send_event(const char *subject, const char *body_text, int attach_jpg);

#endif
