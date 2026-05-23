/**
 * @file my_rtc.c
 * @brief Time Management Service for SNTP synchronization and UI clock logic.
 */

#include "my_rtc.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "ui.h"

static const char *TAG = "TIME_MGR";

/* --- Private Constants --- */
#define YEAR_THRESHOLD (2024 - 1900)  
#define NTP_RETRY_COUNT 15
#define LV_ANGLE_FULL_CIRCLE 3600     // LVGL angles are in 0.1 degree increments

/**
 * @brief Callback triggered by the ESP-IDF SNTP stack when time is synced.
 */
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "System time synchronized via NTP.");
}

/**
 * @brief Checks if the system clock has been successfully synchronized.
 */
bool time_is_synchronized(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return (timeinfo.tm_year >= YEAR_THRESHOLD);
}

/**
 * @brief Initializes SNTP and blocks until time is set or timeout occurs.
 */
void initialize_sntp(void) {
    if (time_is_synchronized()) {
        ESP_LOGI(TAG, "Time already set. Skipping SNTP init.");
        return;
    }

    ESP_LOGI(TAG, "Initializing SNTP for network time sync...");
    
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = time_sync_notification_cb;
    
    esp_netif_sntp_init(&config);

    // Polling for sync status
    int retry = 0;
    while (!time_is_synchronized() && ++retry <= NTP_RETRY_COUNT) {
        ESP_LOGD(TAG, "Waiting for sync... (%d/%d)", retry, NTP_RETRY_COUNT);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (time_is_synchronized()) {
        ESP_LOGI(TAG, "Time successfully synchronized.");
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out. Proceeding with system clock.");
    }

    // Usually, you keep SNTP running in background to maintain drift, 
    // but if you want to save resources, you can deinit here.
    // esp_netif_sntp_deinit(); 
}

/**
 * @brief Sets the environment timezone and updates the C library state.
 */
void set_timezone(const char *tz) {
    if (tz == NULL) return;

    setenv("TZ", tz, 1);
    tzset();

    // Log the converted time for verification
    time_t now;
    struct tm timeinfo;
    char buf[64];
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Timezone set to %s. Current time: %s", tz, buf);
}

/**
 * @brief Updates the static date labels on the UI only if the day has changed.
 * @note This should be called within your main clock loop.
 */
void set_screen_date_labels(void) {
    time_t now;
    struct tm ti;
    static int last_mday = -1; 

    time(&now);
    localtime_r(&now, &ti);

    if (ti.tm_mday == last_mday) {
        return;
    }

    char buf[32];
    ESP_LOGI(TAG, "Date changed or system startup. Updating UI date labels.");

    if (ui_Date) {
        snprintf(buf, sizeof(buf), "%02d,", ti.tm_mday);
        lv_label_set_text(ui_Date, buf);
    }
    
    if (ui_Month) {
        // %B provides the full month name (e.g., "January")
        strftime(buf, sizeof(buf), "%B", &ti);
        lv_label_set_text(ui_Month, buf);
    }
    
    if (ui_Year) {
        snprintf(buf, sizeof(buf), "%d", ti.tm_year + 1900);
        lv_label_set_text(ui_Year, buf);
    }
    
    if (ui_Day) {
        // %A provides the full weekday name (e.g., "Monday")
        strftime(buf, sizeof(buf), "%A", &ti);
        lv_label_set_text(ui_Day, buf);
    }

    // Update the cache
    last_mday = ti.tm_mday;
}

/**
 * @brief Animates/Updates the analog clock hands based on current time.
 */
void update_clock_hands(void) {
    struct tm ti;
    time_t now;
    time(&now);
    localtime_r(&now, &ti);

    /* Update Hands every call (likely every second or frame) */
    if (ui_SecondHand) {
        lv_img_set_angle(ui_SecondHand, (ti.tm_sec * 60));
    }

    if (ui_MinuteHand) {
        lv_img_set_angle(ui_MinuteHand, (ti.tm_min * 60) + (ti.tm_sec));
    }

    if (ui_HourHand) {
        lv_img_set_angle(ui_HourHand, ((ti.tm_hour % 12) * 300) + (ti.tm_min * 5));
    }

    /* Check if date labels need a refresh */
    set_screen_date_labels();
}