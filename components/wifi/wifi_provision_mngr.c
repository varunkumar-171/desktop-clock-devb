/**
 * @file wifi_provision_mngr.c
 * @brief Wi-Fi Provisioning Manager using SoftAP scheme and QR Code generation.
 * * Handles the logic for:
 * 1. Checking if the device is already provisioned.
 * 2. Starting a SoftAP for the mobile app if not provisioned.
 * 3. Handling connection retries and factory resets on failure.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "wifi_provision_mngr.h"
#include "qrcode.h"
#include "ui.h"

/* --- Configuration Constants --- */
static const char *TAG = "WIFI_PROV";

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_SOFTAP   "softap"
#define QRCODE_BASE_URL         "https://espressif.github.io/esp-jumpstart/qrcode.html"
#define MAX_WIFI_RETRIES        5


/* --- Global/Static Variables --- */
EventGroupHandle_t wifi_event_group = NULL;
static char qr_payload[150] = {0};
static int wifi_retry_count = 0;

/**
 * @brief Generates a unique service name based on the device MAC address.
 * Result format: PROV_XXXXXX (where X is the last 3 bytes of MAC)
 */
static void get_device_service_name(char *service_name, size_t max) {
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "PROV_%02X%02X%02X", eth_mac[3], eth_mac[4], eth_mac[5]);
}

/**
 * @brief LVGL Callback to update the UI with a generated QR Code.
 * Executed on the UI thread via lv_async_call.
 */
static void ui_update_qr_display_cb(void *arg) {
    // Ensure UI containers are visible
    if (ui_QRCodeContainer) lv_obj_clear_flag(ui_QRCodeContainer, LV_OBJ_FLAG_HIDDEN);
    if (ui_WiFiSetup) lv_obj_clear_flag(ui_WiFiSetup, LV_OBJ_FLAG_HIDDEN);

    static lv_obj_t *qr_obj = NULL;
    if (qr_obj == NULL) {
        qr_obj = lv_qrcode_create(ui_QRCodeContainer, 120, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
        lv_obj_center(qr_obj);
    }

    lv_qrcode_update(qr_obj, qr_payload, strlen(qr_payload));
}

/**
 * @brief Formats the provisioning payload and triggers UI/Console output.
 */
static void wifi_prov_generate_qr(const char *name, const char *pop, const char *transport) {
    if (pop) {
        snprintf(qr_payload, sizeof(qr_payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, pop, transport);
    } else {
        snprintf(qr_payload, sizeof(qr_payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, transport);
    }

    lv_async_call(ui_update_qr_display_cb, NULL);

    ESP_LOGI(TAG, "Provisioning QR Generated. Payload: %s", qr_payload);
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, qr_payload);
}

/**
 * @brief Unified Event Handler for Wi-Fi, IP, and Provisioning events.
 */
static void main_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    /* --- Provisioning Events --- */
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning service started.");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Credentials: SSID: %s", (const char *)wifi_sta_cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning credentials applied successfully.");
                break;
            case WIFI_PROV_END:
                wifi_prov_mgr_deinit();
                break;
            default: break;
        }
    } 
    /* --- Wi-Fi Events --- */
    else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (wifi_retry_count < MAX_WIFI_RETRIES) {
                    wifi_retry_count++;
                    ESP_LOGI(TAG, "Retry connection to AP... (%d/%d)", wifi_retry_count, MAX_WIFI_RETRIES);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "Max retries reached. Resetting provisioning for security.");
                    wifi_prov_mgr_reset_provisioning();
                    esp_restart();
                }
                break;
            default: break;
        }
    } 
    /* --- IP Events --- */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP Assigned: " IPSTR, IP2STR(&event->ip_info.ip));
        
        wifi_retry_count = 0; // Reset retries on success
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Initialized the Wi-Fi stack in Station mode.
 */
static void wifi_init_stack(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register all event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &main_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &main_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &main_event_handler, NULL));
}

/**
 * @brief Main Entry Point for Wi-Fi Management.
 */
void initialize_wifi_provisioning(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_stack();

    wifi_prov_mgr_config_t prov_config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(TAG, "Device not provisioned. Starting SoftAP service...");

        char service_name[16];
        get_device_service_name(service_name, sizeof(service_name));
        
        const char *pop = "abcd1234"; // Proof of Possession (PIN)
        
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, (const void *)pop, service_name, NULL));

        wifi_prov_generate_qr(service_name, pop, PROV_TRANSPORT_SOFTAP);
    } else {
        ESP_LOGI(TAG, "Already provisioned. Connecting to saved AP...");
        wifi_prov_mgr_deinit();
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    // Wait for connection before proceeding with tasks that require Wi-Fi
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
}