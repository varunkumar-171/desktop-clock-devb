/**
 * @file api_manager.c
 * @brief HTTP Client and JSON Parsing module for Weather and Quote services.
 * * This module handles periodic polling of REST APIs, parsing JSON payloads using cJSON,
 * and safely updating the LVGL UI via thread-safe callbacks.
 */

#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api.h"
#include "cJSON.h"
#include "ui.h"
#include "wifi_provision_mngr.h"

/* --- Configurations --- */
#define MAX_HTTP_OUTPUT_BUFFER    2048
#define API_POLLING_INTERVAL_MS   (8 * 60 * 60 * 1000) // 8 Hours
#define AQI_MAX_SCORE             500

static const char *TAG = "API_MGR";

/* API Endpoints */
static const char *URL_WEATHER = "http://api.weatherstack.com/current?access_key=cc9421d4a10355df65d6017f457a3ee4&query=Phoenix";
static const char *URL_QUOTE   = "https://zenquotes.io/api/random";

/* --- Global Data Structures --- */
static weather_data_t current_weather;  
static char quote[MAX_QUOTE_LEN]; // Buffer to hold the quote text

/**
 * @brief Calculates the Air Quality Index (AQI) based on PM2.5 concentration.
 * Uses standard EPA linear scaling breakpoints.
 * * @param pm25 Particulate matter 2.5 concentration in µg/m³
 * @return int Calculated AQI score (0-500)
 */
int calculate_aqi_score(float pm25) {
    if (pm25 < 0) return 0;
    
    if (pm25 <= 12.0)  return (int)(((50 - 0)    / (12.0 - 0))    * (pm25 - 0)    + 0);
    if (pm25 <= 35.4)  return (int)(((100 - 51)  / (35.4 - 12.1)) * (pm25 - 12.1) + 51);
    if (pm25 <= 55.4)  return (int)(((150 - 101) / (55.4 - 35.5)) * (pm25 - 35.5) + 101);
    if (pm25 <= 150.4) return (int)(((200 - 151) / (150.4 - 55.5)) * (pm25 - 55.5) + 151);
    if (pm25 <= 250.4) return (int)(((300 - 201) / (250.4 - 150.5)) * (pm25 - 150.5) + 201);
    if (pm25 <= 350.4) return (int)(((400 - 301) / (350.4 - 250.5)) * (pm25 - 250.5) + 301);
    
    return AQI_MAX_SCORE; 
}

/**
 * @brief Internal HTTP Event Handler.
 * Manages the lifecycle of the HTTP request and buffers incoming data.
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static int output_len = 0;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            output_len = 0; 
            break;

        case HTTP_EVENT_ON_DATA:
            if (evt->user_data) {
                // Initialize buffer on first data packet
                if (output_len == 0) {
                    memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
                }
                
                int copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len - 1));
                if (copy_len > 0) {
                    memcpy((char *)evt->user_data + output_len, evt->data, copy_len);
                    output_len += copy_len;
                }
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP Request Finished. Bytes: %d", output_len);
            output_len = 0;
            break;

        case HTTP_EVENT_DISCONNECTED:
            output_len = 0;
            break;

        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Parses the Weatherstack JSON response and populates the global weather struct.
 * * @param json_string Raw JSON data from the API
 */
static void parse_weather_json(const char *json_string) {
    cJSON *root = cJSON_Parse(json_string);
    if (!root) {
        ESP_LOGE(TAG, "Weather JSON Parse Failed");
        return; 
    }

    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (current) {
        // Helper macros/logic to safely extract data
        cJSON *temp = cJSON_GetObjectItemCaseSensitive(current, "temperature");
        if (cJSON_IsNumber(temp)) {
            snprintf(current_weather.temperature, sizeof(current_weather.temperature), "%d", temp->valueint);
        }

        cJSON *humidity = cJSON_GetObjectItemCaseSensitive(current, "humidity");
        if (cJSON_IsNumber(humidity)) {
            snprintf(current_weather.humidity, sizeof(current_weather.humidity), "%d", humidity->valueint);
        }

        cJSON *wind = cJSON_GetObjectItemCaseSensitive(current, "wind_speed");
        if (cJSON_IsNumber(wind)) {
            snprintf(current_weather.wind_speed, sizeof(current_weather.wind_speed), "%d", wind->valueint);
        }

        cJSON *precip = cJSON_GetObjectItemCaseSensitive(current, "precip");
        if (cJSON_IsNumber(precip)) {
            snprintf(current_weather.precip, sizeof(current_weather.precip), "%.1f", precip->valuedouble);
        }

        cJSON *desc_arr = cJSON_GetObjectItemCaseSensitive(current, "weather_descriptions");
        if (cJSON_IsArray(desc_arr)) {
            cJSON *desc = cJSON_GetArrayItem(desc_arr, 0);
            if (cJSON_IsString(desc)) {
                strlcpy(current_weather.description, desc->valuestring, sizeof(current_weather.description));
            }
        }

        cJSON *aq_obj = cJSON_GetObjectItemCaseSensitive(current, "air_quality");
        if (aq_obj) {
            cJSON *pm25_obj = cJSON_GetObjectItemCaseSensitive(aq_obj, "pm2_5");
            float val = cJSON_IsNumber(pm25_obj) ? pm25_obj->valuedouble : (cJSON_IsString(pm25_obj) ? atof(pm25_obj->valuestring) : 0.0f);
            snprintf(current_weather.aqi, sizeof(current_weather.aqi), "%d", calculate_aqi_score(val));
        }
    }
    cJSON_Delete(root);
}

/**
 * @brief Parses the ZenQuotes JSON response.
 * * @param json_string Raw JSON data from the API
 */
static void parse_quote_json(const char *json_string) {
    cJSON *root = cJSON_Parse(json_string);
    if (!root) return;

    cJSON *item = cJSON_GetArrayItem(root, 0);
    if (item) {
        cJSON *q = cJSON_GetObjectItemCaseSensitive(item, "q");
        cJSON *a = cJSON_GetObjectItemCaseSensitive(item, "a");

        if (cJSON_IsString(q) && cJSON_IsString(a)) {
            snprintf(quote, sizeof(quote), "\"%s\" - %s", q->valuestring, a->valuestring);
        }
    }
    cJSON_Delete(root);
}

/**
 * @brief Performs a blocking HTTP GET request and dispatches the response to a parser.
 * * @param url Target endpoint
 */
void fetch_api_data(const char *url) {
    char *buffer = malloc(MAX_HTTP_OUTPUT_BUFFER + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP buffer");
        return;
    }
    memset(buffer, 0, MAX_HTTP_OUTPUT_BUFFER + 1);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = buffer,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
        if (strstr(url, URL_WEATHER)) {
            parse_weather_json(buffer);
        } else if (strstr(url, URL_QUOTE)) {
            parse_quote_json(buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP Request failed for %s", url);
    }

    esp_http_client_cleanup(client);
    free(buffer);
}

/* --- LVGL UI Callbacks (Async) --- */

static void update_weather_ui_cb(void *user_data) {
    if (!ui_Temperature) return;
    lv_label_set_text(ui_Temperature, current_weather.temperature);
    lv_label_set_text(ui_Weather, current_weather.description);
    lv_label_set_text(ui_PrecipitationValue, current_weather.precip);
    lv_label_set_text(ui_HumidityValue, current_weather.humidity);
    lv_label_set_text(ui_WindValue, current_weather.wind_speed);
    lv_label_set_text(ui_AirQualityValue, current_weather.aqi);
}

static void update_quote_ui_cb(void *user_data) {
    if (!ui_ExtraLabel) return;
    lv_label_set_text(ui_ExtraLabel, quote);
}

/**
 * @brief Main API update loop. Runs as a FreeRTOS task.
 */
void api_update_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting API Update Task...");
    

    while (1) {  
        fetch_api_data(URL_WEATHER);
        fetch_api_data(URL_QUOTE); 
        
        // Push updates to UI thread safely
        lv_async_call(update_weather_ui_cb, NULL);
        lv_async_call(update_quote_ui_cb, NULL);

        ESP_LOGI(TAG, "Cycle complete. Next update in 8 hours.");
        vTaskDelay(pdMS_TO_TICKS(API_POLLING_INTERVAL_MS));
    }
}