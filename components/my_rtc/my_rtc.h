#include "esp_sntp.h"

#ifndef __RTC_H__
#define __RTC_H__

#ifdef __cplusplus
extern "C" {
#endif

void initialize_sntp(void);
void time_sync_notification_cb(struct timeval *tv);
void set_timezone(const char *tz);
void set_screen_date_labels(void);
void update_clock_hands(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __RTC_H__ */


